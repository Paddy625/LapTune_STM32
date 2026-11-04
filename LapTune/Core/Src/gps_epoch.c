#include "gps_epoch.h"
#include "gps_time.h"
#include <string.h>
#define WEEK_MS 604800000UL
#define HISTORY 16U
_Static_assert(sizeof(GPS_Fix)==48,"GPS snapshot RAM budget");
typedef struct {
    uint64_t first_us,last_us;
    GPS_Fix fix;
    int16_t week;
    uint8_t seen,parts,fix_type,flags,satellites;
} Slot;
static Slot slots[4];
static GPS_Fix history[HISTORY];
static unsigned history_head,history_count;
static uint32_t sequence,last_itow;
static uint64_t last_valid_rx,stream_start_us;
static int16_t last_week;
static uint8_t have_itow;
GPS_Diagnostics gps_diag;
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int32_t i32(const uint8_t *p) { uint32_t n=u32(p); return n<=INT32_MAX ? (int32_t)n : (int32_t)((int64_t)n-4294967296LL); }
static void remember(GPS_Fix fix)
{
    history[history_head]=fix; history_head=(history_head+1U)%HISTORY;
    if (history_count<HISTORY) ++history_count;
}
static void invalidate(GPS_Data *d,uint64_t now)
{
    if (d->position_valid || !history_count || history[(history_head+HISTORY-1)%HISTORY].position_valid)
    {
        GPS_Fix f=history_count ? history[(history_head+HISTORY-1)%HISTORY] : (GPS_Fix){0};
        f.published_us=now; f.position_valid=f.speed_valid=f.altitude_valid=0;
        f.time_sync_state=f.discarded_incomplete=0; f.meas_delay_us=UINT32_MAX;
        remember(f);
    }
    d->position_valid=d->speed_valid=d->altitude_valid=0;
}
void GPS_Epoch_Init(void)
{
    memset(slots,0,sizeof(slots)); memset(history,0,sizeof(history)); memset(&gps_diag,0,sizeof(gps_diag));
    history_head=history_count=sequence=last_itow=have_itow=0; last_valid_rx=stream_start_us=0; last_week=0;
    GPS_Time_Init();
}
void GPS_Epoch_ResetStream(GPS_Data *d,uint64_t now)
{
    invalidate(d,now); memset(slots,0,sizeof(slots)); have_itow=0; stream_start_us=now;
    d->connected=d->datetime_valid=d->receiver_fix_valid=0;
    GPS_Time_Reset();
}
static void publish(GPS_Data *d,Slot *s,uint64_t now)
{
    GPS_Fix *f=&s->fix;
    f->published_us=now; f->rx_us=s->last_us; f->epoch_known=1;
    f->time_valid=(s->parts&4U) && (s->flags&0x0CU)==0x0CU && s->week>=0;
    f->position_valid=(s->parts&5U)==5U && f->time_valid && (s->flags&1U) && (s->fix_type==2 || s->fix_type==3);
    f->speed_valid=f->position_valid && (s->parts&2U);
    f->altitude_valid=f->position_valid && s->fix_type==3;
    f->meas_delay_us=UINT32_MAX; f->time_sync_state=0;
    if (f->position_valid) {
        /* An unqualified pre-fix TOW must never become the ordering anchor.
           After a real fix outage, accept the receiver's corrected time afresh. */
        if (have_itow) {
            int64_t dt=(int64_t)(s->week-last_week)*WEEK_MS+(int64_t)f->itow_ms-last_itow;
            if (dt<=0 && f->rx_us>=last_valid_rx && f->rx_us-last_valid_rx<GPS_EPOCH_TIMEOUT_US) {
                ++gps_diag.late; s->seen=s->parts=0; return;
            }
            if (dt>0 && dt<=1000) {
                if (!gps_diag.min_period_ms || (uint32_t)dt<gps_diag.min_period_ms) gps_diag.min_period_ms=(uint32_t)dt;
                if ((uint32_t)dt>gps_diag.max_period_ms) gps_diag.max_period_ms=(uint32_t)dt;
                if (dt!=200) ++gps_diag.bad_period;
            }
        }
        last_itow=f->itow_ms; last_week=s->week; last_valid_rx=f->rx_us; have_itow=1;
        GPS_Time_Navigation((uint16_t)s->week,f->itow_ms,f->rx_us);
        uint64_t measurement;
        if (GPS_Time_Map((uint16_t)s->week,f->itow_ms,f->rx_us,&measurement)) {
            f->meas_delay_us=(uint32_t)(f->rx_us-measurement); f->time_sync_state=1;
        }
    }
    ++gps_diag.epoch_count;
    if (!f->position_valid) ++gps_diag.invalid_fixes; else ++gps_diag.valid_epochs;
    /* A missing status fragment is not evidence that the receiver lost its fix.
       Keep the last complete solution only until its explicit 800 ms expiry. */
    uint8_t incomplete_status=(s->parts&5U)!=5U;
    if (!f->position_valid && incomplete_status && d->position_valid &&
        now>=last_valid_rx && now-last_valid_rx<GPS_EPOCH_TIMEOUT_US) {
        /* Keep a tombstone for duplicate rejection without replacing the live cache. */
        f->discarded_incomplete=1; remember(*f);
        s->seen=s->parts=0; return;
    }
    remember(*f);
    d->position_valid=f->position_valid; d->speed_valid=f->speed_valid; d->altitude_valid=f->altitude_valid;
    d->latitude_e7=f->latitude_e7; d->longitude_e7=f->longitude_e7;
    d->altitude_mm=f->altitude_mm; d->speed_millikmh=f->speed_millikmh;
    d->position_ms=d->speed_ms=d->altitude_ms=(uint32_t)(now/1000U);
    d->fix_quality=d->position_valid ? ((s->flags&2U)?2:1) : 0;
    s->seen=s->parts=0;
}
static uint8_t days(uint16_t y,uint8_t m)
{
    static const uint8_t table[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return table[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
}
static void utc(GPS_Data *d,const uint8_t *p,uint64_t now)
{
    gps_diag.receiver.utc_flags=p[19]; gps_diag.receiver.seen|=GPS_RX_UTC;
    uint16_t year=(uint16_t)p[12]|((uint16_t)p[13]<<8);
    if ((p[19]&7)!=7 || year<1980 || year>2099 || !p[14] || p[14]>12 || !p[15] ||
        p[15]>days(year,p[14]) || p[16]>23 || p[17]>59 || p[18]>59) { d->datetime_valid=0; return; }
    int32_t nano=i32(p+8); if (nano<=-1000000000 || nano>=1000000000) { d->datetime_valid=0; return; }
    int32_t ms=nano<0 ? -(int32_t)((-(int64_t)nano+999999)/1000000) : nano/1000000;
    int32_t day_ms=((int32_t)p[16]*3600+p[17]*60+p[18])*1000+ms;
    d->year=year; d->month=p[14]; d->day=p[15];
    if (day_ms<0) { day_ms+=86400000; if (--d->day==0) { if (--d->month==0) { d->month=12; --d->year; } d->day=days(d->year,d->month); } }
    if (day_ms>=86400000) { day_ms-=86400000; if (++d->day>days(d->year,d->month)) { d->day=1; if (++d->month>12) { d->month=1; ++d->year; } } }
    d->hour=day_ms/3600000; d->minute=(day_ms/60000)%60; d->second=(day_ms/1000)%60; d->millisecond=day_ms%1000;
    d->datetime_ms=(uint32_t)(now/1000U); d->datetime_valid=1;
    GPS_Time_Utc(u32(p),d,now);
}
void GPS_Epoch_Packet(GPS_Data *d,uint8_t cls,uint8_t id,const uint8_t *p,unsigned len,uint64_t now)
{
    if (cls!=1) return;
    if (id==0x21 && len==20) { utc(d,p,now); d->sentence_ms=(uint32_t)(now/1000U); d->connected=1; return; }
    uint8_t part=(id==2 && len==28)?1:((id==0x12 && len==36)?2:((id==6 && len==52)?4:0));
    if (!part) return;
    if (part==4) {
        GPS_ReceiverStatus *r=&gps_diag.receiver;
        uint16_t week=(uint16_t)p[8]|((uint16_t)p[9]<<8);
        r->week=week<=INT16_MAX ? (int16_t)week : (int16_t)((int32_t)week-65536);
        r->fix_type=p[10]; r->sol_flags=p[11]; r->used_sats=p[47]; r->seen|=GPS_RX_SOL;
    }
    d->connected=1; d->sentence_ms=(uint32_t)(now/1000U);
    uint32_t itow=u32(p); if (itow>=WEEK_MS) return;
    Slot *s=NULL,*empty=NULL,*oldest=&slots[0];
    for (unsigned i=0;i<4;++i) {
        if (slots[i].seen && slots[i].fix.itow_ms==itow) s=&slots[i];
        if (!slots[i].seen) empty=&slots[i];
        if (slots[i].first_us<oldest->first_us) oldest=&slots[i];
    }
    if (!s) {
        for (unsigned i=0;i<history_count;++i) {
            const GPS_Fix *past=&history[(history_head+HISTORY-1U-i)%HISTORY];
            if (past->epoch_known && past->rx_us>=stream_start_us && past->itow_ms==itow && now>=past->rx_us && now-past->rx_us<2000000U) {
                /* A new qualified SOL may supersede an unqualified startup TOW. */
                if (!(part==4 && !past->time_valid && !past->discarded_incomplete && (p[11]&0x0C)==0x0C)) { ++gps_diag.late; return; }
            }
        }
        s=empty ? empty : oldest;
        if (s->seen) { ++gps_diag.incomplete; publish(d,s,now); }
        memset(s,0,sizeof(*s)); s->first_us=now; s->fix.itow_ms=itow; s->fix.fix_id=++sequence; s->fix.meas_delay_us=UINT32_MAX; ++gps_diag.received_epochs;
    }
    if (s->seen&part) { ++gps_diag.late; return; }
    s->seen|=part; s->last_us=now;
    if (part==1) {
        int32_t lon=i32(p+4),lat=i32(p+8);
        if (lat< -900000000 || lat>900000000 || lon< -1800000000 || lon>1800000000) return;
        s->fix.longitude_e7=lon; s->fix.latitude_e7=lat; s->fix.altitude_mm=i32(p+16);
    } else if (part==2) { if (u32(p+20)>UINT32_MAX/36U) return; s->fix.speed_millikmh=u32(p+20)*36U; }
    else {
        s->fix_type=p[10]; s->flags=p[11]; s->satellites=p[47];
        uint16_t week=(uint16_t)p[8]|((uint16_t)p[9]<<8);
        s->week=week<32768U ? (int16_t)week : -1;
        d->satellites=s->satellites;
        d->receiver_fix_valid=(s->flags&1U) && (s->fix_type==2 || s->fix_type==3);
        if (!d->receiver_fix_valid || (s->flags&0x0CU)!=0x0CU) have_itow=0;
    }
    s->parts|=part; s->last_us=now;
    if (s->parts==7) publish(d,s,now);
}
void GPS_Epoch_Service(GPS_Data *d,uint64_t now)
{
    /* Oldest pending epoch first; a later fragment cannot revise a published fix. */
    for (unsigned n=0;n<4;++n) {
        Slot *old=NULL;
        for (unsigned i=0;i<4;++i) if (slots[i].seen && (!old || slots[i].first_us<old->first_us)) old=&slots[i];
        if (!old || now<old->first_us || now-old->first_us<GPS_ASSEMBLY_DEADLINE_US) break;
        ++gps_diag.incomplete; publish(d,old,old->first_us+GPS_ASSEMBLY_DEADLINE_US);
    }
    if (d->position_valid && now>=last_valid_rx && now-last_valid_rx>=GPS_EPOCH_TIMEOUT_US) invalidate(d,now);
    if ((uint32_t)((uint32_t)(now/1000U)-d->sentence_ms)>=3000U) d->connected=0;
    if ((uint32_t)((uint32_t)(now/1000U)-d->datetime_ms)>=3000U) d->datetime_valid=0;
}
void GPS_Epoch_ForWindow(uint64_t sample,uint64_t midpoint,GPS_Fix *out)
{
    memset(out,0,sizeof(*out)); out->meas_delay_us=UINT32_MAX;
    uint64_t best=0; uint8_t found=0;
    for (unsigned i=0;i<history_count;++i) {
        const GPS_Fix *f=&history[(history_head+HISTORY-1U-i)%HISTORY];
        if (f->discarded_incomplete) continue;
        uint64_t at=f->time_sync_state ? f->rx_us-f->meas_delay_us : f->published_us;
        if (at<=midpoint && (!found || at>best)) {
            *out=*f; best=at; found=1;
        }
    }
    if (!found) { if (history_count) ++gps_diag.history_misses; return; }
    if (sample>=best && sample-best>=GPS_EPOCH_TIMEOUT_US)
        out->position_valid=out->speed_valid=out->altitude_valid=0;
}
void GPS_Epoch_ForSample(uint64_t sample,GPS_Fix *out) { GPS_Epoch_ForWindow(sample,sample,out); }
