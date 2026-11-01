#include "gps_epoch.h"
#include <string.h>
#define WEEK_MS 604800000UL
#define HISTORY 16U
typedef struct {
    uint64_t first_us,last_us;
    GPS_Fix fix;
    uint8_t parts,fix_type,flags,satellites;
} Slot;
static Slot slots[4];
static GPS_Fix history[HISTORY];
static unsigned history_head,history_count;
static uint32_t sequence,last_itow;
static uint8_t have_itow;
GPS_Diagnostics gps_diag;
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int32_t i32(const uint8_t *p) { uint32_t n=u32(p); return n<=INT32_MAX ? (int32_t)n : (int32_t)((int64_t)n-4294967296LL); }
static uint8_t newer(uint32_t a,uint32_t b) { uint32_t d=(a+WEEK_MS-b)%WEEK_MS; return d && d<WEEK_MS/2U; }
static void remember(GPS_Fix fix)
{
    history[history_head]=fix; history_head=(history_head+1U)%HISTORY;
    if (history_count<HISTORY) ++history_count;
}
static void invalidate(GPS_Data *d,uint64_t now)
{
    if (d->position_valid || !history_count || history[(history_head+HISTORY-1)%HISTORY].position_valid)
        remember((GPS_Fix){.published_us=now});
    d->position_valid=d->speed_valid=d->altitude_valid=0;
}
void GPS_Epoch_Init(void)
{
    memset(slots,0,sizeof(slots)); memset(history,0,sizeof(history)); memset(&gps_diag,0,sizeof(gps_diag));
    history_head=history_count=sequence=last_itow=have_itow=0;
}
void GPS_Epoch_ResetStream(GPS_Data *d,uint64_t now)
{
    invalidate(d,now); memset(slots,0,sizeof(slots)); have_itow=0;
    d->connected=d->datetime_valid=0;
}
static void publish(GPS_Data *d,Slot *s,uint64_t now)
{
    if (have_itow && !newer(s->fix.itow_ms,last_itow)) { ++gps_diag.late; s->parts=0; return; }
    if (have_itow) {
        uint32_t dt=(s->fix.itow_ms+WEEK_MS-last_itow)%WEEK_MS;
        if (!gps_diag.min_period_ms || dt<gps_diag.min_period_ms) gps_diag.min_period_ms=dt;
        if (dt>gps_diag.max_period_ms) gps_diag.max_period_ms=dt;
        if (dt!=200U) ++gps_diag.bad_period;
    }
    last_itow=s->fix.itow_ms; have_itow=1;
    s->fix.fix_id=++sequence; s->fix.published_us=now;
    s->fix.position_valid=(s->parts&5U)==5U && (s->flags&0x0DU)==0x0DU && (s->fix_type==2 || s->fix_type==3);
    s->fix.speed_valid=s->fix.position_valid && (s->parts&2U);
    s->fix.altitude_valid=s->fix.position_valid && s->fix_type==3;
    if (!s->fix.position_valid) ++gps_diag.invalid_fixes;
    remember(s->fix); ++gps_diag.epoch_count;
    d->position_valid=s->fix.position_valid; d->speed_valid=s->fix.speed_valid; d->altitude_valid=s->fix.altitude_valid;
    d->latitude_e7=s->fix.latitude_e7; d->longitude_e7=s->fix.longitude_e7;
    d->altitude_mm=s->fix.altitude_mm; d->speed_millikmh=s->fix.speed_millikmh;
    d->position_ms=d->speed_ms=d->altitude_ms=(uint32_t)(now/1000U);
    d->satellites=s->satellites; d->fix_quality=d->position_valid ? ((s->flags&2U)?2:1) : 0;
    s->parts=0;
}
static uint8_t days(uint16_t y,uint8_t m)
{
    static const uint8_t table[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return table[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
}
static void utc(GPS_Data *d,const uint8_t *p,uint64_t now)
{
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
}
void GPS_Epoch_Packet(GPS_Data *d,uint8_t cls,uint8_t id,const uint8_t *p,unsigned len,uint64_t now)
{
    if (cls!=1) return;
    if (id==0x21 && len==20) { utc(d,p,now); d->sentence_ms=(uint32_t)(now/1000U); d->connected=1; return; }
    uint8_t part=(id==2 && len==28)?1:((id==0x12 && len==36)?2:((id==6 && len==52)?4:0));
    if (!part) return;
    d->connected=1; d->sentence_ms=(uint32_t)(now/1000U);
    uint32_t itow=u32(p); if (itow>=WEEK_MS) return;
    if (have_itow && !newer(itow,last_itow)) { ++gps_diag.late; return; }
    Slot *s=NULL,*empty=NULL,*oldest=&slots[0];
    for (unsigned i=0;i<4;++i) {
        if (slots[i].parts && slots[i].fix.itow_ms==itow) s=&slots[i];
        if (!slots[i].parts) empty=&slots[i];
        if (slots[i].first_us<oldest->first_us) oldest=&slots[i];
    }
    if (!s) {
        s=empty ? empty : oldest;
        if (s->parts) { ++gps_diag.incomplete; publish(d,s,now); }
        memset(s,0,sizeof(*s)); s->first_us=now; s->fix.itow_ms=itow;
    }
    if (s->parts&part) { ++gps_diag.late; return; }
    if (part==1) {
        int32_t lon=i32(p+4),lat=i32(p+8);
        if (lat< -900000000 || lat>900000000 || lon< -1800000000 || lon>1800000000) return;
        s->fix.longitude_e7=lon; s->fix.latitude_e7=lat; s->fix.altitude_mm=i32(p+16);
    } else if (part==2) { if (u32(p+20)>UINT32_MAX/36U) return; s->fix.speed_millikmh=u32(p+20)*36U; }
    else { s->fix_type=p[10]; s->flags=p[11]; s->satellites=p[47]; }
    s->parts|=part; s->last_us=now;
    if (s->parts==7) publish(d,s,now);
}
void GPS_Epoch_Service(GPS_Data *d,uint64_t now)
{
    /* Oldest pending epoch first; a later fragment cannot revise a published fix. */
    for (unsigned n=0;n<4;++n) {
        Slot *old=NULL;
        for (unsigned i=0;i<4;++i) if (slots[i].parts && (!old || slots[i].first_us<old->first_us)) old=&slots[i];
        if (!old || now-old->first_us<GPS_ASSEMBLY_DEADLINE_US) break;
        ++gps_diag.incomplete; publish(d,old,old->first_us+GPS_ASSEMBLY_DEADLINE_US);
    }
    if (history_count && now-history[(history_head+HISTORY-1)%HISTORY].published_us>=GPS_EPOCH_TIMEOUT_US) invalidate(d,now);
    if ((uint32_t)((uint32_t)(now/1000U)-d->sentence_ms)>=3000U) d->connected=0;
    if ((uint32_t)((uint32_t)(now/1000U)-d->datetime_ms)>=3000U) d->datetime_valid=0;
}
void GPS_Epoch_ForSample(uint64_t sample,GPS_Fix *out)
{
    memset(out,0,sizeof(*out));
    for (unsigned i=0;i<history_count;++i) {
        const GPS_Fix *f=&history[(history_head+HISTORY-1U-i)%HISTORY];
        if (f->published_us<=sample) {
            if (sample-f->published_us<GPS_EPOCH_TIMEOUT_US) *out=*f;
            return;
        }
    }
    if (history_count) ++gps_diag.history_misses;
}
