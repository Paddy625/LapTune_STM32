#include "gps_epoch.h"
#include "gps_fixtures.h"
#include <assert.h>
#include <stdio.h>
static GPS_Data data;
static void epoch(uint32_t itow,uint64_t t,unsigned parts,unsigned type)
{
    uint8_t p[28],v[36],s[52]; nav_payloads(itow,p,v,s); s[10]=type;
    if (parts&1) GPS_Epoch_Packet(&data,1,2,p,28,t);
    if (parts&2) GPS_Epoch_Packet(&data,1,0x12,v,36,t+1000);
    if (parts&4) GPS_Epoch_Packet(&data,1,6,s,52,t+2000);
}
int main(void)
{
    GPS_Fix a,b; GPS_Epoch_Init(); GPS_Epoch_ForSample(10,&a); assert(!a.position_valid);
    epoch(1000,100000,7,3); GPS_Epoch_ForSample(102000,&a);
    assert(a.position_valid && a.fix_id==1 && a.itow_ms==1000 && a.latitude_e7==521234567);
    assert(a.speed_millikmh==44424 && a.altitude_mm==123456 && a.altitude_valid);
    GPS_Epoch_ForSample(200000,&b); assert(!memcmp(&a,&b,sizeof(a)));
    epoch(1200,300000,7,3); GPS_Epoch_ForSample(302000,&b);
    assert(b.fix_id==2 && b.latitude_e7==a.latitude_e7 && gps_diag.min_period_ms==200);
    epoch(1400,500000,5,2); GPS_Epoch_Service(&data,580000); GPS_Epoch_ForSample(580000,&b);
    assert(b.fix_id==3 && b.position_valid && !b.altitude_valid && !b.speed_valid && gps_diag.incomplete==1);
    epoch(1400,590000,2,3); GPS_Epoch_ForSample(600000,&a); assert(!memcmp(&a,&b,sizeof(a)) && gps_diag.late);
    epoch(1600,700000,1,3); GPS_Epoch_Service(&data,780000); GPS_Epoch_ForSample(780000,&a);
    assert(!a.position_valid && a.epoch_known && a.itow_ms==1600 && gps_diag.invalid_fixes==1);
    GPS_Epoch_Service(&data,780100); assert(gps_diag.invalid_fixes==1);
    assert(gps_diag.received_epochs==4 && gps_diag.valid_epochs==3 && gps_diag.epoch_count==4);
    epoch(1800,900000,7,3); GPS_Epoch_Service(&data,1702000); GPS_Epoch_ForSample(1702000,&a);
    assert(!a.position_valid && !data.position_valid);
    for (unsigned sec=5;sec<=20;sec+=5) { GPS_Epoch_Service(&data,900000+(uint64_t)sec*1000000); GPS_Epoch_ForSample(900000+(uint64_t)sec*1000000,&a); assert(!a.position_valid); }
    GPS_Epoch_ResetStream(&data,22000000); epoch(0,22100000,7,3); GPS_Epoch_ForSample(22102000,&a);
    assert(a.position_valid && a.fix_id>3); uint32_t id=a.fix_id;
    GPS_Epoch_ResetStream(&data,23000000); epoch(604799800,23100000,7,3); epoch(0,23300000,7,3);
    GPS_Epoch_ForSample(23302000,&a); assert(a.fix_id==id+2 && a.itow_ms==0);
    uint8_t utc[20]={0}; utc[12]=0xEA; utc[13]=7; utc[14]=3; utc[15]=1; utc[19]=7;
    put32(utc+8,(uint32_t)-1); GPS_Epoch_Packet(&data,1,0x21,utc,20,23400000);
    assert(data.datetime_valid && data.month==2 && data.day==28 && data.hour==23 && data.millisecond==999);
    utc[19]=0; GPS_Epoch_Packet(&data,1,0x21,utc,20,23500000); assert(!data.datetime_valid);
    /* Out-of-order parts are joined by iTOW, never by arrival order or coordinates. */
    GPS_Epoch_Init(); memset(&data,0,sizeof(data));
    uint8_t p[28],v[36],s[52]; nav_payloads(100,p,v,s);
    GPS_Epoch_Packet(&data,1,6,s,52,100); GPS_Epoch_Packet(&data,1,0x12,v,36,200);
    assert(!data.position_valid); GPS_Epoch_Packet(&data,1,2,p,28,300);
    assert(data.position_valid); s[11]=1; nav_payloads(300,p,v,s); s[11]=1;
    GPS_Epoch_Packet(&data,1,2,p,28,200100); GPS_Epoch_Packet(&data,1,0x12,v,36,200200); GPS_Epoch_Packet(&data,1,6,s,52,200300);
    assert(!data.position_valid);
    puts("PASS GPS epochs: iTOW coherence, immutable repeats, identical new fix, deadlines, missing fields, 2D, validity, outages, week wrap, UTC");
}
