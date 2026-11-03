#include "sd_logger.h"
#include "sd_spi.h"
#include "gps.h"
#include "timebase.h"
#include "boot_identity.h"
#include "session.h"
#include "../FatFs/ff.h"
#include <stdio.h>
#include <string.h>

#define CAPACITY SD_LOG_QUEUE_CAPACITY
static struct Record {
    ImuRecord imu; LogTime time; GPS_Fix gps;
} queue[CAPACITY];
static unsigned head,tail,count;
static uint32_t last_sync,file_started,last_attempt,last_button,last_fix_id;
static unsigned file_number,rotation_records;
static uint32_t snapshot_ms,file_last_nr,file_records,file_synced_records;
static uint64_t file_last_sample_us,stop_at_us;
SDLogger_Diagnostics sd_diag;
unsigned SDLogger_RecordBytes(void) { return sizeof(queue[0]); }
static volatile uint8_t stop_requested,resume_requested,automatic_enabled;
static uint8_t opened,rotation_pending,recovery_pending,acquisition_fault,have_button;
static FATFS fs;
static FIL file;
static char batch[1024];
static uint8_t session_selected,metadata_gps_known,metadata_config_state,metadata_imu_verified;
char sd_log_session_dir[64];
volatile uint32_t sd_max_pause_us;
volatile LogState sd_log_state=LOG_OFF;
volatile uint32_t sd_log_written,sd_log_dropped;
volatile unsigned sd_log_error;
char sd_log_filename[13];

DWORD get_fattime(void)
{
    LogTime t=LogClock_AtUs(Timebase_NowUs());
    if (!t.valid) return (1U<<21)|(1U<<16); /* 1980-01-01 metadata only. */
    return ((DWORD)(t.year-1980)<<25)|((DWORD)t.month<<21)|((DWORD)t.day<<16)|
           ((DWORD)t.hour<<11)|((DWORD)t.minute<<5)|(t.second/2U);
}
static void fail(FRESULT result,LogState state)
{
    if (state==LOG_CARD_ERROR && result==FR_DENIED) state=LOG_FULL;
    /* A failed best-effort close must not hide the original I/O/init failure. */
    if (sd_log_state<LOG_CARD_ERROR) {
        sd_log_error=result; sd_log_state=state; recovery_pending=1;
        if (!sd_diag.init_failures) { sd_diag.first_error_us=Timebase_NowUs(); sd_diag.first_error_code=result; }
        ++sd_diag.init_failures; sd_diag.last_error_us=Timebase_NowUs(); sd_diag.last_error_code=result;
        sd_diag.clean_shutdown=0;
    }
}
void SDLogger_StatusText(char text[26])
{
    const char *label="SD OFF";
    switch (sd_log_state) {
    case LOG_RECORDING: label="SD REC"; break;
    case LOG_STOPPED: label="SD SAVED"; break;
    case LOG_FULL: label=automatic_enabled ? "SD FULL" : "PAUSE FULL"; break;
    case LOG_DATA_LOSS: label=automatic_enabled ? "SD DATA LOSS" : "PAUSE LOSS"; break;
    case LOG_CARD_ERROR: {
        const char *prefix=automatic_enabled ? "SD" : "PAUSE";
        const char *reason="FILESYSTEM";
        switch (sd_log_error) {
        case FR_NO_FILESYSTEM: reason="FAT16/32"; break;
        case FR_NOT_READY: reason="NOT READY"; break;
        case FR_DISK_ERR: reason="I/O"; break;
        case FR_WRITE_PROTECTED: reason="READ ONLY"; break;
        case FR_INVALID_OBJECT: reason="FILE"; break;
        case FR_INT_ERR: reason="INTERNAL"; break;
        default: break;
        }
        if (sd_spi_diag.error==SD_SPI_OCR && sd_spi_diag.ocr_valid)
            (void)snprintf(text,26,"%s E%02u OCR %08lX",prefix,sd_log_error,(unsigned long)sd_spi_diag.ocr);
        else if (sd_spi_diag.error!=SD_SPI_OK)
            (void)snprintf(text,26,"%s E%02u %s %02u:%02X",prefix,sd_log_error,SD_SPI_ErrorName(),
                           sd_spi_diag.command,sd_spi_diag.response);
        else (void)snprintf(text,26,"%s E%02u %s",prefix,sd_log_error,reason);
        size_t len=strlen(text); memset(text+len,' ',25U-len); text[25]='\0';
        return;
    }
    default: break;
    }
    if (sd_log_state==LOG_RECORDING)
        (void)snprintf(text,26,"REC S%04lX/%s",
            (unsigned long)(BootIdentity_Counter()&0xFFFFU),sd_log_filename);
    else if (sd_log_state==LOG_STOPPED)
        (void)snprintf(text,26,"SD SAVED S%04lX",(unsigned long)(BootIdentity_Counter()&0xFFFFU));
    else (void)snprintf(text,26,"%-12s%-12s ",label,sd_log_filename);
}
static uint8_t failed(void) { return sd_log_state>=LOG_CARD_ERROR; }
static void drop_records(unsigned n,uint32_t reason)
{
    if (n) {
        sd_diag.last_drop_nr=queue[(tail+n-1U)%CAPACITY].imu.nr;
        sd_diag.last_drop_us=Timebase_NowUs(); sd_diag.last_drop_reason=reason;
        sd_log_dropped+=n; tail=(tail+n)%CAPACITY; count-=n; sd_diag.queue_depth=count;
    }
}
static void confirmed_sync(void)
{
    sd_diag.synced_records+=file_records-file_synced_records; file_synced_records=file_records;
    if (file_records) {
        sd_diag.last_synced_nr=file_last_nr; sd_diag.last_synced_sample_us=file_last_sample_us;
        sd_diag.last_synced_us=Timebase_NowUs();
    }
}
static void open_next_file(void)
{
    FRESULT r=FR_DENIED;
    char name[sizeof(sd_log_filename)],path[96];
    while (file_number<9999U) {
        ++file_number;
        (void)snprintf(name,sizeof(name),"LOG%04u.TXT",file_number);
        (void)snprintf(path,sizeof(path),"%s/%s",sd_log_session_dir,name);
        r=f_open(&file,path,FA_WRITE|FA_CREATE_NEW);
        if (r!=FR_EXIST) break;
    }
    if (r!=FR_OK) { fail(r,r==FR_EXIST || r==FR_DENIED ? LOG_FULL : LOG_CARD_ERROR); return; }
    opened=1; file_records=file_synced_records=file_last_nr=0; file_last_sample_us=0;
    (void)strcpy(sd_log_filename,name);
    static const char header[]="NR;DATA;CZAS;ACC_X;ACC_Y;ACC_Z;GYRO_X;GYRO_Y;GYRO_Z;GPS_LAT;GPS_LON;GPS_SPEED_KMH;GPS_ALT_MSL_M;T_IMU_US;GPS_STATE;GPS_FIX_ID;GPS_ITOW_MS;IMU_VALID;IMU_FLAGS;T_ACC_US;T_GYRO_US;IMU_DROPPED;IMU_SPI_ERRORS;IMU_PAIR_ERRORS;GPS_UART_ERRORS;GPS_CHECKSUM_ERRORS;GPS_INCOMPLETE_EPOCHS;GPS_LATE_EPOCHS;SD_QUEUE_DROPPED;SD_MAX_PAUSE_US;IMU_MISSED_DRDY;GPS_RX_OVERFLOWS;GPS_INVALID_FIXES;GPS_T_RX_US;GPS_T_MEAS_US;TIME_SYNC_STATE\r\n";
    UINT wrote=0;
    r=f_write(&file,header,sizeof(header)-1,&wrote);
    if (r==FR_OK && wrote!=sizeof(header)-1) r=FR_DENIED;
    if (r==FR_OK) r=f_sync(&file);
    if (r!=FR_OK) { fail(r,r==FR_DENIED ? LOG_FULL : LOG_CARD_ERROR); return; }
    file_started=last_sync=HAL_GetTick();
}
static void close_file(void)
{
    FRESULT r=f_close(&file); opened=0;
    if (r!=FR_OK) fail(r,LOG_CARD_ERROR); else confirmed_sync();
}
static void cleanup_failure(void)
{
    if (!recovery_pending) return;
    /* After a card I/O failure, abandon the old FIL/cache before any remount.
       Never replay its FAT writes onto a newly inserted, possibly different card. */
    if (opened && sd_log_state!=LOG_CARD_ERROR) close_file();
    opened=0; rotation_pending=rotation_records=0; /* Preserve unattempted records for retry. */
    (void)f_mount(NULL,"",0);
    memset(&file,0,sizeof(file)); recovery_pending=0; last_attempt=HAL_GetTick();
}
static void connect_card(void)
{
    ++sd_diag.mount_attempts; sd_diag.clean_shutdown=0;
    (void)f_mount(NULL,"",0);
    sd_log_error=0; sd_log_state=LOG_OFF; sd_log_filename[0]='\0';
    FRESULT r=f_mount(&fs,"",1);
    if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
    DWORD free_clusters; FATFS *volume;
    r=f_getfree("",&free_clusters,&volume);
    if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
    /* Do not create an empty file on each retry while the same card is full. */
    if (!free_clusters) { fail(FR_DENIED,LOG_FULL); return; }
    if (!session_selected) {
        for (unsigned n=0;n<100;++n) {
            if (!n) (void)snprintf(sd_log_session_dir,sizeof(sd_log_session_dir),"%s",BootIdentity_Directory());
            else (void)snprintf(sd_log_session_dir,sizeof(sd_log_session_dir),"%s-%02u",BootIdentity_Directory(),n);
            r=f_mkdir(sd_log_session_dir);
            if (r!=FR_EXIST) break;
        }
        if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
        session_selected=1;
    } else {
        r=f_mkdir(sd_log_session_dir);
        if (r!=FR_OK && r!=FR_EXIST) { fail(r,LOG_CARD_ERROR); return; }
    }
    r=Session_Write(sd_log_session_dir);
    if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
    metadata_gps_known=strcmp(GPS_SoftwareVersion(),"unknown")!=0;
    metadata_config_state=GPS_GetConfig()->state; metadata_imu_verified=lsm6ds3_config_verified;
    open_next_file();
    if (sd_log_state==LOG_OFF && opened) {
        sd_log_state=LOG_RECORDING;
        if (!sd_diag.sd_ready_us) sd_diag.sd_ready_us=Timebase_NowUs();
    }
    snapshot_ms=HAL_GetTick();
}
static void pause_since(uint64_t start)
{
    uint64_t elapsed=Timebase_NowUs()-start;
    if (elapsed>sd_max_pause_us) sd_max_pause_us=elapsed>UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}
void SDLogger_Start(void)
{
    uint64_t start=Timebase_NowUs();
    memset(&sd_diag,0,sizeof(sd_diag));
    head=tail=count=0; sd_log_written=sd_log_dropped=sd_max_pause_us=last_fix_id=0;
    file_number=0; session_selected=0; sd_log_session_dir[0]=0;
    rotation_records=0; rotation_pending=recovery_pending=acquisition_fault=have_button=0;
    stop_requested=resume_requested=opened=0; automatic_enabled=1;
    connect_card(); if (failed()) cleanup_failure();
    pause_since(start);
}
void SDLogger_RequestStop(void) { stop_at_us=Timebase_NowUs(); automatic_enabled=0; resume_requested=0; stop_requested=1; }
void SDLogger_RequestResume(void) { automatic_enabled=1; resume_requested=1; }
void SDLogger_Button(void)
{
    uint32_t now=HAL_GetTick();
    if (have_button && (uint32_t)(now-last_button)<200U) return;
    have_button=1; last_button=now;
    if (automatic_enabled) SDLogger_RequestStop(); else SDLogger_RequestResume();
}
void SDLogger_DataLoss(void)
{
    acquisition_fault=1;
    if (sd_log_state==LOG_RECORDING) { ++sd_log_dropped; fail(FR_INT_ERR,LOG_DATA_LOSS); }
}
uint8_t SDLogger_Ready(void) { return opened && sd_log_state==LOG_RECORDING && automatic_enabled && !stop_requested; }
uint8_t SDLogger_CanAccept(void)
{ return (opened && sd_log_state==LOG_RECORDING) ? count<CAPACITY : !automatic_enabled; }
uint8_t SDLogger_ShouldWait(uint64_t t)
{
    if (!automatic_enabled && (sd_log_state!=LOG_RECORDING || t>stop_at_us)) return 0;
    uint64_t now=Timebase_NowUs(); return now<t || now-t<SD_LOG_HOLDBACK_US;
}
static uint8_t stop_has_raw(void)
{ ImuRecord r; return ImuStream_Peek(&r) && r.t_us<=stop_at_us; }
uint8_t SDLogger_AppendRaw(const ImuRecord *record,LogTime time)
{
    acquisition_fault=0;
    if (!automatic_enabled && (sd_log_state!=LOG_RECORDING || record->t_us>stop_at_us)) {
        ++sd_log_dropped; sd_diag.last_drop_nr=record->nr; sd_diag.last_drop_reason=5;
        sd_diag.last_drop_us=Timebase_NowUs(); return 1;
    }
    if (!opened || sd_log_state!=LOG_RECORDING || count==CAPACITY) return 0; /* Caller retains ownership. */
    queue[head].imu=*record; queue[head].time=time;
    ImuRecord next;
    uint64_t midpoint=record->t_us;
    if (ImuStream_PeekNext(&next) && next.t_us>=record->t_us) midpoint+=(next.t_us-record->t_us)/2U;
    GPS_Epoch_ForWindow(record->t_us,midpoint,&queue[head].gps);
    head=(head+1U)%CAPACITY; ++count; sd_diag.queue_depth=count;
    if (count>sd_diag.queue_high_water) sd_diag.queue_high_water=count;
    return 1;
}
static void fixed(char out[24],int64_t value,unsigned factor,unsigned digits)
{
    uint64_t mag=value<0 ? (uint64_t)-value : (uint64_t)value;
    (void)snprintf(out,24,"%s%lu.%0*lu",value<0 ? "-" : "",(unsigned long)(mag/factor),(int)digits,(unsigned long)(mag%factor));
}
/* nano.specs does not promise %llu. Preserve all 64 bits without printf. */
static void decimal64(char out[21],uint64_t n)
{
    char reversed[20]; unsigned len=0;
    do { reversed[len++]=(char)('0'+n%10U); n/=10U; } while (n);
    for (unsigned i=0;i<len;++i) out[i]=reversed[len-1-i];
    out[len]=0;
}
static unsigned format(char out[512],const struct Record *r,uint32_t prior_fix)
{
    char date[16]="",time[24]="",value[6][24]={{0}},navigation[4][24]={{0}},id[16]="",itow[16]="";
    if (r->time.valid) {
        (void)snprintf(date,sizeof(date),"%04u-%02u-%02u",r->time.year,r->time.month,r->time.day);
        (void)snprintf(time,sizeof(time),"%02u:%02u:%02u.%03u",r->time.hour,r->time.minute,r->time.second,r->time.millisecond);
    }
    if (!r->imu.flags) for (unsigned i=0;i<6;++i) fixed(value[i],(int64_t)r->imu.raw[i]*(i<3 ? 488 : 70000),1000000,6);
    if (r->gps.position_valid) {
        fixed(navigation[0],r->gps.latitude_e7,10000000,7); fixed(navigation[1],r->gps.longitude_e7,10000000,7);
    }
    if (r->gps.epoch_known) {
        (void)snprintf(id,sizeof(id),"%lu",(unsigned long)r->gps.fix_id);
        (void)snprintf(itow,sizeof(itow),"%lu",(unsigned long)r->gps.itow_ms);
    }
    if (r->gps.speed_valid)
        fixed(navigation[2],r->gps.speed_millikmh,1000,3);
    if (r->gps.altitude_valid) {
        fixed(navigation[3],r->gps.altitude_mm,1000,3);
    }
    uint64_t acc=r->imu.t_us,gyro=r->imu.t_us;
    uint32_t skew=r->imu.acc_minus_gyro_us<0 ? (uint32_t)-(int64_t)r->imu.acc_minus_gyro_us : (uint32_t)r->imu.acc_minus_gyro_us;
    if (r->imu.acc_minus_gyro_us<0) acc=acc>=skew ? acc-skew : 0;
    else gyro=gyro>=skew ? gyro-skew : 0;
    char t[21],ta[21],tg[21],rx[21]="",measurement[21]="";
    if (r->gps.epoch_known) decimal64(rx,r->gps.rx_us);
    if (r->gps.time_sync_state) decimal64(measurement,r->gps.rx_us-r->gps.meas_delay_us);
    decimal64(t,r->imu.t_us); decimal64(ta,acc); decimal64(tg,gyro);
    return (unsigned)snprintf(out,512,"%lu;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%c;%s;%s;%u;%u;%s;%s;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%s;%s;%u\r\n",
        (unsigned long)r->imu.nr,date,time,value[0],value[1],value[2],value[3],value[4],value[5],
        navigation[0],navigation[1],navigation[2],navigation[3],t,!r->gps.position_valid ? 'I' : (r->gps.fix_id==prior_fix ? 'R' : 'N'),id,itow,
        !r->imu.flags,r->imu.flags,ta,tg,
        (unsigned long)imu_diag.dropped,(unsigned long)imu_diag.spi_errors,(unsigned long)imu_diag.pair_errors,
        (unsigned long)gps_diag.uart_errors,(unsigned long)gps_diag.checksum_errors,(unsigned long)gps_diag.incomplete,
        (unsigned long)gps_diag.late,(unsigned long)sd_log_dropped,(unsigned long)sd_max_pause_us,(unsigned long)imu_diag.missed_drdy,
        (unsigned long)gps_diag.rx_overflows,(unsigned long)gps_diag.invalid_fixes,rx,measurement,r->gps.time_sync_state);
}
static void process(uint32_t now)
{
    if (!automatic_enabled) stop_requested=1;
    if (failed()) { cleanup_failure(); now=HAL_GetTick(); }
    if (!opened && automatic_enabled &&
        (resume_requested || (failed() && (uint32_t)(now-last_attempt)>=SD_LOG_RETRY_INTERVAL_MS)) &&
        (sd_log_state!=LOG_DATA_LOSS || !acquisition_fault)) {
        resume_requested=0; stop_requested=0;
        /* B1 may interrupt between the retry condition and clearing the flags. */
        if (!automatic_enabled) { stop_requested=1; return; }
        connect_card();
        if (failed()) cleanup_failure();
        return;
    }
    if (stop_requested) rotation_pending=0;
    if (sd_log_state==LOG_RECORDING && !stop_requested && !rotation_pending &&
        (uint32_t)(now-file_started)>=SD_LOG_FILE_DURATION_MS) {
        /* Snapshot the boundary. Samples arriving during I/O belong to the next file. */
        rotation_pending=1; rotation_records=count;
    }
    if (sd_log_state==LOG_RECORDING && count && (!rotation_pending || rotation_records)) {
        unsigned bytes=0,records=0,available=count;
        uint32_t batch_fix=last_fix_id;
        if (rotation_pending) available=rotation_records;
        while (records<available && sizeof(batch)-bytes>=512U) {
            const struct Record *r=&queue[(tail+records)%CAPACITY];
            /* Format into the static batch itself; a 512-byte temporary on the
               stack would leave too little headroom for nested UART/DMA IRQs. */
            unsigned len=format(batch+bytes,r,batch_fix);
            if (len>=512U) { fail(FR_INT_ERR,LOG_CARD_ERROR); break; }
            bytes+=len; ++records;
            if (r->gps.position_valid) batch_fix=r->gps.fix_id;
        }
        if (sd_log_state==LOG_RECORDING) {
            UINT wrote=0;
            uint64_t start=Timebase_NowUs(); FRESULT r=f_write(&file,batch,bytes,&wrote);
            uint64_t elapsed=Timebase_NowUs()-start;
            if (elapsed>sd_max_pause_us) sd_max_pause_us=(uint32_t)elapsed;
            if (r!=FR_OK || wrote!=bytes) {
                /* Only this attempted batch is uncertain. Unattempted queue entries stay owned. */
                drop_records(records,7); fail(r==FR_OK ? FR_DENIED : r,r==FR_OK ? LOG_FULL : LOG_CARD_ERROR);
            }
            else {
                const ImuRecord *first=&queue[tail].imu,*last=&queue[(tail+records-1U)%CAPACITY].imu;
                if (!sd_log_written) {
                    sd_diag.first_written_us=Timebase_NowUs(); sd_diag.first_written_nr=first->nr;
                    sd_diag.first_written_sample_us=first->t_us;
                }
                sd_diag.last_written_nr=file_last_nr=last->nr; file_last_sample_us=last->t_us;
                file_records+=records;
                tail=(tail+records)%CAPACITY; count-=records; sd_diag.queue_depth=count;
                sd_log_written+=records; last_fix_id=batch_fix;
                if (rotation_pending) rotation_records-=records;
            }
        }
    }
    if (sd_log_state==LOG_RECORDING && rotation_pending && !rotation_records && !stop_requested) {
        close_file(); rotation_pending=0;
        if (sd_log_state==LOG_RECORDING) { FRESULT r=Session_Write(sd_log_session_dir); if (r!=FR_OK) fail(r,LOG_CARD_ERROR); }
        /* Keep accepting into RAM while close/open performs SD I/O. A stop arriving
           here still needs a new file if any samples are already queued. */
        if (sd_log_state==LOG_RECORDING) {
            if (!stop_requested || count) open_next_file();
        }
    }
    if (opened && sd_log_state==LOG_RECORDING && (uint32_t)(HAL_GetTick()-last_sync)>=SD_LOG_SYNC_INTERVAL_MS) {
        FRESULT r=f_sync(&file); last_sync=HAL_GetTick();
        if (r!=FR_OK) fail(r,LOG_CARD_ERROR); else confirmed_sync();
    }
    if (opened && sd_log_state==LOG_RECORDING && ((uint32_t)(HAL_GetTick()-snapshot_ms)>=SD_LOG_SNAPSHOT_INTERVAL_MS || (!metadata_gps_known && strcmp(GPS_SoftwareVersion(),"unknown")) ||
        metadata_config_state!=GPS_GetConfig()->state || metadata_imu_verified!=lsm6ds3_config_verified)) {
        FRESULT r=f_sync(&file);
        if (r==FR_OK) { confirmed_sync(); r=Session_Write(sd_log_session_dir); }
        snapshot_ms=HAL_GetTick(); metadata_gps_known=strcmp(GPS_SoftwareVersion(),"unknown")!=0;
        metadata_config_state=GPS_GetConfig()->state; metadata_imu_verified=lsm6ds3_config_verified;
        if (r!=FR_OK) fail(r,LOG_CARD_ERROR);
    }
    if (sd_log_state==LOG_RECORDING && stop_requested && count==0 && !stop_has_raw()) {
        if (opened) close_file();
        if (sd_log_state==LOG_RECORDING) {
            sd_diag.clean_shutdown=1;
            FRESULT r=Session_Write(sd_log_session_dir); if (r!=FR_OK) fail(r,LOG_CARD_ERROR);
        }
        if (sd_log_state==LOG_RECORDING) sd_log_state=LOG_STOPPED;
    }
    if (failed()) cleanup_failure();
}

void SDLogger_Process(uint32_t now)
{
    uint64_t start=Timebase_NowUs(); process(now); pause_since(start);
}
