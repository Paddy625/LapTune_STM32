#include "sd_logger.h"
#include "sd_spi.h"
#include "../FatFs/ff.h"
#include <stdio.h>
#include <string.h>

#define CAPACITY 128U
static struct Record { LSM6DS3_Sample sample; LogTime time; uint32_t number; } queue[CAPACITY];
static unsigned head,tail,count;
static uint32_t sequence,last_sync,file_started;
static unsigned file_number,rotation_records;
static volatile uint8_t stop_requested;
static uint8_t opened,rotation_pending;
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
    if (sd_log_state<LOG_CARD_ERROR) { sd_log_error=result; sd_log_state=state; }
    stop_requested=1;
}
void SDLogger_StatusText(char text[26])
{
    const char *label="SD OFF";
    switch (sd_log_state) {
    case LOG_RECORDING: label="SD REC"; break;
    case LOG_STOPPED: label="SD SAVED"; break;
    case LOG_FULL: label="SD FULL"; break;
    case LOG_DATA_LOSS: label="SD DATA LOSS"; break;
    case LOG_CARD_ERROR: {
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
        if (sd_spi_diag.error!=SD_SPI_OK)
            (void)snprintf(text,26,"SD E%02u %s %02u:%02X",sd_log_error,SD_SPI_ErrorName(),
                           sd_spi_diag.command,sd_spi_diag.response);
        else (void)snprintf(text,26,"SD E%02u %s",sd_log_error,reason);
        size_t len=strlen(text); memset(text+len,' ',25U-len); text[25]='\0';
        return;
    }
    default: break;
    }
    (void)snprintf(text,26,"%-12s%-12s ",label,sd_log_filename);
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
    static const char header[]="NR;DATA;CZAS;ACC_X;ACC_Y;ACC_Z;GYRO_X;GYRO_Y;GYRO_Z\r\n";
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
void SDLogger_Start(void)
{
    head=tail=count=sequence=0; sd_log_written=sd_log_dropped=0;
    file_number=rotation_records=0; rotation_pending=0;
    stop_requested=opened=0; sd_log_error=0; sd_log_state=LOG_OFF; sd_log_filename[0]='\0';
    FRESULT r=f_mount(&fs,"",1);
    if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
    open_next_file();
    if (sd_log_state==LOG_OFF && opened) sd_log_state=LOG_RECORDING;
}
void SDLogger_RequestStop(void) { stop_requested=1; }
void SDLogger_DataLoss(void)
{
    if (sd_log_state==LOG_RECORDING) { ++sd_log_dropped; fail(FR_INT_ERR,LOG_DATA_LOSS); }
}
void SDLogger_Append(const LSM6DS3_Sample *sample,LogTime time)
{
    if (sd_log_state!=LOG_RECORDING || stop_requested) return;
    ++sequence;
    if (count==CAPACITY) { ++sd_log_dropped; fail(FR_INT_ERR,LOG_DATA_LOSS); return; }
    queue[head].sample=*sample; queue[head].time=time; queue[head].number=sequence;
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
    char date[16]="",time[24]="",value[6][20];
    if (r->time.valid) {
        (void)snprintf(date,sizeof(date),"%04u-%02u-%02u",r->time.year,r->time.month,r->time.day);
        (void)snprintf(time,sizeof(time),"%02u:%02u:%02u.%03u",r->time.hour,r->time.minute,r->time.second,r->time.millisecond);
    }
    fixed(value[0],r->sample.ax,1000000,6); fixed(value[1],r->sample.ay,1000000,6);
    fixed(value[2],r->sample.az,1000000,6); fixed(value[3],r->sample.gx,1000,3);
    fixed(value[4],r->sample.gy,1000,3); fixed(value[5],r->sample.gz,1000,3);
    return (unsigned)snprintf(out,192,"%lu;%s;%s;%s;%s;%s;%s;%s;%s\r\n",
        (unsigned long)r->number,date,time,value[0],value[1],value[2],value[3],value[4],value[5]);
}
void SDLogger_Process(uint32_t now)
{
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
    if (opened && (sd_log_state!=LOG_RECORDING || (stop_requested && count==0))) {
        close_file();
        if (sd_log_state==LOG_RECORDING) sd_log_state=LOG_STOPPED;
    }
}
