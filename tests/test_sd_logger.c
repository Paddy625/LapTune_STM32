#include "sd_logger.h"
#include "sd_spi.h"
#include "gps.h"
#include "gps_time.h"
#include "session.h"
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
#ifndef LAPTUNE_PIPELINE_TEST
volatile ImuDiagnostics imu_diag;
uint8_t lsm6ds3_registers[16],lsm6ds3_config_verified;
#endif
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
static void (*on_sync)(void),(*on_init)(void),(*on_write)(void);
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
    if (on_write) { void (*action)(void)=on_write; on_write=NULL; action(); }
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
    no_card=write_error=0; sync_calls=0; on_sync=on_init=on_write=NULL; tick=start;
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
        assert(columns==36); ++lines; p=end+2;
    }
    assert(lines==rows+1); return text;
}
static ImuRecord record(uint32_t nr,uint64_t t)
{
    return (ImuRecord){.nr=nr,.t_us=t,.acc_minus_gyro_us=-75,.raw={2048,-2048,1,1000,-1000,1}};
}
static uint8_t append_raw(const ImuRecord *r,LogTime t)
{ if (r->nr>imu_diag.acquired) imu_diag.acquired=r->nr; return SDLogger_AppendRaw(r,t); }
static void append(uint32_t nr) { ImuRecord r=record(nr,mono); append_raw(&r,(LogTime){0}); }
static void stop(void)
{
    SDLogger_RequestStop();
    for (unsigned i=0;i<200 && sd_log_state==LOG_RECORDING;++i) { SDLogger_ConsumeImu(); SDLogger_Process(tick); advance(10); }
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
    ImuRecord r=record(3,t); append_raw(&r,utc);
    r=record(4,t+9615); append_raw(&r,utc);
    nav(1200,t+200000,7,3); r=record(5,t+200000); append_raw(&r,utc);
    nav(1400,t+400000,5,2); GPS_Epoch_Service(&gps_data,t+580000);
    r=record(6,t+580000); append_raw(&r,utc);
    r=record(7,t+1380000); r.flags=IMU_ERR_SPI; append_raw(&r,(LogTime){0}); stop();
    assert(sd_log_written==6 && !sd_log_dropped && sd_max_pause_us);
    char *text=read_log("LOG0001.TXT",6);
    assert(strstr(text,"3;2026-11-01;12:13:14.123;0.999424;-0.999424;0.000488;70.000000;-70.000000;0.070000;52.1234567;21.5678901;44.424;123.456;7200000001;N;1;1000;1;0;7199999926;7200000001;"));
    assert(strstr(text,"7200009616;R;1;1000;"));
    assert(strstr(text,"7200200001;N;2;1200;"));
    assert(strstr(text,";52.1234567;21.5678901;;;7200580001;N;3;1400;"));
    assert(strstr(text,"7;;;;;;;;;;;;;7201380001;I;3;1400;0;1;"));
    char *json=read_file("SESSION.json");
    assert(strstr(json,"\"format_version\": 4") && strstr(json,"test\\\"firmware\\\\7.03"));
    const char *export_dir=getenv("P0_ARTIFACT_DIR");
    if (export_dir) {
        char path[512]; snprintf(path,sizeof(path),"%s/LOG0001.TXT",export_dir);
        FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(text,1,strlen(text),f)==strlen(text)); fclose(f);
        snprintf(path,sizeof(path),"%s/SESSION.json",export_dir); f=fopen(path,"wb"); assert(f);
        assert(fwrite(json,1,strlen(json),f)==strlen(json)); fclose(f);
    }
    free(json); free(text);
}
static void test_metadata_boundaries(void)
{
    fresh_card(0); SDLogger_Start(); append(1); SDLogger_Process(tick);
    advance(SD_LOG_FILE_DURATION_MS); SDLogger_Process(tick);
    assert(!strcmp(sd_log_filename,"LOG0002.TXT")); append(2); SDLogger_Process(tick);
    advance(SD_LOG_SNAPSHOT_INTERVAL_MS); SDLogger_Process(tick);
    char *json=read_file("SESSION.json");
    assert(strstr(json,"\"last_written_nr\": 2") && strstr(json,"\"last_synced_nr\": 2") && strstr(json,"\"clean_shutdown\": false"));
    free(json); append(3); stop(); json=read_file("SESSION.json");
    assert(strstr(json,"\"last_written_nr\": 3") && strstr(json,"\"last_synced_nr\": 3") && strstr(json,"\"clean_shutdown\": true"));
    free(json); json=read_file("SESSION.bak");
    assert(strstr(json,"\"last_written_nr\": 2") && strstr(json,"\"clean_shutdown\": false")); free(json);
}
static void test_sync_not_retroactive(void)
{
    fresh_card(0); SDLogger_Start(); append(1); SDLogger_Process(tick);
    assert(sd_log_written==1 && sd_diag.synced_records==0);
    write_error=1; advance(1001); SDLogger_Process(tick); assert(sd_log_state==LOG_CARD_ERROR);
    write_error=0; advance(SD_LOG_RETRY_INTERVAL_MS); SDLogger_Process(tick);
    assert(SDLogger_Ready() && !sd_diag.synced_records && !sd_diag.last_synced_nr);
    append(2); stop(); assert(sd_log_written==2 && sd_diag.synced_records==1 && sd_diag.last_synced_nr==2);
}
static void test_integer_limits(void)
{
    fresh_card(0); SDLogger_Start();
    ImuRecord r=record(UINT32_MAX,UINT64_MAX); r.acc_minus_gyro_us=75;
    append_raw(&r,(LogTime){0}); stop();
    char *text=read_log("LOG0001.TXT",1);
    assert(strstr(text,"4294967295;;;"));
    assert(strstr(text,";18446744073709551615;I;;;1;0;18446744073709551615;18446744073709551540;"));
    free(text);
}
/* A synchronous disk write may service GPS/IMU, but must not corrupt the
   frozen metadata or the next CSV batch that shares its RAM workspace. */
static void update_during_metadata(void)
{ imu_diag.acquired=999; gps_diag.uart_errors=88; config.attempts=42; }
static void test_workspace_and_packing(void)
{
    fresh_card(0); GPS_Time_Init(); SDLogger_Start();
    LogTime utc={.year=2026,.month=11,.day=7,.hour=12,.minute=13,.second=14,.millisecond=999,.valid=1};
    uint64_t t=7200000001ULL; nav(1000,t,7,3);
    for (unsigned n=1;n<=16;++n) {
        ImuRecord r=record(n,t+n*9615U);
        for (unsigned axis=0;axis<6;++axis) r.raw[axis]=axis&1 ? INT16_MIN : INT16_MAX;
        assert(append_raw(&r,utc));
    }
    SDLogger_Process(tick);
    assert(sd_diag.max_batch_records>=4 && sd_diag.max_batch_bytes<=1024);
    unsigned accepted=sd_log_written;
    uint32_t errors=gps_diag.uart_errors; uint8_t attempts=config.attempts;
    on_write=update_during_metadata;
    assert(Session_Write(sd_log_session_dir)==FR_OK && !on_write);
    char *json=read_file("SESSION.json");
    assert(strstr(json,"\"last_acquired_nr\": 16") && !strstr(json,"\"gps_uart_errors\": 88"));
    assert(!strstr(json,"\"gps_configuration_attempts\": 42")); free(json);
    gps_diag.uart_errors=errors; config.attempts=attempts;
    assert(sd_log_written==accepted); stop();
    char *text=read_log("LOG0001.TXT",16),*line=strstr(text,"\r\n")+2;
    for (unsigned n=1;n<=16;++n) {
        char *end; assert(strtoul(line,&end,10)==n && *end==';');
        assert(strstr(line,";15.990296;-15.990784;15.990296;-2293.760000;2293.690000;-2293.760000;"));
        line=strstr(line,"\r\n")+2;
    }
    assert(!*line); free(text);
    /* Missing UTC is counted once on acceptance, not again on batch retries. */
    fresh_card(0); GPS_Time_Init(); SDLogger_Start();
    for (unsigned n=1;n<=16;++n) append(n);
    ImuRecord waiting=record(17,mono);
    assert(!append_raw(&waiting,(LogTime){0}) && sd_diag.utc_missing_anchor==16);
    SDLogger_Process(tick); assert(append_raw(&waiting,(LogTime){0}));
    stop(); assert(sd_diag.utc_missing_anchor==17 && !sd_diag.utc_missing_pps && !sd_diag.utc_out_of_range);
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
    for (unsigned i=1;i<=16;++i) append(i);
    ImuRecord waiting=record(17,mono);
    assert(!SDLogger_AppendRaw(&waiting,(LogTime){0}));
    assert(sd_log_state==LOG_RECORDING && !sd_log_dropped);
    SDLogger_Process(tick); assert(SDLogger_AppendRaw(&waiting,(LogTime){0}));
    stop(); assert(sd_diag.queue_high_water==16 && !sd_log_dropped);
    char *text=read_log("LOG0001.TXT",17); assert(strstr(text,"17;;;")); free(text);
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
static void test_pps_columns(void)
{
    extern void PPSTest_Clear(void);
    extern void PPSTest_Capture(uint64_t us);
    fresh_card(0); SDLogger_Start(); PPSTest_Clear(); PPSTest_Capture(1000000);
    for (unsigned sec=2;sec<=4;++sec) {
        uint8_t tp[16]={0}; put32(tp,sec*1000); tp[12]=2400&255; tp[13]=2400>>8;
        GPS_Time_PulseMessage(tp,16,(sec-1)*1000000ULL+110000);
        GPS_Time_Navigation(2400,sec*1000-200,sec*1000000ULL-100000);
        PPSTest_Capture(sec*1000000ULL); GPS_Time_Service(sec*1000000ULL);
    }
    assert(GPS_Time_Diagnostics().state==1);
    uint8_t p[28],v[36],s[52],utc[20]={0}; nav_payloads(4200,p,v,s);
    s[8]=2400&255; s[9]=2400>>8;
    GPS_Epoch_Packet(&gps_data,1,2,p,28,4300000);
    GPS_Epoch_Packet(&gps_data,1,0x12,v,36,4310000);
    GPS_Epoch_Packet(&gps_data,1,6,s,52,4320000);
    put32(utc,4000); utc[12]=2026&255; utc[13]=2026>>8;
    utc[14]=11; utc[15]=3; utc[16]=12; utc[19]=7;
    GPS_Epoch_Packet(&gps_data,1,0x21,utc,20,4330000);
    mono=4900000; tick=4900;
    ImuRecord a=record(2,4200000),b=record(3,4209615);
    assert(append_raw(&a,LogClock_AtUs(a.t_us)) && append_raw(&b,LogClock_AtUs(b.t_us)));
    stop(); char footer[26]; SDLogger_StatusText(footer); assert(!strcmp(footer,"SD SAVED S0001"));
    char *text=read_log("LOG0001.TXT",2);
    assert(strstr(text,"2;2026-11-03;12:00:00.200;") && strstr(text,"3;2026-11-03;12:00:00.209;"));
    assert(strstr(text,";N;1;4200;") && strstr(text,";R;1;4200;"));
    char *times=strstr(text,";4320000;4200000;1\r\n");
    assert(times && strstr(times+1,";4320000;4200000;1\r\n")); free(text);
    char *json=read_file("SESSION.json");
    assert(strstr(json,"\"time_sync_state\": 1") && strstr(json,"\"pps_model_period_ticks\": 1000000"));
    assert(strstr(json,"\"gps_receiver_seen\": 3") && strstr(json,"\"gps_sol_fix_type\": 3") && strstr(json,"\"gps_sol_week\": 2400"));
    free(json); PPSTest_Clear();
}
static void test_nmea_columns(void)
{
    fresh_card(0); GPS_Epoch_InitNMEA(); SDLogger_Start();
    GPS_Data d={.position_valid=1,.altitude_valid=1,.speed_valid=1,
        .latitude_e7=521234567,.longitude_e7=215678901,.altitude_mm=123456,.speed_millikmh=44424};
    GPS_Epoch_NMEA(&d,"$GPGGA,120000,",1000000);
    GPS_Epoch_NMEA(&d,"$GPRMC,120000,",1100000);
    mono=1200000; tick=1200;
    ImuRecord a=record(1,1100000),b=record(2,1109615);
    assert(append_raw(&a,(LogTime){0}) && append_raw(&b,(LogTime){0}));
    stop(); char *text=read_log("LOG0001.TXT",2);
    assert(strstr(text,";52.1234567;21.5678901;44.424;123.456;1100000;N;1;;"));
    assert(strstr(text,";1109615;R;1;;") && strstr(text,";1100000;;0\r\n"));
    free(text);
}
int main(void)
{
    image=malloc(SECTORS*512U); assert(image);
    test_workspace_and_packing(); test_contract(); test_pps_columns(); test_metadata_boundaries(); test_sync_not_retroactive(); test_integer_limits(); test_rotation_and_pause(); test_retry_and_card_change(); test_overload_and_failures(); test_full_and_30_minutes(); test_nmea_columns();
    assert(f_mount(NULL,"",0)==FR_OK); free(image);
    puts("PASS SD/FatFs: 36-column contract, exact uint64/scales, GPS N/R/I, JSON/LFN, session separation, rotation/retry/removal/full/overload, 30 virtual minutes");
    return 0;
}
