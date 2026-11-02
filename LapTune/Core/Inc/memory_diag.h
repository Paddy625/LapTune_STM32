#ifndef LAPTUNE_MEMORY_DIAG_H
#define LAPTUNE_MEMORY_DIAG_H
#include <stdint.h>
void MemoryDiag_Init(void);
uint32_t MemoryDiag_StackFreeMin(void);
uint32_t MemoryDiag_StaticBytes(void);
uint32_t MemoryDiag_StackReserved(void);
uint32_t MemoryDiag_HeapUsed(void);
#endif
