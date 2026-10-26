#include "sd_logger.h"
#include "../FatFs/ff.h"
#include <stdio.h>
#include <string.h>

#define CAPACITY 128U
static struct Record { LSM6DS3_Sample sample; LogTime time; uint32_t number; } queue[CAPACITY];
static unsigned head,tail,count;
static uint32_t sequence,last_sync;
static volatile uint8_t stop_requested;
static uint8_t opened;
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
    sd_log_error=result; sd_log_state=state; stop_requested=1;
}
void SDLogger_Start(void)
{
    head=tail=count=sequence=0; sd_log_written=sd_log_dropped=0;
    stop_requested=opened=0; sd_log_error=0; sd_log_state=LOG_OFF; sd_log_filename[0]='\0';
    FRESULT r=f_mount(&fs,"",1);
    if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
    unsigned n;
    for (n=1;n<=9999;++n) {
        (void)snprintf(sd_log_filename,sizeof(sd_log_filename),"LOG%04u.TXT",n);
        r=f_open(&file,sd_log_filename,FA_WRITE|FA_CREATE_NEW);
        if (r!=FR_EXIST) break;
    }
    if (r!=FR_OK) { fail(r,LOG_CARD_ERROR); return; }
    opened=1;
    static const char header[]="NR;DATA;CZAS;ACC_X;ACC_Y;ACC_Z;GYRO_X;GYRO_Y;GYRO_Z\r\n";
    UINT wrote=0;
    r=f_write(&file,header,sizeof(header)-1,&wrote);
    if (r==FR_OK && wrote!=sizeof(header)-1) r=FR_DENIED;
    if (r==FR_OK) r=f_sync(&file);
    if (r!=FR_OK) { fail(r,r==FR_DENIED ? LOG_FULL : LOG_CARD_ERROR); return; }
    last_sync=HAL_GetTick(); sd_log_state=LOG_RECORDING;
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
    if (sd_log_state==LOG_RECORDING && count) {
        unsigned bytes=0,records=0,available=count;
        while (records<available) {
            char line[192]; unsigned len=format(line,&queue[(tail+records)%CAPACITY]);
            if (len>=sizeof(line)) { fail(FR_INT_ERR,LOG_CARD_ERROR); break; }
            if (len>sizeof(batch)-bytes) break;
            memcpy(batch+bytes,line,len); bytes+=len; ++records;
        }
        if (sd_log_state!=LOG_RECORDING) return;
        UINT wrote=0;
        FRESULT r=f_write(&file,batch,bytes,&wrote);
        if (r!=FR_OK || wrote!=bytes) { fail(r==FR_OK ? FR_DENIED : r,r==FR_OK ? LOG_FULL : LOG_CARD_ERROR); }
        else {
            tail=(tail+records)%CAPACITY; count-=records; sd_log_written+=records;
        }
    }
    if (sd_log_state==LOG_RECORDING && (uint32_t)(now-last_sync)>=1000U) {
        FRESULT r=f_sync(&file); last_sync=now;
        if (r!=FR_OK) fail(r,LOG_CARD_ERROR);
    }
    if (opened && (sd_log_state!=LOG_RECORDING || (stop_requested && count==0))) {
        FRESULT r=f_close(&file); opened=0;
        if (r!=FR_OK) fail(r,LOG_CARD_ERROR);
        else if (sd_log_state==LOG_RECORDING) sd_log_state=LOG_STOPPED;
    }
}
