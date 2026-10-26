#include "log_clock.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    GPS_Data d={.datetime_valid=1,.year=2024,.month=2,.day=29,.hour=23,.minute=59,.second=59,.millisecond=900,.datetime_ms=1000};
    LogClock_Update(&d);
    LogTime t=LogClock_At(1200);
    assert(t.valid && t.year==2024 && t.month==3 && t.day==1 && t.hour==0 && t.second==0 && t.millisecond==100);
    d.year=2026; d.month=1; d.day=1; d.hour=d.minute=d.second=d.millisecond=0;
    LogClock_Update(&d); t=LogClock_At(900);
    assert(t.valid && t.year==2025 && t.month==12 && t.day==31 && t.hour==23 && t.minute==59 && t.second==59 && t.millisecond==900);
    assert(!LogClock_At(4000).valid);
    d.datetime_ms=UINT32_MAX-100; LogClock_Update(&d); t=LogClock_At(99);
    assert(t.valid && t.millisecond==200);
    d.datetime_valid=0; LogClock_Update(&d); assert(!LogClock_At(100).valid);
    puts("PASS log clock: fraction, midnight, leap day/year, backwards backlog, stale UTC and tick wrap");
}
