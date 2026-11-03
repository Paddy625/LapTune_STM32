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
    assert(Timebase_TimerHz(64000000,0)==64000000);
    assert(Timebase_TimerHz(64000000,3)==64000000); /* APB /1 aliases */
    assert(Timebase_TimerHz(32000000,4)==64000000); assert(Timebase_TimerHz(16000000,5)==32000000);
    Timebase_Init(); Timebase_TestSet(65535,0); Timebase_TestReadRace(3);
    assert(Timebase_NowUs()==65539); Timebase_OnOverflow(); assert(Timebase_NowUs()==65539);
    Timebase_Init(); Timebase_TestCapture(65530,3,1,0);
    assert(Timebase_PPSSnapshot().last_capture_us==65530);
    Timebase_TestCapture(2,3,1,0); assert(Timebase_PPSSnapshot().last_capture_us==65538);
    Timebase_Init();
    uint64_t epoch=0;
    for (unsigned pulse=0;pulse<10;++pulse) {
        uint64_t t=1000ULL+pulse*1004700ULL;
        while (epoch+65536<=t) { Timebase_TestSet(0,1); Timebase_OnOverflow(); epoch+=65536; }
        Timebase_TestSet((uint16_t)t,0); Timebase_TestCapture((uint16_t)t,(uint16_t)t,0,0);
    }
    PPS_Diagnostics pps=Timebase_PPSSnapshot();
    assert(pps.pulses==10 && pps.periods==9 && pps.sum_period_us/pps.periods==1004700);
    PPS_Capture pulse;
    assert(Timebase_PPSGet(10,&pulse) && pulse.t_us==1000ULL+9*1004700ULL);
    assert(!Timebase_PPSGet(6,&pulse));
    assert(Timebase_PPSBefore(1000ULL+8*1004700ULL+100000,&pulse) && pulse.seq==9);
    assert(!Timebase_PPSBefore(1,&pulse));
    Timebase_TestCapture(4,5,1,1); assert(Timebase_PPSSnapshot().overruns==1);
    puts("PASS timebase: >2 virtual hours, every overflow, pending IRQ, 32-bit boundary");
}
