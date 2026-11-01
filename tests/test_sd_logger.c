#include "sd_logger.h"
#include "sd_spi.h"
#include "gps.h"
#include "gps_fixtures.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SECTORS 131072U
static unsigned char *image;
static uint32_t tick;
static int no_card,write_error;
static uint64_t mono;
volatile ImuDiagnostics imu_diag;
uint8_t lsm6ds3_registers[16],lsm6ds3_config_verified;
static GPS_Data gps_data;
static GPS_ConfigStatus config={.state=GPS_CONFIG_READY,.baud=38400,.measurement_ms=200,.nav_rate=1,.time_ref=1};
const GPS_ConfigStatus *GPS_GetConfig(void) { return &config; }
const char *GPS_SoftwareVersion(void) { return "test\"firmware\\7.03"; }
const char *GPS_HardwareVersion(void) { return "00040007"; }
const char *BootIdentity_Directory(void) { return "S000000010000000200000003-0000000000000001"; }
uint64_t Timebase_NowUs(void) { return mono; }
static void advance(uint32_t ms) { tick+=ms; mono+=(uint64_t)ms*1000; }
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
    advance(n); /* Emulate media latency, keeping interrupts/time independent. */
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
    no_card=write_error=0; sync_calls=0; on_sync=on_init=NULL; tick=start;
    mono=(uint64_t)start*1000;
    GPS_Epoch_Init(); memset(&gps_data,0,sizeof(gps_data)); memset((void*)&imu_diag,0,sizeof(imu_diag));
    sd_spi_diag=(SD_SPI_Diagnostics){0};
    assert(f_mount(NULL,"",0)==FR_OK);
    memset(image,0,SECTORS*512U);
    MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=1,.align=1,.n_root=512,.au_size=2048};
    unsigned char work[4096]; assert(f_mkfs("",&options,work,sizeof(work))==FR_OK);
}
static char *read_file(const char *name)
{
    char path[96]; snprintf(path,sizeof(path),"%s/%s",sd_log_session_dir,name);
    FIL f; UINT done; assert(f_open(&f,path,FA_READ)==FR_OK);
    UINT bytes=(UINT)f_size(&f); char *text=calloc(bytes+1U,1); assert(text);
    assert(f_read(&f,text,bytes,&done)==FR_OK && done==bytes); assert(f_close(&f)==FR_OK); return text;
}
static char *read_log(const char *name,unsigned rows)
{
    char *text=read_file(name); unsigned lines=0;
    for (char *p=text;*p;) {
        char *end=strstr(p,"\r\n"); assert(end); unsigned columns=1;
        for (char *q=p;q<end;++q) if (*q==';') ++columns;
        assert(columns==33); ++lines; p=end+2;
    }
    assert(lines==rows+1); return text;
}
static ImuRecord record(uint32_t nr,uint64_t t)
{
    return (ImuRecord){.nr=nr,.t_us=t,.acc_minus_gyro_us=-75,.raw={2048,-2048,1,1000,-1000,1}};
}
static void append(uint32_t nr) { ImuRecord r=record(nr,mono); SDLogger_AppendRaw(&r,(LogTime){0}); }
static void stop(void)
{
    SDLogger_RequestStop();
    for (unsigned i=0;i<20 && sd_log_state==LOG_RECORDING;++i) SDLogger_Process(tick);
    assert(sd_log_state==LOG_STOPPED);
}
static void nav(uint32_t itow,uint64_t us,unsigned parts,unsigned type)
{
    uint8_t p[28],v[36],s[52]; nav_payloads(itow,p,v,s); s[10]=type;
    if (parts&1) GPS_Epoch_Packet(&gps_data,1,2,p,28,us);
    if (parts&2) GPS_Epoch_Packet(&gps_data,1,0x12,v,36,us);
    if (parts&4) GPS_Epoch_Packet(&gps_data,1,6,s,52,us);
}
static void test_contract(void)
{
    fresh_card(0); SDLogger_Start(); assert(sd_log_state==LOG_RECORDING);
    append(1); SDLogger_Process(tick);
    uint64_t t=7200000001ULL; nav(1000,t,7,3);
    LogTime utc={.year=2026,.month=11,.day=1,.hour=12,.minute=13,.second=14,.millisecond=123,.valid=1};
    ImuRecord r=record(3,t); SDLogger_AppendRaw(&r,utc);
    r=record(4,t+9615); SDLogger_AppendRaw(&r,utc);
    nav(1200,t+200000,7,3); r=record(5,t+200000); SDLogger_AppendRaw(&r,utc);
    nav(1400,t+400000,5,2); GPS_Epoch_Service(&gps_data,t+480000);
    r=record(6,t+480000); SDLogger_AppendRaw(&r,utc);
    r=record(7,t+1280000); r.flags=IMU_ERR_SPI; SDLogger_AppendRaw(&r,(LogTime){0}); stop();
    assert(sd_log_written==6 && !sd_log_dropped && sd_max_pause_us);
    char *text=read_log("LOG0001.TXT",6);
    assert(strstr(text,"3;2026-11-01;12:13:14.123;0.999424;-0.999424;0.000488;70.000000;-70.000000;0.070000;52.1234567;21.5678901;44.424;123.456;7200000001;N;1;1000;1;0;7199999926;7200000001;"));
    assert(strstr(text,"7200009616;R;1;1000;"));
    assert(strstr(text,"7200200001;N;2;1200;"));
    assert(strstr(text,";52.1234567;21.5678901;;;7200480001;N;3;1400;"));
    assert(strstr(text,"7;;;;;;;;;;;;;7201280001;I;;;0;1;"));
    char *json=read_file("SESSION.json");
    assert(strstr(json,"\"format_version\": 2") && strstr(json,"test\\\"firmware\\\\7.03"));
    const char *export_dir=getenv("P0_ARTIFACT_DIR");
    if (export_dir) {
        char path[512]; snprintf(path,sizeof(path),"%s/LOG0001.TXT",export_dir);
        FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(text,1,strlen(text),f)==strlen(text)); fclose(f);
        snprintf(path,sizeof(path),"%s/SESSION.json",export_dir); f=fopen(path,"wb"); assert(f);
        assert(fwrite(json,1,strlen(json),f)==strlen(json)); fclose(f);
    }
    free(json); free(text);
}
static void test_integer_limits(void)
{
    fresh_card(0); SDLogger_Start();
    ImuRecord r=record(UINT32_MAX,UINT64_MAX); r.acc_minus_gyro_us=75;
    SDLogger_AppendRaw(&r,(LogTime){0}); stop();
    char *text=read_log("LOG0001.TXT",1);
    assert(strstr(text,"4294967295;;;"));
    assert(strstr(text,";18446744073709551615;I;;;1;0;18446744073709551615;18446744073709551540;"));
    free(text);
}
static void test_rotation_and_pause(void)
{
    fresh_card(UINT32_MAX-10000U); SDLogger_Start(); char session[64]; strcpy(session,sd_log_session_dir);
    for (unsigned i=1;i<=16;++i) append(i);
    assert(!SDLogger_CanAccept()); advance(SD_LOG_FILE_DURATION_MS);
    for (unsigned n=0;n<20 && !strcmp(sd_log_filename,"LOG0001.TXT");++n) SDLogger_Process(tick);
    assert(!strcmp(sd_log_filename,"LOG0002.TXT") && sd_log_written==16 && !sd_log_dropped);
    append(17); SDLogger_Process(tick); SDLogger_Button(); SDLogger_Process(tick);
    assert(sd_log_state==LOG_STOPPED); SDLogger_Button(); SDLogger_Process(tick); assert(sd_log_state==LOG_STOPPED);
    advance(201); SDLogger_Button(); SDLogger_Process(tick);
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0003.TXT") && !strcmp(session,sd_log_session_dir));
    append(18); stop(); free(read_log("LOG0001.TXT",16)); free(read_log("LOG0002.TXT",1)); free(read_log("LOG0003.TXT",1));
    SDLogger_Start(); assert(strcmp(session,sd_log_session_dir)); stop(); /* New boot/collision cannot mix files. */
}
static void test_retry_and_card_change(void)
{
    fresh_card(0); no_card=1; SDLogger_Start(); assert(sd_log_state==LOG_CARD_ERROR);
    no_card=0; advance(SD_LOG_RETRY_INTERVAL_MS); SDLogger_Process(tick); assert(sd_log_state==LOG_RECORDING);
    append(1); SDLogger_Process(tick); advance(SD_LOG_SYNC_INTERVAL_MS); SDLogger_Process(tick);
    char session[64]; strcpy(session,sd_log_session_dir);
    no_card=1; append(2); advance(SD_LOG_SYNC_INTERVAL_MS); SDLogger_Process(tick); assert(sd_log_state==LOG_CARD_ERROR);
    no_card=0; advance(SD_LOG_RETRY_INTERVAL_MS); SDLogger_Process(tick);
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0002.TXT") && !strcmp(session,sd_log_session_dir));
    append(3); stop(); free(read_log("LOG0002.TXT",1));
    char path[96]; snprintf(path,sizeof(path),"%s/LOG0001.TXT",session); assert(f_unlink(path)==FR_OK);
    SDLogger_RequestResume(); SDLogger_Process(tick); assert(!strcmp(sd_log_filename,"LOG0003.TXT"));
    append(4); SDLogger_Process(tick); write_error=1; advance(SD_LOG_SYNC_INTERVAL_MS); SDLogger_Process(tick);
    assert(sd_log_state==LOG_CARD_ERROR); write_error=0;
    /* Replacement card is blank FAT. Old file/FAT cache must not be replayed. */
    memset(image,0,SECTORS*512U); MKFS_PARM options={.fmt=FM_FAT|FM_SFD,.n_fat=1,.align=1,.n_root=512,.au_size=2048};
    uint8_t work[4096]; assert(f_mkfs("",&options,work,sizeof(work))==FR_OK);
    advance(SD_LOG_RETRY_INTERVAL_MS); SDLogger_Process(tick);
    assert(sd_log_state==LOG_RECORDING && !strcmp(sd_log_filename,"LOG0004.TXT") && !strcmp(session,sd_log_session_dir));
    append(5); stop(); free(read_log("LOG0004.TXT",1));
}
static void test_overload_and_failures(void)
{
    fresh_card(0); SDLogger_Start();
    for (unsigned i=1;i<=17;++i) append(i);
    assert(sd_log_state==LOG_DATA_LOSS && sd_log_dropped==1); SDLogger_Process(tick); assert(sd_log_dropped==17);
    advance(SD_LOG_RETRY_INTERVAL_MS); SDLogger_Process(tick); assert(sd_log_state==LOG_RECORDING);
    append(18); stop(); char *text=read_log("LOG0002.TXT",1); assert(strstr(text,"18;;;")); free(text);
    SDLogger_RequestResume(); SDLogger_Process(tick); on_sync=SDLogger_RequestStop;
    advance(SD_LOG_FILE_DURATION_MS); SDLogger_Process(tick); assert(sd_log_state==LOG_STOPPED);
    SDLogger_RequestResume(); SDLogger_Process(tick); no_card=1; advance(SD_LOG_FILE_DURATION_MS); SDLogger_Process(tick);
    assert(sd_log_state==LOG_CARD_ERROR); SDLogger_RequestStop(); no_card=0;
    advance(SD_LOG_RETRY_INTERVAL_MS*2); SDLogger_Process(tick); assert(sd_log_state==LOG_CARD_ERROR);
}
static void test_full_and_30_minutes(void)
{
    fresh_card(0); SDLogger_Start();
    /* Virtual 30-minute stream with actual FatFs writes and 3-minute rotation. */
    uint32_t start=tick;
    for (uint32_t n=1;n<=187200;++n) {
        uint32_t deadline=start+(uint32_t)((uint64_t)n*1000U/104U);
        if ((int32_t)(deadline-tick)>0) advance(deadline-tick);
        append(n); SDLogger_Process(tick);
        assert(sd_log_state==LOG_RECORDING);
    }
    stop(); assert(sd_log_written==187200 && !sd_log_dropped && (uint32_t)(tick-start)>=1800000U);
    fresh_card(0); SDLogger_Start(); FIL filler; assert(f_open(&filler,"FILL.BIN",FA_WRITE|FA_CREATE_NEW)==FR_OK);
    char zeros[4096]={0}; UINT done;
    do { assert(f_write(&filler,zeros,sizeof(zeros),&done)==FR_OK); } while (done==sizeof(zeros));
    assert(f_close(&filler)==FR_OK);
    for (unsigned i=0;i<20 && sd_log_state==LOG_RECORDING;++i) { append(i+1); advance(1001); SDLogger_Process(tick); }
    assert(sd_log_state==LOG_FULL); SDLogger_Process(tick); advance(SD_LOG_RETRY_INTERVAL_MS); SDLogger_Process(tick);
    assert(sd_log_state==LOG_FULL);
}
int main(void)
{
    image=malloc(SECTORS*512U); assert(image);
    test_contract(); test_integer_limits(); test_rotation_and_pause(); test_retry_and_card_change(); test_overload_and_failures(); test_full_and_30_minutes();
    assert(f_mount(NULL,"",0)==FR_OK); free(image);
    puts("PASS SD/FatFs: 33-column contract, exact uint64/scales, GPS N/R/I, JSON/LFN, session separation, rotation/retry/removal/full/overload, 30 virtual minutes");
}
