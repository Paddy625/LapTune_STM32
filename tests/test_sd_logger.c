#include "sd_logger.h"
#include "sd_spi.h"
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
static unsigned sync_calls,init_calls;
static DSTATUS medium_status=STA_NOINIT;
static void (*on_sync)(void),(*on_init)(void);
volatile SD_SPI_Diagnostics sd_spi_diag;
/* The footer test injects a CMD0 failure; card protocol is tested separately. */
const char *SD_SPI_ErrorName(void) { assert(sd_spi_diag.error==SD_SPI_CMD0); return "CMD0"; }
uint32_t HAL_GetTick(void) { return tick; }
DSTATUS disk_initialize(BYTE p)
{
    ++init_calls; medium_status=p || no_card ? STA_NOINIT : 0;
    if (on_init) { void (*action)(void)=on_init; on_init=NULL; action(); }
    return medium_status;
}
DSTATUS disk_status(BYTE p) { return p ? STA_NOINIT : medium_status; }
DRESULT disk_read(BYTE p,BYTE *out,LBA_t at,UINT n)
{
    assert(!p && at<SECTORS && n<=SECTORS-at);
    if (no_card) { medium_status=STA_NOINIT; return RES_ERROR; }
    memcpy(out,image+at*512,n*512); return RES_OK;
}
DRESULT disk_write(BYTE p,const BYTE *in,LBA_t at,UINT n)
{
    assert(!p && at<SECTORS && n<=SECTORS-at);
    if (write_error || no_card) { medium_status=STA_NOINIT; return RES_ERROR; }
    memcpy(image+at*512,in,n*512);
    if (inject) {
        inject=0; LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0}; SDLogger_Append(&s,t);
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *out)
{
    assert(!p);
    if (cmd==CTRL_SYNC) {
        ++sync_calls;
        if (on_sync) { void (*action)(void)=on_sync; on_sync=NULL; action(); }
        if (write_error || no_card) { medium_status=STA_NOINIT; return RES_ERROR; }
        return RES_OK;
    }
    if (cmd==GET_SECTOR_COUNT) *(LBA_t*)out=SECTORS;
    else if (cmd==GET_BLOCK_SIZE) *(DWORD*)out=1;
    else if (cmd==GET_SECTOR_SIZE) *(WORD*)out=512;
    else return RES_PARERR;
    return RES_OK;
}
static void fresh_card(uint32_t start)
{
    no_card=write_error=inject=0; sync_calls=0; on_sync=on_init=NULL; tick=start;
    sd_spi_diag=(SD_SPI_Diagnostics){0};
    assert(f_mount(NULL,"",0)==FR_OK);
    memset(image,0,SECTORS*512U);
    MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=1,.align=1,.n_root=512,.au_size=512};
    unsigned char work[4096]; assert(f_mkfs("",&options,work,sizeof(work))==FR_OK);
}
static char *read_log(const char *name,unsigned rows)
{
    FIL f; UINT done;
    assert(f_open(&f,name,FA_READ)==FR_OK);
    UINT bytes=(UINT)f_size(&f);
    char *text=calloc(bytes+1U,1); assert(text);
    assert(f_read(&f,text,bytes,&done)==FR_OK && done==bytes);
    assert(f_close(&f)==FR_OK);
    unsigned lines=0;
    for (char *p=text;*p;) {
        char *end=strstr(p,"\r\n"); assert(end);
        unsigned columns=0;
        for (char *q=p;q<end;++q) if (*q==';') ++columns;
        assert(columns==8); ++lines; p=end+2;
    }
    assert(lines==rows+1U);
    return text;
}
static void test_automatic_rotation(void)
{
    fresh_card(5000); SDLogger_Start();
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0001.TXT"));
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0};
    for (unsigned n=0;n<100;++n) SDLogger_Append(&s,t);
    tick+=SD_LOG_FILE_DURATION_MS-1U; SDLogger_Process(tick);
    assert(!strcmp(sd_log_filename,"LOG0001.TXT"));
    /* The boundary spans multiple writes; a callback enqueues into the next file. */
    inject=1; ++tick;
    for (unsigned n=0;n<100 && !strcmp(sd_log_filename,"LOG0001.TXT");++n) SDLogger_Process(tick);
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0002.TXT"));
    assert(sd_log_written==100 && !sd_log_dropped);
    char *first=read_log("LOG0001.TXT",100);
    assert(strstr(first,"100;;;1.000000;2.000000;3.000000;4.000;5.000;6.000\r\n"));
    assert(!strstr(first,"101;;;"));
    uint32_t second_start=tick;
    SDLogger_Process(tick); assert(sd_log_written==101);
    tick+=SD_LOG_SYNC_INTERVAL_MS; SDLogger_Process(tick);
    char *second=read_log("LOG0002.TXT",1); assert(strstr(second,"101;;;")); free(second);
    tick=second_start+SD_LOG_FILE_DURATION_MS-1U; SDLogger_Process(tick);
    assert(!strcmp(sd_log_filename,"LOG0002.TXT"));
    ++tick; SDLogger_Process(tick); /* Even an empty queue rotates on time, without B1. */
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0003.TXT"));
    char *preserved=read_log("LOG0001.TXT",100); assert(!strcmp(first,preserved));
    free(first); free(preserved);
    SDLogger_RequestStop(); SDLogger_Process(tick); assert(sd_log_state==LOG_STOPPED);
}
static void append_and_stop(void)
{
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0};
    SDLogger_Append(&s,t); SDLogger_RequestStop();
}
static void test_stop_and_tick_wrap(void)
{
    fresh_card(UINT32_MAX-50000U); SDLogger_Start();
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0};
    tick+=SD_LOG_FILE_DURATION_MS-1U; SDLogger_Process(tick);
    assert(!strcmp(sd_log_filename,"LOG0001.TXT"));
    ++tick; SDLogger_Append(&s,t); on_sync=append_and_stop; SDLogger_Process(tick);
    /* Stop during close must preserve a sample that was already queued for file 2. */
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0002.TXT"));
    SDLogger_Process(tick); assert(sd_log_state==LOG_STOPPED && sd_log_written==2);
    char *text=read_log("LOG0001.TXT",1); assert(strstr(text,"1;;;")); free(text);
    text=read_log("LOG0002.TXT",1); assert(strstr(text,"2;;;")); free(text);

    fresh_card(0); SDLogger_Start();
    for (unsigned n=0;n<100;++n) SDLogger_Append(&s,t);
    tick=SD_LOG_FILE_DURATION_MS; SDLogger_Process(tick); SDLogger_RequestStop();
    for (unsigned n=0;n<100 && sd_log_state==LOG_RECORDING;++n) SDLogger_Process(tick);
    assert(sd_log_state==LOG_STOPPED && sd_log_written==100);
    assert(!strcmp(sd_log_filename,"LOG0001.TXT")); /* Stop cancels a pending rotation. */
    free(read_log("LOG0001.TXT",100));
}
static void remove_card(void) { no_card=1; }
static void test_rotation_error(void)
{
    fresh_card(0); SDLogger_Start();
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0}; SDLogger_Append(&s,t);
    tick=SD_LOG_FILE_DURATION_MS; on_sync=remove_card; SDLogger_Process(tick);
    assert(sd_log_state==LOG_CARD_ERROR && sd_log_written==1);
    assert(!strcmp(sd_log_filename,"LOG0001.TXT"));
    no_card=0; FATFS reader; assert(f_mount(&reader,"",1)==FR_OK);
    free(read_log("LOG0001.TXT",1));
    assert(f_mount(NULL,"",0)==FR_OK);
}
static void test_sync_and_restart(void)
{
    fresh_card(0); SDLogger_Start();
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0}; SDLogger_Append(&s,t);
    unsigned before=sync_calls; SDLogger_Process(tick);
    tick=SD_LOG_SYNC_INTERVAL_MS-1U; SDLogger_Process(tick); assert(sync_calls==before);
    ++tick; SDLogger_Process(tick); assert(sync_calls==before+1U);
    SDLogger_Append(&s,t); ++tick; SDLogger_Process(tick);
    /* Simulate losing the RAM cache: unmount without f_close, then reboot/mount.
       This checks FatFs metadata persistence, not electrical power-fail behaviour. */
    assert(f_mount(NULL,"",0)==FR_OK); SDLogger_Start();
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0002.TXT"));
    char *text=read_log("LOG0001.TXT",1);
    assert(strstr(text,"1;;;") && !strstr(text,"2;;;")); free(text);
    SDLogger_RequestStop(); SDLogger_Process(tick); assert(sd_log_state==LOG_STOPPED);
}
static void test_error_messages(void)
{
    fresh_card(0); SDLogger_Start(); char text[26]; SDLogger_StatusText(text);
    assert(strlen(text)==25 && strstr(text,"SD REC") && strstr(text,"LOG0001.TXT"));
    SDLogger_RequestStop(); SDLogger_Process(tick); SDLogger_StatusText(text);
    assert(strlen(text)==25 && strstr(text,"SD SAVED"));
    no_card=1; SDLogger_Start(); SDLogger_StatusText(text);
    assert(strstr(text,"SD E03 NOT READY"));
    sd_spi_diag=(SD_SPI_Diagnostics){.error=SD_SPI_CMD0,.command=0,.response=0xFF};
    SDLogger_StatusText(text); assert(strlen(text)==25 && strstr(text,"SD E03 CMD0 00:FF"));
    sd_spi_diag=(SD_SPI_Diagnostics){.error=SD_SPI_OCR,.ocr_valid=1,.ocr=0x00FF8000};
    SDLogger_StatusText(text); assert(strlen(text)==25 && strstr(text,"SD E03 OCR 00FF8000"));
    sd_spi_diag=(SD_SPI_Diagnostics){0}; no_card=0;
    assert(f_mount(NULL,"",0)==FR_OK); memset(image,0,SECTORS*512U);
    SDLogger_Start(); assert(sd_log_error==FR_NO_FILESYSTEM); SDLogger_StatusText(text);
    assert(strlen(text)==25 && strstr(text,"SD E13 FAT16/32"));
}
static void test_auto_retry_and_reinsert(void)
{
    fresh_card(0); no_card=1; SDLogger_Start(); assert(sd_log_state==LOG_CARD_ERROR);
    unsigned attempts=init_calls;
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0}; SDLogger_Append(&s,t);
    tick=SD_LOG_RETRY_INTERVAL_MS-1U; SDLogger_Process(tick); assert(init_calls==attempts);
    ++tick; SDLogger_Process(tick); assert(init_calls==attempts+1U && sd_log_state==LOG_CARD_ERROR);
    no_card=0; tick+=SD_LOG_RETRY_INTERVAL_MS-1U; SDLogger_Process(tick); assert(init_calls==attempts+1U);
    ++tick; SDLogger_Process(tick); assert(sd_log_state==LOG_RECORDING && init_calls==attempts+2U);
    assert(!strcmp(sd_log_filename,"LOG0001.TXT"));
    SDLogger_Append(&s,t); SDLogger_Process(tick);
    tick+=SD_LOG_SYNC_INTERVAL_MS; SDLogger_Process(tick); /* Persist sample 2. */
    SDLogger_Append(&s,t); no_card=1;
    tick+=SD_LOG_SYNC_INTERVAL_MS; SDLogger_Process(tick);
    assert(sd_log_state==LOG_CARD_ERROR);
    SDLogger_Append(&s,t); /* Sample 4 arrives while card is absent. */
    no_card=0; tick+=SD_LOG_RETRY_INTERVAL_MS; SDLogger_Process(tick);
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0002.TXT"));
    SDLogger_Append(&s,t); SDLogger_Process(tick); SDLogger_RequestStop(); SDLogger_Process(tick);
    assert(sd_log_state==LOG_STOPPED && sd_log_dropped>=2);
    char *text=read_log("LOG0001.TXT",1); assert(strstr(text,"2;;;")); free(text);
    text=read_log("LOG0002.TXT",1); assert(strstr(text,"5;;;") && !strstr(text,"4;;;")); free(text);
}
static void test_retry_different_card(void)
{
    fresh_card(0); SDLogger_Start();
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0};
    SDLogger_Append(&s,t); SDLogger_Process(tick);
    tick+=SD_LOG_SYNC_INTERVAL_MS; SDLogger_Process(tick);
    SDLogger_Append(&s,t); no_card=1;
    tick+=SD_LOG_SYNC_INTERVAL_MS; SDLogger_Process(tick); assert(sd_log_state==LOG_CARD_ERROR);
    unsigned char *first_card=image;
    image=calloc(SECTORS,512); assert(image); fresh_card(tick);
    FATFS prepared; FIL f; UINT done;
    assert(f_mount(&prepared,"",1)==FR_OK);
    assert(f_open(&f,"LOG0001.TXT",FA_WRITE|FA_CREATE_NEW)==FR_OK);
    assert(f_write(&f,"second card",11,&done)==FR_OK && done==11); assert(f_close(&f)==FR_OK);
    tick+=SD_LOG_RETRY_INTERVAL_MS; SDLogger_Process(tick);
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0002.TXT"));
    SDLogger_Append(&s,t); SDLogger_Process(tick); SDLogger_RequestStop(); SDLogger_Process(tick);
    char *text=read_log("LOG0002.TXT",1); assert(strstr(text,"3;;;") && !strstr(text,"2;;;")); free(text);
    assert(f_open(&f,"LOG0001.TXT",FA_READ)==FR_OK); char marker[12]={0};
    assert(f_read(&f,marker,11,&done)==FR_OK && !strcmp(marker,"second card")); assert(f_close(&f)==FR_OK);
    assert(f_mount(NULL,"",0)==FR_OK); free(image); image=first_card;
}
static void test_button_pause_resume(void)
{
    fresh_card(0); SDLogger_Start(); SDLogger_Button(); SDLogger_Process(tick);
    assert(sd_log_state==LOG_STOPPED);
    unsigned attempts=init_calls;
    tick+=SD_LOG_RETRY_INTERVAL_MS*2U; SDLogger_Process(tick);
    assert(sd_log_state==LOG_STOPPED && init_calls==attempts); /* Manual stop stays stopped. */
    SDLogger_Button(); tick+=10; SDLogger_Button(); SDLogger_Process(tick);
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0002.TXT"));
    tick+=250; SDLogger_Button(); SDLogger_Process(tick); assert(sd_log_state==LOG_STOPPED);
    no_card=1; tick+=SD_LOG_RETRY_INTERVAL_MS*2U; SDLogger_Process(tick); assert(sd_log_state==LOG_STOPPED);
    no_card=0; SDLogger_Button(); SDLogger_Process(tick);
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0003.TXT"));
    SDLogger_RequestStop(); SDLogger_Process(tick);

    fresh_card(0); no_card=1; SDLogger_Start(); SDLogger_Button();
    char text[26]; SDLogger_StatusText(text); assert(strstr(text,"PAUSE E03"));
    attempts=init_calls; no_card=0; tick+=SD_LOG_RETRY_INTERVAL_MS*2U; SDLogger_Process(tick);
    assert(init_calls==attempts); SDLogger_Button(); SDLogger_Process(tick); assert(sd_log_state==LOG_RECORDING);
    SDLogger_RequestStop(); SDLogger_Process(tick);

    fresh_card(0); no_card=1; SDLogger_Start(); no_card=0; on_init=SDLogger_RequestStop;
    tick+=SD_LOG_RETRY_INTERVAL_MS; SDLogger_Process(tick); /* B1 during a mount. */
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0}; SDLogger_Append(&s,t);
    SDLogger_Process(tick); assert(sd_log_state==LOG_STOPPED && !sd_log_written);
    free(read_log("LOG0001.TXT",0));
}
static void delay_sync(void) { tick+=500; }
static void test_retry_wrap_and_acquisition_fault(void)
{
    fresh_card(UINT32_MAX-1000U); no_card=1; SDLogger_Start(); unsigned attempts=init_calls;
    tick+=SD_LOG_RETRY_INTERVAL_MS-1U; SDLogger_Process(tick); assert(init_calls==attempts);
    no_card=0; ++tick; SDLogger_Process(tick); assert(sd_log_state==LOG_RECORDING && init_calls==attempts+1U);
    SDLogger_DataLoss(); SDLogger_Process(tick); assert(sd_log_state==LOG_DATA_LOSS);
    attempts=init_calls; tick+=SD_LOG_RETRY_INTERVAL_MS; SDLogger_Process(tick); assert(init_calls==attempts);
    LSM6DS3_Sample s={1,2,3,4,5,6}; LogTime t={0}; SDLogger_Append(&s,t);
    SDLogger_Process(tick); assert(sd_log_state==LOG_RECORDING && init_calls==attempts+1U);
    SDLogger_Append(&s,t); SDLogger_Process(tick);
    SDLogger_DataLoss(); SDLogger_Append(&s,t); on_sync=delay_sync;
    attempts=init_calls; uint32_t before=tick;
    SDLogger_Process(tick); assert(tick==before+500U && init_calls==attempts);
    tick+=SD_LOG_RETRY_INTERVAL_MS-1U; SDLogger_Process(tick); assert(init_calls==attempts);
    ++tick; SDLogger_Process(tick); assert(sd_log_state==LOG_RECORDING && init_calls==attempts+1U);
    SDLogger_RequestStop(); SDLogger_Process(tick);
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
    assert(sd_log_error==FR_DISK_ERR); /* Preserve the first failure through f_close. */
    write_error=0; no_card=1; SDLogger_Start(); assert(sd_log_state==LOG_CARD_ERROR);
    no_card=0; assert(f_mount(&fs,"",1)==FR_OK);
    DWORD free_clusters; FATFS *volume;
    assert(f_getfree("",&free_clusters,&volume)==FR_OK);
    assert(f_open(&f,"FILL.BIN",FA_WRITE|FA_CREATE_NEW)==FR_OK);
    assert(f_lseek(&f,(FSIZE_t)free_clusters*volume->csize*512U)==FR_OK);
    assert(f_close(&f)==FR_OK);
    SDLogger_Start(); assert(sd_log_state==LOG_FULL); SDLogger_Process(tick);
    for (unsigned n=0;n<3;++n) {
        tick+=SD_LOG_RETRY_INTERVAL_MS; SDLogger_Process(tick);
        assert(sd_log_state==LOG_FULL && !sd_log_filename[0]); /* No empty file per retry. */
    }
    free(text);
    test_automatic_rotation(); test_stop_and_tick_wrap(); test_rotation_error(); test_sync_and_restart(); test_error_messages();
    test_auto_retry_and_reinsert(); test_retry_different_card(); test_button_pause_resume(); test_retry_wrap_and_acquisition_fault();
    free(image);
    puts("PASS SD logger/FatFs: FAT/UTC, rotation, 2s retries, absent/reinserted/different card, gaps, B1 pause/resume/debounce, full card, fault recovery and wrap");
}
