#include "timebase.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    Timebase_Init(); uint64_t previous=0;
    for (uint64_t upper=0;upper<7200000000ULL;upper+=65536U) {
        Timebase_TestSet(65535,0); assert(Timebase_NowUs()==upper+65535);
        Timebase_TestSet(7,1); uint64_t pending=Timebase_NowUs();
        assert(pending==upper+65536+7 && pending>previous);
        Timebase_OnOverflow(); Timebase_TestSet(7,0);
        assert(Timebase_NowUs()==pending); previous=pending;
    }
    assert(previous>7200000000ULL);
    puts("PASS timebase: >2 virtual hours, every overflow, pending IRQ, 32-bit boundary");
}
