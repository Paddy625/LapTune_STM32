#include "gps_display.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls; static char rows[6][26]; static GPS_Data *live;
HAL_StatusTypeDef TFT_Text(uint16_t x,uint16_t y,const char *t,uint8_t scale,uint16_t fg,uint16_t bg)
{
    (void)fg; (void)bg; assert(scale==1 && strlen(t)==25 && x+strlen(t)*6<=TFT_WIDTH);
    assert(y==72U+(calls%6U)*8U); strcpy(rows[calls%6],t); ++calls;
    if (live) live->latitude_e7=0; /* Concurrent input change must not tear the frame. */
    return HAL_OK;
}
static void starts(unsigned row,const char *text)
{ assert(!strncmp(rows[row],text,strlen(text))); for (unsigned i=strlen(text);i<25;++i) assert(rows[row][i]==' '); }
int main(void)
{
    GPS_Data d={.connected=1,.position_valid=1,.receiver_fix_valid=1,.altitude_valid=1,.datetime_valid=1,
        .latitude_e7=-349333333,.longitude_e7=-583666667,.altitude_mm=-12345,
        .year=2026,.month=10,.day=20,.hour=12,.minute=35,.second=19,.satellites=8};
    live=&d; assert(GPSDisplay_Update(0,&d,NULL)==HAL_OK && calls==6); live=NULL;
    starts(0,"GPS FIX S:08"); starts(1,"LAT -34.9333333"); starts(2,"LON -058.3666667");
    starts(3,"ALT -12.3 M"); starts(4,"UTC 12:35:19"); starts(5,"DATE 2026-10-20");
    GPSDisplay_Update(199,&d,NULL); assert(calls==6);
    memset(&d,0,sizeof(d)); GPSDisplay_Update(200,&d,NULL); assert(calls==12);
    starts(0,"GPS NO DATA"); starts(1,"LAT ---"); starts(4,"UTC --:--:--"); starts(5,"DATE ----/--/--");
    d.connected=1; GPSDisplay_Update(400,&d,NULL); starts(0,"GPS NO FIX S:00");
    d.receiver_fix_valid=1; d.satellites=9; GPSDisplay_Update(600,&d,NULL); starts(0,"GPS FIX WAIT DATA S:09");
    GPS_ConfigStatus c={.state=GPS_CONFIG_ERROR}; d.position_valid=1;
    GPSDisplay_Update(800,&d,&c); starts(0,"GPS FIX S:09"); /* Config error must not impersonate RF loss. */
    d.connected=0; GPSDisplay_Update(1000,&d,&c); starts(0,"GPS CFG ERROR");
    d.connected=1; d.time_sync_state=d.datetime_valid=1; GPSDisplay_Update(1200,&d,NULL); starts(4,"UTC PPS 00:00:00");
    c.restore_state=GPS_RESTORE_PENDING; GPSDisplay_Update(1400,&d,&c); starts(0,"GPS RESTORE 1/3");
    c.restore_state=GPS_RESTORE_COLD_SENT; GPSDisplay_Update(1600,&d,&c); starts(0,"GPS RESTORE 3/3");
    c.restore_state=GPS_RESTORE_RECONFIGURED; GPSDisplay_Update(1800,&d,&c); starts(0,"GPS FIX S:09");
    puts("PASS GPS UI: complete snapshot frame, RF/data/config distinction, PPS label, clearing and 5Hz");
}
