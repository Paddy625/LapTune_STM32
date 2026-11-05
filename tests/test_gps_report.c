#include "gps_report.h"
#include "gps.h"
#include "sd_logger.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char contents[20000]; static unsigned length,saved,closes,opens;
static int ready=1,write_fail,close_fail;
static uint16_t raw_depth;
uint16_t ImuStream_Count(void) { return raw_depth; }
static GPS_QueryResult result; static uint8_t bytes[400]; static int pending;
char sd_log_session_dir[64]="S_TEST";
unsigned GPS_ConfigQueryCount(void) { return 90; }
uint8_t SDLogger_Ready(void) { return ready; }
const GPS_QueryResult *GPS_QueryPending(void) { return pending ? &result : NULL; }
void GPS_QuerySaved(void) { assert(pending); ++saved; pending=0; }
FRESULT f_open(FIL *f,const TCHAR *name,BYTE mode)
{ assert(!strcmp(name,"S_TEST/GPS_CFG.TXT") && mode==(FA_WRITE|FA_OPEN_ALWAYS)); ++opens; memset(f,0,sizeof(*f)); f->obj.objsize=length; return FR_OK; }
FRESULT f_lseek(FIL *f,FSIZE_t offset) { assert(offset==length); f->fptr=offset; return FR_OK; }
FRESULT f_write(FIL *f,const void *data,UINT n,UINT *wrote)
{
    assert(pending); /* Result stays owned throughout all nested storage calls. */
    if(write_fail) { *wrote=0; return FR_DISK_ERR; }
    assert(length+n<sizeof(contents)); memcpy(contents+length,data,n); length+=n; contents[length]=0;
    *wrote=n; f->obj.objsize=length; return FR_OK;
}
FRESULT f_close(FIL *f) { (void)f; ++closes; return close_fail ? FR_DISK_ERR : FR_OK; }
static void record(GPS_QueryStatus status)
{
    length=0; contents[0]=0; memset(bytes,0,sizeof(bytes)); pending=1;
    result=(GPS_QueryResult){.name="CFG-RATE",.payload=bytes,.status=status,.index=1,.total=21,
        .attempts=1,.rx_present=1,.rx_class=6,.rx_id=8,.length=6,.stored=6,.ck_a=0x11,.ck_b=0x22};
    bytes[0]=0xE8; bytes[1]=3; bytes[2]=bytes[4]=1;
}
int main(void)
{
    record(GPS_QUERY_OK); ready=0; GPSReport_Process(0); assert(!opens && pending);
    ready=1; raw_depth=150; GPSReport_Process(1); assert(!opens && pending);
    raw_depth=0; GPSReport_Process(1);
    assert(saved==1 && closes==1 && !pending && strstr(contents,"measurement_ms=1000 nav_cycles=1 time_ref=1"));
    assert(strstr(contents,"raw=B5 62 06 08 06 00 E8 03 01 00 01 00 11 22") && strstr(contents,"END 1\r\n"));
    record(GPS_QUERY_TIMEOUT); result.length=result.stored=result.rx_present=0; result.index=21;
    GPSReport_Process(2); assert(strstr(contents,"status=TIMEOUT") && strstr(contents,"decoded=UNKNOWN") && strstr(contents,"COMPLETE:"));
    assert(!strstr(contents,"measurement_ms="));
    record(GPS_QUERY_NAK); result.length=result.stored=2; result.rx_class=5; result.rx_id=0;
    GPSReport_Process(3); assert(strstr(contents,"status=NAK") && strstr(contents,"decoded=UNKNOWN"));
    record(GPS_QUERY_OK); result.name="NAV-SVINFO"; result.rx_class=1; result.rx_id=0x30;
    result.length=result.stored=392; bytes[4]=32; bytes[8+31*12+1]=31; bytes[8+31*12+4]=45;
    GPSReport_Process(4); assert(strstr(contents,"channels=32") && strstr(contents,"svid=31 flags=0x00 quality=0 CNO_dBHz=45"));
    record(GPS_QUERY_TRUNCATED); result.length=420; result.stored=400;
    GPSReport_Process(5); assert(strstr(contents,"[PAYLOAD TRUNCATED]") && strstr(contents,"decoded=UNKNOWN"));
    record(GPS_QUERY_MALFORMED); result.length=result.stored=0;
    GPSReport_Process(6); assert(strstr(contents,"raw=B5 62 06 08 00 00 11 22") && strstr(contents,"decoded=UNKNOWN"));
    record(GPS_QUERY_OK); result.name="UART1-VERIFY"; result.mask_state=GPS_MASK_VERIFIED;
    result.rx_id=0; result.length=result.stored=20; bytes[0]=1; bytes[14]=3; bytes[15]=0;
    const uint8_t tx[]={0xB5,0x62,6,0,1,0,1,8,0x22}; result.tx_frame=tx; result.tx_length=sizeof(tx);
    GPSReport_Process(7); assert(strstr(contents,"mask_change=VERIFIED") && strstr(contents,"out=0x0003") && strstr(contents,"tx=B5 62 06 00 01 00 01 08 22"));
    record(GPS_QUERY_SKIPPED); result.rx_present=0; result.length=result.stored=0; result.mask_state=GPS_MASK_ALREADY;
    GPSReport_Process(8); assert(strstr(contents,"status=SKIPPED") && strstr(contents,"ALREADY_0003") && !strstr(contents,"raw="));
    record(GPS_QUERY_OK); result.index=90; result.total=150;
    GPSReport_Process(9); assert(strstr(contents,"CONFIG_READ_COMPLETE") && !strstr(contents,"COMPLETE: all"));
    record(GPS_QUERY_OK); result.rx_id=0x23; result.length=result.stored=40;
    bytes[10]=3; bytes[11]=16; bytes[12]=7; GPSReport_Process(10);
    assert(strstr(contents,"minSV=3 maxSV=16 minCNO=7"));
    record(GPS_QUERY_OK); write_fail=1; unsigned before=saved; GPSReport_Process(100);
    assert(pending && saved==before); write_fail=0; unsigned attempts=opens; GPSReport_Process(101); assert(opens==attempts);
    GPSReport_Process(2100); assert(!pending && saved==before+1);
    record(GPS_QUERY_OK); close_fail=1; before=saved; GPSReport_Process(3000); assert(pending && saved==before);
    close_fail=0; GPSReport_Process(5000); assert(!pending && saved==before+1);
    puts("PASS GPS report: raw frames, decoded values, unknown failures, 32 satellites, truncation, SD absent/write/close failures preserve pending result");
}
