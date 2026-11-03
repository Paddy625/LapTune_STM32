#include "gps_time.h"
#include "gps_epoch.h"
#include "timebase.h"
#include "gps_fixtures.h"
#include <assert.h>
#include <stdio.h>

static PPS_Capture pulses[4];
static uint32_t sequence;
static const uint32_t period=1004700; /* Deliberately non-ideal MCU, not an IMU-derived calibration. */
static uint64_t at(uint32_t tow) { return 1000000ULL+(uint64_t)(tow-1000)*period/1000; }
uint8_t Timebase_PPSGet(uint32_t seq,PPS_Capture *out)
{
    *out=pulses[seq%4]; return seq && out->seq==seq;
}
uint8_t Timebase_PPSBefore(uint64_t rx,PPS_Capture *out)
{
    for (unsigned i=0;i<4 && i<sequence;++i)
        if (Timebase_PPSGet(sequence-i,out) && out->t_us<=rx) return 1;
    return 0;
}
static void capture_at(uint64_t us)
{
    ++sequence; pulses[sequence%4]=(PPS_Capture){us,sequence,0};
    GPS_Time_Service(us);
}
static void capture(uint32_t tow) { capture_at(at(tow)); }
static void timtp(uint16_t week,uint32_t tow,uint64_t rx,uint32_t sub)
{
    uint8_t p[16]={0}; put32(p,tow); put32(p+4,sub); p[12]=week; p[13]=week>>8;
    GPS_Time_PulseMessage(p,sizeof(p),rx);
}
static void nav(uint32_t tow) { GPS_Time_Navigation(2400,tow,at(tow)+100000); }
static void lock(void)
{
    GPS_Time_Init(); memset(pulses,0,sizeof(pulses)); sequence=0;
    capture(1000); nav(1000);
    for (uint32_t tow=2000;tow<=4000;tow+=1000) {
        timtp(2400,tow,at(tow-1000)+110000,0);
        nav(tow-200); capture(tow);
    }
    assert(GPS_Time_Diagnostics().state==1 && GPS_Time_Diagnostics().matched==3);
    assert(GPS_Time_Diagnostics().period_ticks==period);
}
int main(void)
{
    lock(); uint64_t measured;
    nav(4200);
    assert(GPS_Time_Map(2400,4200,at(4200)+100000,&measured) && measured==at(4200));
    assert(!GPS_Time_Map(2400,4200,at(4200)-1,&measured));
    assert(!GPS_Time_Map(2400,4200,at(4200)+500001,&measured));
    GPS_Data utc={.year=2026,.month=12,.day=31,.hour=23,.minute=59,.second=59,.datetime_valid=1};
    GPS_Time_Utc(4000,&utc,at(4000)+120000);
    LogTime t;
    assert(GPS_Time_UTCAt(at(5250),&t));
    assert(t.year==2027 && t.month==1 && t.day==1 && t.hour==0 && t.second==0 && t.millisecond==250);
    assert(GPS_Time_UTCAt(at(3800),&t) && t.second==58 && t.millisecond==800);
    GPS_Time_Utc(4000,&utc,1); assert(!GPS_Time_UTCAt(at(4200),&t));
    GPS_Time_Utc(4000,&utc,at(4000)+120000);
    /* Parsing an older UART packet after a long foreground pause cannot unlock the clock. */
    GPS_Time_Service(at(2800)+100000); assert(GPS_Time_Diagnostics().state==1);
    GPS_Time_Service(at(5100));
    assert(!GPS_Time_Diagnostics().state && GPS_Time_Diagnostics().losses==1);
    assert(!GPS_Time_UTCAt(at(5100),&t));

    /* A one-second-wrong pulse label is rejected against independent NAV week/TOW. */
    GPS_Time_Init(); memset(pulses,0,sizeof(pulses)); sequence=0;
    capture(1000); nav(1000); timtp(2400,3000,at(1000)+110000,0);
    nav(1800); capture(2000);
    assert(!GPS_Time_Diagnostics().state && GPS_Time_Diagnostics().rejected==1);

    /* A small Q32 fractional correction is legal; UTC timebase is not silently accepted as GPS. */
    lock(); nav(4800); timtp(2400,5000,at(4000)+110000,128); capture(5000);
    assert(GPS_Time_Diagnostics().state && GPS_Time_Diagnostics().matched==4);
    uint8_t p[16]={0}; put32(p,6000); p[12]=2400&255; p[13]=2400>>8; p[14]=1;
    GPS_Time_PulseMessage(p,16,at(5000)+110000);
    assert(GPS_Time_Diagnostics().rejected==1);
    GPS_Time_Reset(); assert(!GPS_Time_Diagnostics().state && GPS_Time_Diagnostics().matched==4);

    /* Week rollover changes the week number, not the continuous MCU scale. */
    GPS_Time_Init(); memset(pulses,0,sizeof(pulses)); sequence=0; capture_at(1000000);
    for (unsigned n=0;n<3;++n) {
        timtp(2401,n*1000,1000000ULL+n*period+110000,0);
        GPS_Time_Navigation(n ? 2401 : 2400,n ? n*1000-200 : 604799800,
                            1000000ULL+(n+1)*period-period/5+100000);
        capture_at(1000000ULL+(n+1)*period);
    }
    assert(GPS_Time_Diagnostics().state);
    uint64_t expected=1000000ULL+3*period+period/5;
    assert(GPS_Time_Map(2401,2200,expected+100000,&measured) && measured==expected);

    /* Real epoch snapshots retain measurement time, and N belongs to the nearest historical IMU row. */
    GPS_Epoch_Init(); lock(); GPS_Data d={0}; uint8_t pos[28],vel[36],sol[52];
    nav_payloads(4200,pos,vel,sol); sol[8]=2400&255; sol[9]=2400>>8;
    GPS_Epoch_Packet(&d,1,2,pos,28,at(4200)+90000);
    GPS_Epoch_Packet(&d,1,0x12,vel,36,at(4200)+95000);
    GPS_Epoch_Packet(&d,1,6,sol,52,at(4200)+100000);
    GPS_Fix f,repeat;
    uint64_t row=at(4200)-4000;
    GPS_Epoch_ForWindow(row-9615,row-4808,&f); assert(!f.position_valid);
    GPS_Epoch_ForWindow(row,row+4807,&f);
    assert(f.position_valid && f.time_sync_state==1 && f.rx_us-f.meas_delay_us==at(4200));
    GPS_Epoch_ForWindow(row+9615,row+14422,&repeat); assert(!memcmp(&f,&repeat,sizeof(f)));
    puts("PASS GPS/PPS: next-pulse identity, 4700ppm scale, latency guards, UTC rollover, loss, bad phase, fractions and historical nearest-row association");
}
