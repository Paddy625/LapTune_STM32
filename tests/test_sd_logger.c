#include "sd_logger.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SECTORS 32768U
static unsigned char *image;
static uint32_t tick;
static int no_card,write_error,inject;
uint32_t HAL_GetTick(void) { return tick; }
DSTATUS disk_initialize(BYTE p) { return p || no_card ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE p) { return disk_initialize(p); }
DRESULT disk_read(BYTE p,BYTE *out,LBA_t at,UINT n)
{
    assert(!p && at<SECTORS && n<=SECTORS-at); memcpy(out,image+at*512,n*512); return RES_OK;
}
DRESULT disk_write(BYTE p,const BYTE *in,LBA_t at,UINT n)
{
    assert(!p && at<SECTORS && n<=SECTORS-at);
    if (write_error) return RES_ERROR;
    memcpy(image+at*512,in,n*512);
    if (inject) {
        inject=0; LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0}; SDLogger_Append(&s,t);
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *out)
{
    assert(!p);
    if (cmd==CTRL_SYNC) return write_error ? RES_ERROR : RES_OK;
    if (cmd==GET_SECTOR_COUNT) *(LBA_t*)out=SECTORS;
    else if (cmd==GET_BLOCK_SIZE) *(DWORD*)out=1;
    else if (cmd==GET_SECTOR_SIZE) *(WORD*)out=512;
    else return RES_PARERR;
    return RES_OK;
}
int main(void)
{
    image=calloc(SECTORS,512); assert(image);
    MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=1,.align=1,.n_root=512,.au_size=512};
    unsigned char work[4096]; assert(f_mkfs("",&options,work,sizeof(work))==FR_OK);
    FATFS fs; FIL f; UINT done;
    assert(f_mount(&fs,"",1)==FR_OK);
    assert(f_open(&f,"LOG0001.TXT",FA_WRITE|FA_CREATE_NEW)==FR_OK);
    assert(f_write(&f,"preserve",8,&done)==FR_OK && done==8); assert(f_close(&f)==FR_OK);
    SDLogger_Start(); assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0002.TXT"));
    LSM6DS3_Sample s={0.000488f,-1,16,0.07f,-70,2000};
    LogTime t={.year=2026,.month=10,.day=26,.hour=12,.minute=34,.second=56,.millisecond=123,.valid=1};
    for (unsigned n=0;n<100;++n) SDLogger_Append(&s,t);
    inject=1;
    for (unsigned n=0;n<100 && sd_log_written<101;++n) SDLogger_Process(tick+=20);
    assert(sd_log_written==101 && sd_log_state==LOG_RECORDING);
    SDLogger_RequestStop(); SDLogger_Process(tick);
    assert(sd_log_state==LOG_STOPPED);
    assert(f_open(&f,"LOG0002.TXT",FA_READ)==FR_OK);
    char *text=calloc(20000,1); assert(text);
    assert(f_read(&f,text,19999,&done)==FR_OK && done>8000); assert(f_close(&f)==FR_OK);
    assert(!strncmp(text,"NR;DATA;CZAS;ACC_X;ACC_Y;ACC_Z;GYRO_X;GYRO_Y;GYRO_Z\r\n",49));
    assert(strstr(text,"1;2026-10-26;12:34:56.123;0.000488;-1.000000;16.000000;0.070;-70.000;2000.000\r\n"));
    assert(strstr(text,"101;;;1.000000;2.000000;3.000000;4.000;5.000;6.000\r\n"));
    unsigned lines=0;
    for (char *p=text;*p;) {
        char *end=strstr(p,"\r\n"); assert(end); unsigned columns=0;
        for (char *q=p;q<end;++q) if (*q==';') ++columns;
        assert(columns==8); ++lines; p=end+2;
    }
    assert(lines==102);
    assert(f_open(&f,"LOG0001.TXT",FA_READ)==FR_OK); char old[9]={0};
    assert(f_read(&f,old,8,&done)==FR_OK && !strcmp(old,"preserve")); assert(f_close(&f)==FR_OK);
    SDLogger_Start(); assert(!strcmp(sd_log_filename,"LOG0003.TXT"));
    for (unsigned n=0;n<129;++n) SDLogger_Append(&s,t);
    assert(sd_log_state==LOG_DATA_LOSS && sd_log_dropped); SDLogger_Process(tick);
    SDLogger_Start(); SDLogger_Append(&s,t); write_error=1;
    SDLogger_Process(tick); SDLogger_RequestStop(); SDLogger_Process(tick);
    assert(sd_log_state==LOG_CARD_ERROR);
    write_error=0; no_card=1; SDLogger_Start(); assert(sd_log_state==LOG_CARD_ERROR);
    no_card=0; assert(f_mount(&fs,"",1)==FR_OK);
    DWORD free_clusters; FATFS *volume;
    assert(f_getfree("",&free_clusters,&volume)==FR_OK);
    assert(f_open(&f,"FILL.BIN",FA_WRITE|FA_CREATE_NEW)==FR_OK);
    assert(f_lseek(&f,(FSIZE_t)free_clusters*volume->csize*512U)==FR_OK);
    assert(f_close(&f)==FR_OK);
    SDLogger_Start(); assert(sd_log_state==LOG_FULL); SDLogger_Process(tick);
    free(text); free(image);
    puts("PASS SD logger/FatFs: real FAT image, 9 columns, UTC/units, unique files, concurrent enqueue, close, overflow and I/O errors");
}
