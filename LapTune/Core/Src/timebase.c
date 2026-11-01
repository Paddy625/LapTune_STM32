#include "timebase.h"
#ifndef LAPTUNE_HOST_TEST
#include "stm32f1xx_hal.h"
#else
static uint16_t counter;
static uint8_t pending;
void Timebase_TestSet(uint16_t c,uint8_t p) { counter=c; pending=p; }
#endif
static volatile uint64_t upper;
void Timebase_Init(void)
{
    upper=0;
#ifndef LAPTUNE_HOST_TEST
    __HAL_RCC_TIM2_CLK_ENABLE();
    uint32_t clock=HAL_RCC_GetPCLK1Freq();
    if (RCC->CFGR&RCC_CFGR_PPRE1) clock*=2U;
    TIM2->CR1=0; TIM2->PSC=clock/1000000U-1U; TIM2->ARR=0xFFFFU;
    TIM2->EGR=TIM_EGR_UG; TIM2->SR=0; TIM2->CNT=0; TIM2->DIER=TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM2_IRQn,0,0); HAL_NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1=TIM_CR1_CEN;
#else
    counter=0; pending=0;
#endif
}
void Timebase_OnOverflow(void)
{
#ifndef LAPTUNE_HOST_TEST
    if (TIM2->SR&TIM_SR_UIF) { TIM2->SR=~TIM_SR_UIF; upper+=65536U; }
#else
    if (pending) { pending=0; upper+=65536U; }
#endif
}
uint64_t Timebase_NowUs(void)
{
#ifndef LAPTUNE_HOST_TEST
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    uint64_t value=upper; uint16_t low=(uint16_t)TIM2->CNT;
    /* An overflow can precede its ISR, including inside a higher-priority ISR. */
    if (TIM2->SR&TIM_SR_UIF) { value+=65536U; low=(uint16_t)TIM2->CNT; }
    __set_PRIMASK(mask);
#else
    uint64_t value=upper; uint16_t low=counter;
    if (pending) value+=65536U;
#endif
    return value+low;
}
