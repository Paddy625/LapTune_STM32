#include "timebase.h"
#include <string.h>
#ifndef LAPTUNE_HOST_TEST
#include "stm32f1xx_hal.h"
static uint32_t lock(void) { uint32_t m=__get_PRIMASK(); __disable_irq(); return m; }
static void unlock(uint32_t m) { __set_PRIMASK(m); }
static uint16_t read_counter(void) { return (uint16_t)TIM2->CNT; }
static uint8_t overflow_pending(void) { return !!(TIM2->SR&TIM_SR_UIF); }
#else
static uint16_t counter,race_counter;
static uint8_t pending,race;
static uint32_t lock(void) { return 0; }
static void unlock(uint32_t m) { (void)m; }
void Timebase_TestSet(uint16_t c,uint8_t p) { counter=c; pending=p; }
void Timebase_TestReadRace(uint16_t c) { race=1; race_counter=c; }
static uint16_t read_counter(void)
{
    uint16_t c=counter;
    if (race) { race=0; counter=race_counter; pending=1; }
    return c;
}
static uint8_t overflow_pending(void) { return pending; }
#endif
static volatile uint64_t upper;
static volatile PPS_Diagnostics pps;
static PPS_Capture pulses[4];
TimebaseConfig timebase_config;
uint32_t Timebase_TimerHz(uint32_t pclk1,uint8_t encoding)
{ return encoding<4 ? pclk1 : pclk1*2U; }
void Timebase_Init(void)
{
    upper=0; memset((void*)&pps,0,sizeof(pps)); memset(pulses,0,sizeof(pulses));
#ifndef LAPTUNE_HOST_TEST
    __HAL_RCC_TIM2_CLK_ENABLE(); __HAL_RCC_GPIOA_CLK_ENABLE();
    uint32_t pclk=HAL_RCC_GetPCLK1Freq();
    uint32_t clock=Timebase_TimerHz(pclk,(RCC->CFGR>>8)&7U);
    timebase_config=(TimebaseConfig){pclk,clock,clock/1000000U-1U,clock/(clock/1000000U),65535};
    TIM2->CR1=0; TIM2->PSC=timebase_config.prescaler; TIM2->ARR=65535;
    /* PA0 = TIM2_CH1, rising edge captured by hardware on the IMU clock. */
    GPIO_InitTypeDef pin={0}; pin.Pin=GPIO_PIN_0;
    pin.Mode=GPIO_MODE_INPUT; pin.Pull=GPIO_PULLDOWN; HAL_GPIO_Init(GPIOA,&pin);
    TIM2->CCMR1=TIM_CCMR1_CC1S_0; TIM2->CCER=TIM_CCER_CC1E;
    TIM2->EGR=TIM_EGR_UG; TIM2->SR=0; TIM2->CNT=0;
    TIM2->DIER=TIM_DIER_UIE|TIM_DIER_CC1IE;
    HAL_NVIC_SetPriority(TIM2_IRQn,0,0); HAL_NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1=TIM_CR1_CEN;
#else
    counter=race=pending=0;
    timebase_config=(TimebaseConfig){32000000,64000000,63,1000000,65535};
#endif
}
static void capture(uint16_t captured,uint16_t now,uint8_t overflow,uint8_t overrun)
{
    uint64_t t=upper+captured;
    if (overflow && captured<=now) t+=65536U;
    if (overrun) ++pps.overruns;
    if (pps.pulses) {
        uint64_t dt=t-pps.last_capture_us;
        pps.last_period_us=dt>UINT32_MAX ? UINT32_MAX : (uint32_t)dt;
        if (!overrun && dt>=800000U && dt<=1200000U) {
            ++pps.periods; pps.sum_period_us+=dt;
            if (!pps.min_period_us || dt<pps.min_period_us) pps.min_period_us=(uint32_t)dt;
            if (dt>pps.max_period_us) pps.max_period_us=(uint32_t)dt;
        } else ++pps.rejected_periods;
    }
    ++pps.pulses; pps.last_capture_us=t;
    pulses[(pps.pulses-1U)%4U]=(PPS_Capture){t,pps.pulses,overrun};
}
#ifdef LAPTUNE_HOST_TEST
void Timebase_TestCapture(uint16_t c,uint16_t n,uint8_t p,uint8_t o) { capture(c,n,p,o); }
#endif
void Timebase_OnOverflow(void)
{
#ifndef LAPTUNE_HOST_TEST
    uint32_t flags=TIM2->SR;
    if ((TIM2->DIER&TIM_DIER_CC1IE) && (TIM2->CCER&TIM_CCER_CC1E) &&
        (TIM2->CCMR1&TIM_CCMR1_CC1S) && (flags&TIM_SR_CC1IF)) {
        uint16_t c=(uint16_t)TIM2->CCR1,n=(uint16_t)TIM2->CNT;
        capture(c,n,!!(flags&TIM_SR_UIF),!!(flags&TIM_SR_CC1OF));
    }
    TIM2->SR=~(flags&(TIM_SR_UIF|TIM_SR_CC1IF|TIM_SR_CC1OF));
    if (flags&TIM_SR_UIF) upper+=65536U;
#else
    if (pending) { pending=0; upper+=65536U; }
#endif
}
uint64_t Timebase_NowUs(void)
{
    uint32_t m=lock(); uint64_t value=upper; uint16_t low=read_counter();
    if (overflow_pending()) { value+=65536U; low=read_counter(); }
    unlock(m); return value+low;
}
PPS_Diagnostics Timebase_PPSSnapshot(void)
{ uint32_t m=lock(); PPS_Diagnostics d=pps; unlock(m); return d; }

uint8_t Timebase_PPSBefore(uint64_t at,PPS_Capture *out)
{
    uint32_t m=lock(); uint8_t found=0;
    for (unsigned i=0;i<4;++i) {
        const PPS_Capture *p=&pulses[i];
        if (p->seq && p->t_us<=at && (!found || p->t_us>out->t_us)) { *out=*p; found=1; }
    }
    unlock(m); return found;
}
uint8_t Timebase_PPSGet(uint32_t seq,PPS_Capture *out)
{
    uint32_t m=lock(); *out=pulses[(seq-1U)%4U]; uint8_t found=seq && out->seq==seq;
    unlock(m); return found;
}
