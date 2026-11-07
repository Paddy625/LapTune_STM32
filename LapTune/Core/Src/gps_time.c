#include "gps_time.h"
#include "gps_epoch.h"
#include "timebase.h"
#include <string.h>
#include <limits.h>
#define WEEK_MS 604800000LL
#define NAV_LATENCY_LIMIT_US 500000U
#define CLOCK_FRESH_US 1500000U
static struct {
    uint64_t pulse_us,nav_rx_us,pending_gps_ms,nav_gps_ms,utc_rx_us;
    int64_t gps_ms;
    uint32_t sequence,pending_sequence,utc_itow_ms;
    LogTime utc;
    uint8_t streak,pending,have_anchor,have_nav;
} clock_model;
static GPS_TimeDiagnostics diag;
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int64_t gps_ms(uint16_t week,uint32_t tow) { return (int64_t)week*WEEK_MS+tow; }
static uint8_t latency(int64_t gps,uint64_t rx,int64_t anchor_gps,uint64_t anchor_mcu,uint32_t period,uint64_t *mapped)
{
    int64_t d=gps-anchor_gps;
    if (d< -3000 || d>3000) return 0;
    int64_t t=(int64_t)anchor_mcu+d*period/1000;
    if (t<0 || (uint64_t)t>rx || rx-(uint64_t)t>NAV_LATENCY_LIMIT_US) return 0;
    *mapped=(uint64_t)t; return 1;
}
void GPS_Time_Init(void) { memset(&clock_model,0,sizeof(clock_model)); memset(&diag,0,sizeof(diag)); }
void GPS_Time_Reset(void)
{
    if (diag.state) ++diag.losses;
    memset(&clock_model,0,sizeof(clock_model)); diag.state=0;
}
void GPS_Time_Service(uint64_t now)
{
    if (clock_model.pending) {
        PPS_Capture p;
        if (Timebase_PPSGet(clock_model.pending_sequence,&p) && p.t_us<=now) {
            clock_model.pending=0;
            uint32_t period=1000000U;
            uint8_t consecutive=clock_model.have_anchor && p.seq==clock_model.sequence+1U &&
                (int64_t)clock_model.pending_gps_ms-clock_model.gps_ms==1000;
            if (consecutive) {
                uint64_t d=p.t_us-clock_model.pulse_us;
                if (d<970000U || d>1030000U) consecutive=0;
                else period=(uint32_t)d;
            }
            uint64_t nav_time;
            uint8_t qualified=clock_model.have_nav && now>=clock_model.nav_rx_us &&
                now-clock_model.nav_rx_us<GPS_EPOCH_TIMEOUT_US;
            if (clock_model.have_anchor && p.seq<=clock_model.sequence) { ++diag.rejected; return; }
            if (p.overrun || !qualified || !latency((int64_t)clock_model.nav_gps_ms,clock_model.nav_rx_us,
                (int64_t)clock_model.pending_gps_ms,p.t_us,period,&nav_time)) {
                ++diag.rejected;
                if (diag.state) ++diag.losses;
                diag.state=clock_model.streak=0;
            } else {
                uint32_t variation=period>diag.period_ticks ? period-diag.period_ticks : diag.period_ticks-period;
                if (!consecutive || (clock_model.streak>=2 && variation>500U)) clock_model.streak=1;
                else if (clock_model.streak<3) ++clock_model.streak;
                diag.period_change_ticks=clock_model.streak>=2 ? variation : 0;
                diag.period_ticks=period; ++diag.matched;
                clock_model.gps_ms=(int64_t)clock_model.pending_gps_ms;
                clock_model.pulse_us=p.t_us; clock_model.sequence=p.seq; clock_model.have_anchor=1;
                if (clock_model.streak>=3 && !diag.state) { diag.state=1; ++diag.locks; }
            }
        }
    }
    if (diag.state && ((now>=clock_model.pulse_us && now-clock_model.pulse_us>CLOCK_FRESH_US) ||
        (now>=clock_model.nav_rx_us && now-clock_model.nav_rx_us>GPS_EPOCH_TIMEOUT_US))) {
        diag.state=0; clock_model.streak=0; ++diag.losses;
    }
}
void GPS_Time_PulseMessage(const uint8_t *p,unsigned len,uint64_t rx)
{
    GPS_Time_Service(rx);
    ++diag.timtp_packets;
    if (len!=16) { ++diag.rejected; return; }
    uint32_t tow=u32(p),sub=u32(p+4); uint16_t week=(uint16_t)p[12]|((uint16_t)p[13]<<8);
    uint64_t pulse_us=(uint64_t)tow*1000U+(((uint64_t)sub*1000U+2147483648ULL)>>32);
    uint32_t remainder=(uint32_t)(pulse_us%1000000U);
    uint8_t integral=remainder<=2U || remainder>=999998U;
    pulse_us=((pulse_us+500000U)/1000000U)*1000000U;
    PPS_Capture previous;
    /* NEO-6 TIM-TP describes the NEXT pulse. We configure legacy CFG-TP for
       GPS time, 1 Hz, rising edge. Never label a pulse from nearest UART alone. */
    /* u-blox 6: byte 15 and flag bits 2..7 are reserved, not newer GNSS IDs. */
    if ((p[14]&1U) || !week || tow>=WEEK_MS || !integral ||
        !Timebase_PPSBefore(rx,&previous) || previous.overrun || rx-previous.t_us>=900000U) {
        ++diag.rejected; return;
    }
    clock_model.pending_gps_ms=(uint64_t)week*WEEK_MS+pulse_us/1000U;
    clock_model.pending_sequence=previous.seq+1U; clock_model.pending=1;
    GPS_Time_Service(rx);
}
void GPS_Time_Navigation(uint16_t week,uint32_t tow,uint64_t rx)
{
    if (clock_model.have_nav && rx<clock_model.nav_rx_us) return;
    clock_model.nav_gps_ms=(uint64_t)gps_ms(week,tow); clock_model.nav_rx_us=rx; clock_model.have_nav=1;
    GPS_Time_Service(rx);
}
uint8_t GPS_Time_Map(uint16_t week,uint32_t tow,uint64_t rx,uint64_t *mapped)
{
    if (!diag.state || !clock_model.have_anchor) return 0;
    if (rx>clock_model.pulse_us && rx-clock_model.pulse_us>CLOCK_FRESH_US) return 0;
    return latency(gps_ms(week,tow),rx,clock_model.gps_ms,clock_model.pulse_us,diag.period_ticks,mapped);
}
void GPS_Time_Utc(uint32_t tow,const GPS_Data *d,uint64_t rx)
{
    clock_model.utc=(LogTime){.year=d->year,.month=d->month,.day=d->day,.hour=d->hour,
        .minute=d->minute,.second=d->second,.millisecond=d->millisecond,.valid=d->datetime_valid};
    clock_model.utc_itow_ms=tow; clock_model.utc_rx_us=rx;
}
GPS_UTCStatus GPS_Time_UTCStatus(uint64_t sample,LogTime *out)
{
    *out=(LogTime){0};
    if (!clock_model.utc.valid) return GPS_UTC_WAIT_ANCHOR;
    if (!diag.state || !diag.period_ticks) return GPS_UTC_WAIT_PPS;
    uint64_t age=sample>=clock_model.utc_rx_us ? sample-clock_model.utc_rx_us : clock_model.utc_rx_us-sample;
    if (age>3000000U) return GPS_UTC_OUT_OF_RANGE; /* A repeated iTOW in a later week cannot revive old UTC. */
    int64_t offset=(int64_t)sample-(int64_t)clock_model.pulse_us;
    if (offset< -3000000 || offset>1500000) return GPS_UTC_OUT_OF_RANGE;
    int64_t utc_offset=(int64_t)clock_model.utc_itow_ms-clock_model.gps_ms%WEEK_MS;
    if (utc_offset>WEEK_MS/2) utc_offset-=WEEK_MS;
    if (utc_offset< -WEEK_MS/2) utc_offset+=WEEK_MS;
    int64_t delta=offset*1000000/diag.period_ticks-utc_offset*1000;
    if (delta< -3000000 || delta>3000000) return GPS_UTC_OUT_OF_RANGE;
    *out=LogClock_ShiftUs(clock_model.utc,delta); return GPS_UTC_OK;
}
uint8_t GPS_Time_UTCAt(uint64_t sample,LogTime *out)
{ return GPS_Time_UTCStatus(sample,out)==GPS_UTC_OK; }
GPS_TimeDiagnostics GPS_Time_Diagnostics(void) { return diag; }
