#include "log_clock.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    LogTime anchor={.valid=1,.year=2024,.month=2,.day=29,.hour=23,.minute=59,.second=59,.millisecond=900};
    LogTime t=LogClock_ShiftUs(anchor,200000);
    assert(t.valid && t.year==2024 && t.month==3 && t.day==1 && t.hour==0 && t.second==0 && t.millisecond==100);
    anchor=(LogTime){.valid=1,.year=2026,.month=1,.day=1};
    t=LogClock_ShiftUs(anchor,-100000);
    assert(t.valid && t.year==2025 && t.month==12 && t.day==31 && t.hour==23 && t.minute==59 && t.second==59 && t.millisecond==900);
    anchor=(LogTime){.valid=1,.year=2024,.month=3,.day=1};
    t=LogClock_ShiftUs(anchor,-1000); assert(t.month==2 && t.day==29 && t.millisecond==999);
    anchor.valid=0; assert(!LogClock_ShiftUs(anchor,1000000).valid);
    puts("PASS UTC calendar: microseconds, midnight, leap day/year, backwards samples and invalid anchors");
}
