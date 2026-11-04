#ifndef LAPTUNE_BOOT_IDENTITY_H
#define LAPTUNE_BOOT_IDENTITY_H
#include <stdint.h>
#define LAPTUNE_FIRMWARE_VERSION "2.1.1"
/* Two reserved 1 KB flash pages, outside the linker's application image. */
uint8_t BootIdentity_Init(void);
const char *BootIdentity_Directory(void);
uint64_t BootIdentity_Counter(void);
#ifdef LAPTUNE_HOST_TEST
uint16_t BootFlash_Read(unsigned offset);
uint8_t BootFlash_Program(unsigned offset,uint16_t value);
uint8_t BootFlash_Erase(unsigned page);
#endif
#endif
