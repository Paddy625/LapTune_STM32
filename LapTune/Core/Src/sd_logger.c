#include "sd_logger.h"
#include "sd_spi.h"
#include "../FatFs/ff.h"
#include <stdio.h>
#include <string.h>

#define CAPACITY 128U
struct Navigation {
    int32_t latitude_e7,longitude_e7;
    uint32_t speed_millikmh;
    uint8_t position_valid,speed_valid;
};
static struct Record {
    LSM6DS3_Sample sample; LogTime time; uint32_t number; struct Navigation gps;
} queue[CAPACITY];
static unsigned head,tail,count;
static uint32_t sequence,last_sync,file_started,last_attempt,last_button;
static unsigned file_number,rotation_records;
static volatile uint8_t stop_requested,resume_requested,automatic_enabled;
static uint8_t opened,rotation_pending,recovery_pending,acquisition_fault,have_button;
static FATFS fs;
static FIL file;
static char batch[1024];
volatile LogState sd_log_state=LOG_OFF;
volatile uint32_t sd_log_written,sd_log_dropped;
volatile unsigned sd_log_error;
char sd_log_filename[13];

DWORD get_fattime(void)
{
    LogTime t=LogClock_At(HAL_GetTick());
    if (!t.valid) return (1U<<21)|(1U<<16); /* 1980-01-01 metadata only. */
    return ((DWORD)(t.year-1980)<<25)|((DWORD)t.month<<21)|((DWORD)t.day<<16)|
           ((DWORD)t.hour<<11)|((DWORD)t.minute<<5)|(t.second/2U);
}
static void fail(FRESULT result,LogState state)
{
    /* A failed best-effort close must not hide the original I/O/init failure. */
    if (sd_log_state<LOG_CARD_ERROR) {
        sd_log_error=result; sd_log_state=state; recovery_pending=1;
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
    (void)snprintf(text,26,"%-12s%-12s ",label,sd_log_filename);
}
static uint8_t failed(void) { return sd_log_state>=LOG_CARD_ERROR; }
static void discard_queue(void)
{
    sd_log_dropped+=count; head=tail=count=0; rotation_pending=0; rotation_records=0;
}
static void open_next_file(void)
{
    FRESULT r=FR_DENIED;
    char name[sizeof(sd_log_filename)];
    while (file_number<9999U) {
        ++file_number;
        (void)snprintf(name,sizeof(name),"LOG%04u.TXT",file_number);
        r=f_open(&file,name,FA_WRITE|FA_CREATE_NEW);
        if (r!=FR_EXIST) break;
    }
    if (r!=FR_OK) { fail(r,r==FR_EXIST || r==FR_DENIED ? LOG_FULL : LOG_CARD_ERROR); return; }
    opened=1;
    (void)strcpy(sd_log_filename,name);
    static const char header[]="NR;DATA;CZAS;ACC_X;ACC_Y;ACC_Z;GYRO_X;GYRO_Y;GYRO_Z;GPS_LAT;GPS_LON;GPS_SPEED_KMH\r\n";
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
    if (r!=FR_OK) fail(r,LOG_CARD_ERROR);
}
static void cleanup_failure(void)
{
    if (!recovery_pending) return;
    /* After a card I/O failure, abandon the old FIL/cache before any remount.
       Never replay its FAT writes onto a newly inserted, possibly different card. */
    if (opened && sd_log_state!=LOG_CARD_ERROR) close_file();
    opened=0; discard_queue();
    (void)f_mount(NULL,"",0);
    memset(&file,0,sizeof(file)); recovery_pending=0; last_attempt=HAL_GetTick();
}
static void connect_card(void)
{
    (void)f_mount(NULL,"",0);
    file_number=0; sd_log_error=0; sd_log_state=LOG_OFF; sd_log_filename[0]='\0';
    FRESULT r=f_mount(&fs,"",1);
    if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
    DWORD free_clusters; FATFS *volume;
    r=f_getfree("",&free_clusters,&volume);
    if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
    /* Do not create an empty file on each retry while the same card is full. */
    if (!free_clusters) { fail(FR_DENIED,LOG_FULL); return; }
    open_next_file();
    if (sd_log_state==LOG_OFF && opened) sd_log_state=LOG_RECORDING;
}
void SDLogger_Start(void)
{
    head=tail=count=sequence=0; sd_log_written=sd_log_dropped=0;
    rotation_records=0; rotation_pending=recovery_pending=acquisition_fault=have_button=0;
    stop_requested=resume_requested=opened=0; automatic_enabled=1;
    connect_card(); if (failed()) cleanup_failure();
}
void SDLogger_RequestStop(void) { automatic_enabled=0; resume_requested=0; stop_requested=1; }
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
void SDLogger_Append(const LSM6DS3_Sample *sample,LogTime time,const GPS_Data *gps)
{
    ++sequence; acquisition_fault=0;
    if (sd_log_state!=LOG_RECORDING || stop_requested || !automatic_enabled) { ++sd_log_dropped; return; }
    if (count==CAPACITY) { ++sd_log_dropped; fail(FR_INT_ERR,LOG_DATA_LOSS); return; }
    queue[head].sample=*sample; queue[head].time=time; queue[head].number=sequence;
    struct Navigation nav={0};
    uint32_t now=HAL_GetTick();
    if (gps && gps->connected) {
        if (gps->position_valid && (uint32_t)(now-gps->position_ms)<GPS_FRESH_MS) {
            nav.latitude_e7=gps->latitude_e7; nav.longitude_e7=gps->longitude_e7;
            nav.position_valid=1;
        }
        if (gps->speed_valid && (uint32_t)(now-gps->speed_ms)<GPS_FRESH_MS) {
            nav.speed_millikmh=gps->speed_millikmh; nav.speed_valid=1;
        }
    }
    queue[head].gps=nav;
    head=(head+1U)%CAPACITY; ++count;
}
static void fixed(char out[20],float value,unsigned factor,unsigned digits)
{
    int32_t rounded=(int32_t)(value*factor+(value<0 ? -0.5f : 0.5f));
    uint32_t mag=(uint32_t)(rounded<0 ? -rounded : rounded);
    (void)snprintf(out,20,"%s%lu.%0*lu",rounded<0 ? "-" : "",(unsigned long)(mag/factor),
                   (int)digits,(unsigned long)(mag%factor));
}
static unsigned format(char out[192],const struct Record *r)
{
    char date[16]="",time[24]="",value[6][20],navigation[3][20]={{0}};
    if (r->time.valid) {
        (void)snprintf(date,sizeof(date),"%04u-%02u-%02u",r->time.year,r->time.month,r->time.day);
        (void)snprintf(time,sizeof(time),"%02u:%02u:%02u.%03u",r->time.hour,r->time.minute,r->time.second,r->time.millisecond);
    }
    fixed(value[0],r->sample.ax,1000000,6); fixed(value[1],r->sample.ay,1000000,6);
    fixed(value[2],r->sample.az,1000000,6); fixed(value[3],r->sample.gx,1000,3);
    fixed(value[4],r->sample.gy,1000,3); fixed(value[5],r->sample.gz,1000,3);
    if (r->gps.position_valid) {
        const int32_t coordinates[]={r->gps.latitude_e7,r->gps.longitude_e7};
        for (unsigned i=0;i<2;++i) {
            uint32_t magnitude=coordinates[i]<0 ? 0U-(uint32_t)coordinates[i] : (uint32_t)coordinates[i];
            (void)snprintf(navigation[i],20,"%s%lu.%07lu",coordinates[i]<0 ? "-" : "",
                           (unsigned long)(magnitude/10000000U),(unsigned long)(magnitude%10000000U));
        }
    }
    if (r->gps.speed_valid)
        (void)snprintf(navigation[2],20,"%lu.%03lu",(unsigned long)(r->gps.speed_millikmh/1000U),
                       (unsigned long)(r->gps.speed_millikmh%1000U));
    return (unsigned)snprintf(out,192,"%lu;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s\r\n",
        (unsigned long)r->number,date,time,value[0],value[1],value[2],value[3],value[4],value[5],
        navigation[0],navigation[1],navigation[2]);
}
void SDLogger_Process(uint32_t now)
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
        if (rotation_pending) available=rotation_records;
        while (records<available) {
            char line[192]; unsigned len=format(line,&queue[(tail+records)%CAPACITY]);
            if (len>=sizeof(line)) { fail(FR_INT_ERR,LOG_CARD_ERROR); break; }
            if (len>sizeof(batch)-bytes) break;
            memcpy(batch+bytes,line,len); bytes+=len; ++records;
        }
        if (sd_log_state==LOG_RECORDING) {
            UINT wrote=0;
            FRESULT r=f_write(&file,batch,bytes,&wrote);
            if (r!=FR_OK || wrote!=bytes) { fail(r==FR_OK ? FR_DENIED : r,r==FR_OK ? LOG_FULL : LOG_CARD_ERROR); }
            else {
                tail=(tail+records)%CAPACITY; count-=records; sd_log_written+=records;
                if (rotation_pending) rotation_records-=records;
            }
        }
    }
    if (sd_log_state==LOG_RECORDING && rotation_pending && !rotation_records && !stop_requested) {
        close_file(); rotation_pending=0;
        /* Keep accepting into RAM while close/open performs SD I/O. A stop arriving
           here still needs a new file if any samples are already queued. */
        if (sd_log_state==LOG_RECORDING) {
            if (stop_requested && !count) sd_log_state=LOG_STOPPED;
            else open_next_file();
        }
    }
    if (sd_log_state==LOG_RECORDING && (uint32_t)(HAL_GetTick()-last_sync)>=SD_LOG_SYNC_INTERVAL_MS) {
        FRESULT r=f_sync(&file); last_sync=HAL_GetTick();
        if (r!=FR_OK) fail(r,LOG_CARD_ERROR);
    }
    if (opened && sd_log_state==LOG_RECORDING && stop_requested && count==0) {
        close_file();
        if (sd_log_state==LOG_RECORDING) sd_log_state=LOG_STOPPED;
    }
    if (failed()) cleanup_failure();
}
