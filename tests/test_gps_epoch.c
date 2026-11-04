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
    epoch(1400,500000,5,2); GPS_Epoch_Service(&data,680000); GPS_Epoch_ForSample(680000,&b);
    assert(b.fix_id==3 && b.position_valid && !b.altitude_valid && !b.speed_valid && gps_diag.incomplete==1);
    epoch(1400,690000,2,3); GPS_Epoch_ForSample(700000,&a); assert(!memcmp(&a,&b,sizeof(a)) && gps_diag.late);
    epoch(1600,700000,1,3); GPS_Epoch_Service(&data,880000); GPS_Epoch_ForSample(880000,&a);
    assert(a.position_valid && a.fix_id==b.fix_id && gps_diag.invalid_fixes==1);
    GPS_Epoch_Service(&data,880100); assert(gps_diag.invalid_fixes==1);
    epoch(1600,881000,6,3); /* Late SOL/VEL must not reopen the rejected partial epoch. */
    assert(gps_diag.received_epochs==4 && gps_diag.valid_epochs==3 && gps_diag.epoch_count==4);
    epoch(1800,900000,7,3); GPS_Epoch_Service(&data,1702000); GPS_Epoch_ForSample(1702000,&a);
    assert(!a.position_valid && !data.position_valid);
    for (unsigned sec=5;sec<=20;sec+=5) { GPS_Epoch_Service(&data,900000+(uint64_t)sec*1000000); GPS_Epoch_ForSample(900000+(uint64_t)sec*1000000,&a); assert(!a.position_valid); }
    assert(!data.connected && !data.receiver_fix_valid && !data.satellites);
    GPS_Epoch_ResetStream(&data,22000000); epoch(0,22100000,7,3); GPS_Epoch_ForSample(22102000,&a);
    assert(a.position_valid && a.fix_id>3); uint32_t id=a.fix_id;
    GPS_Epoch_ResetStream(&data,23000000); epoch(604799800,23100000,7,3);
    { uint8_t wp[28],wv[36],ws[52]; nav_payloads(0,wp,wv,ws); ws[8]=1;
      GPS_Epoch_Packet(&data,1,2,wp,28,23300000); GPS_Epoch_Packet(&data,1,0x12,wv,36,23301000);
      GPS_Epoch_Packet(&data,1,6,ws,52,23302000); }
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
    /* Regression: pre-fix TOW jumps backwards after acquisition. Previously this
       rejected every valid epoch throughout the following 15 minutes. */
    GPS_Epoch_Init(); memset(&data,0,sizeof(data));
    epoch(1200000,100000,7,0);
    for (unsigned n=0;n<4500;++n) {
        epoch(1000+n*200,300000+(uint64_t)n*200000,7,3);
        assert(data.position_valid && data.satellites==9);
    }
    assert(gps_diag.valid_epochs==4500 && !gps_diag.late);
    /* Serialisation at 38400 baud can put SOL 90 ms after POSLLH. */
    GPS_Epoch_Init(); memset(&data,0,sizeof(data));
    for (unsigned n=0;n<60;++n) {
        uint64_t t=100000+(uint64_t)n*200000;
        nav_payloads(1000+n*200,p,v,s);
        GPS_Epoch_Packet(&data,1,2,p,28,t);
        GPS_Epoch_Packet(&data,1,0x12,v,36,t+45000);
        GPS_Epoch_Service(&data,t+90000);
        GPS_Epoch_Packet(&data,1,6,s,52,t+90000);
        assert(data.position_valid && data.satellites==9);
    }
    assert(gps_diag.valid_epochs==60 && !gps_diag.incomplete);
    /* The first qualified epoch can occur anywhere in the GPS week. */
    GPS_Epoch_Init(); memset(&data,0,sizeof(data));
    epoch(0,100000,7,0); epoch(400000000,300000,7,3);
    assert(data.position_valid && gps_diag.valid_epochs==1);
    puts("PASS GPS epochs: iTOW coherence, immutable repeats, identical new fix, deadlines, missing fields, 2D, validity, outages, week wrap, UTC");
}
