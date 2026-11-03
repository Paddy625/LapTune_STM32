#ifndef LAPTUNE_TIMEBASE_H
#define LAPTUNE_TIMEBASE_H
#include <stdint.h>
typedef struct { uint32_t pclk1_hz,timer_hz,prescaler,tick_hz,reload; } TimebaseConfig;
typedef struct {
    uint64_t last_capture_us,sum_period_us;
    uint32_t pulses,periods,last_period_us,min_period_us,max_period_us,overruns,rejected_periods;
} PPS_Diagnostics;
typedef struct { uint64_t t_us; uint32_t seq; uint8_t overrun; } PPS_Capture;
uint8_t Timebase_PPSBefore(uint64_t at_us,PPS_Capture *capture);
uint8_t Timebase_PPSGet(uint32_t sequence,PPS_Capture *capture);
extern TimebaseConfig timebase_config;
/* Nominal MCU microseconds; never disciplined from assumed ODR or PPS. */
void Timebase_Init(void);
uint64_t Timebase_NowUs(void);
void Timebase_OnOverflow(void); /* Shared TIM2 update + CH1 capture IRQ. */
uint32_t Timebase_TimerHz(uint32_t pclk1,uint8_t apb_encoding);
PPS_Diagnostics Timebase_PPSSnapshot(void);
#ifdef LAPTUNE_HOST_TEST
void Timebase_TestSet(uint16_t counter,uint8_t pending);
void Timebase_TestReadRace(uint16_t counter_after_overflow);
void Timebase_TestCapture(uint16_t captured,uint16_t counter,uint8_t overflow,uint8_t overrun);
#endif
#endif
