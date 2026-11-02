#include "memory_diag.h"
#ifndef LAPTUNE_HOST_TEST
#include "stm32f1xx_hal.h"
extern uint32_t _estack,_Min_Stack_Size,_ebss;
static uint32_t *bottom,*paint_end,min_free;
void MemoryDiag_Init(void)
{
    bottom=(uint32_t*)((uintptr_t)&_estack-(uintptr_t)&_Min_Stack_Size);
    paint_end=(uint32_t*)((__get_MSP()-64U)&~3U); /* Never paint live frames. */
    for (uint32_t *p=bottom;p<paint_end;++p) *p=0xA55A3CC3U;
    min_free=(uint32_t)((uintptr_t)paint_end-(uintptr_t)bottom);
}
uint32_t MemoryDiag_StackFreeMin(void)
{
    if (!bottom) return 0;
    uint32_t *p=bottom;
    while (p<paint_end && *p==0xA55A3CC3U) ++p;
    uint32_t free=(uint32_t)((uintptr_t)p-(uintptr_t)bottom);
    if (free<min_free) min_free=free;
    return min_free;
}
uint32_t MemoryDiag_StaticBytes(void) { return (uint32_t)(uintptr_t)&_ebss-0x20000000U; }
uint32_t MemoryDiag_StackReserved(void) { return (uint32_t)(uintptr_t)&_Min_Stack_Size; }
#else
void MemoryDiag_Init(void) {}
uint32_t MemoryDiag_StackFreeMin(void) { return 0; }
uint32_t MemoryDiag_StaticBytes(void) { return 0; }
uint32_t MemoryDiag_StackReserved(void) { return 0; }
uint32_t MemoryDiag_HeapUsed(void) { return 0; }
#endif
