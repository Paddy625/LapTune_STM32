#include "log_clock.h"
#include "gps_time.h"
static uint8_t days(uint16_t y,uint8_t m)
{
    static const uint8_t table[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return table[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
}
LogTime LogClock_ShiftUs(LogTime t,int64_t delta)
{
    if (!t.valid) return t;
    int64_t day_us=((int64_t)t.hour*3600+t.minute*60+t.second)*1000000+t.millisecond*1000+delta;
    while (day_us<0) {
        day_us+=86400000000LL;
        if (--t.day==0) { if (--t.month==0) { t.month=12; --t.year; } t.day=days(t.year,t.month); }
    }
    while (day_us>=86400000000LL) {
        day_us-=86400000000LL;
        if (++t.day>days(t.year,t.month)) { t.day=1; if (++t.month>12) { t.month=1; ++t.year; } }
    }
    t.hour=day_us/3600000000LL; t.minute=(day_us/60000000)%60;
    t.second=(day_us/1000000)%60; t.millisecond=(day_us/1000)%1000; return t;
}
LogTime LogClock_AtUs(uint64_t sample_us)
{ LogTime t={0}; (void)GPS_Time_UTCAt(sample_us,&t); return t; }
