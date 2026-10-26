#include "log_clock.h"
#include <string.h>
static GPS_Data anchor;
static uint8_t days(uint16_t y,uint8_t m)
{
    static const uint8_t table[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return table[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
}
void LogClock_Update(const GPS_Data *data) { anchor=*data; }
LogTime LogClock_At(uint32_t sample_ms)
{
    LogTime t={0};
    int32_t elapsed=(int32_t)(sample_ms-anchor.datetime_ms);
    if (!anchor.datetime_valid || elapsed<=-(int32_t)GPS_FRESH_MS ||
        elapsed>=(int32_t)GPS_FRESH_MS || anchor.second>59) return t;
    t.year=anchor.year; t.month=anchor.month; t.day=anchor.day;
    int32_t day_ms=((int32_t)anchor.hour*3600+anchor.minute*60+anchor.second)*1000+
                   anchor.millisecond+elapsed;
    if (day_ms<0) {
        day_ms+=86400000;
        if (--t.day==0) { if (--t.month==0) { t.month=12; --t.year; } t.day=days(t.year,t.month); }
    } else if (day_ms>=86400000) {
        day_ms-=86400000;
        if (++t.day>days(t.year,t.month)) { t.day=1; if (++t.month>12) { t.month=1; ++t.year; } }
    }
    t.hour=day_ms/3600000; t.minute=(day_ms/60000)%60;
    t.second=(day_ms/1000)%60; t.millisecond=day_ms%1000; t.valid=1;
    return t;
}
