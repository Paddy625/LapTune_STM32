#ifndef LAPTUNE_TIMEBASE_H
#define LAPTUNE_TIMEBASE_H
#include <stdint.h>
/* Nominal MCU microseconds (HSI PLL), never disciplined/reset by GPS or files. */
void Timebase_Init(void);
uint64_t Timebase_NowUs(void);
void Timebase_OnOverflow(void);
#ifdef LAPTUNE_HOST_TEST
void Timebase_TestSet(uint16_t counter,uint8_t pending);
#endif
#endif
