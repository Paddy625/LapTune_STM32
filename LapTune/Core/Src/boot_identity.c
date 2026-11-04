#include "boot_identity.h"
#include <stdio.h>
#ifndef LAPTUNE_HOST_TEST
#include "stm32f1xx_hal.h"
#define FLASH_BASE_ADDRESS 0x0801F800UL
static uint16_t BootFlash_Read(unsigned offset) { return *(volatile uint16_t*)(FLASH_BASE_ADDRESS+offset); }
static uint8_t BootFlash_Program(unsigned offset,uint16_t value)
{ return HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,FLASH_BASE_ADDRESS+offset,value)==HAL_OK; }
static uint8_t BootFlash_Erase(unsigned page)
{
    FLASH_EraseInitTypeDef erase={.TypeErase=FLASH_TYPEERASE_PAGES,.PageAddress=FLASH_BASE_ADDRESS+page*1024U,.NbPages=1};
    uint32_t error; return HAL_FLASHEx_Erase(&erase,&error)==HAL_OK;
}
#endif
static uint64_t boot_counter;
static uint8_t gps_restore;
static char directory[48];
static uint32_t checksum(uint64_t n)
{
    uint32_t crc=0xFFFFFFFFU;
    for (unsigned i=0;i<8;++i) {
        crc^=(uint8_t)(n>>(8U*i));
        for (unsigned b=0;b<8;++b) crc=(crc>>1)^((crc&1U)?0xEDB88320U:0);
    }
    return ~crc;
}
static uint8_t erased(unsigned at)
{
    for (unsigned i=0;i<16;i+=2) if (BootFlash_Read(at+i)!=0xFFFF) return 0;
    return 1;
}
uint8_t BootIdentity_Init(void)
{
    uint64_t best=0; unsigned best_at=0; uint8_t restore_reserved=0;
    gps_restore=0;
    for (unsigned at=0;at<2048;at+=16) {
        if (BootFlash_Read(at+14)!=0x4C54) continue;
        uint64_t n=0; uint32_t crc=0;
        for (unsigned i=0;i<4;++i) n|=(uint64_t)BootFlash_Read(at+2*i)<<(16*i);
        crc=BootFlash_Read(at+8)|((uint32_t)BootFlash_Read(at+10)<<16);
        if (crc==checksum(n)) {
            /* Old firmware left this halfword erased. Scan all committed
               records so a temporary downgrade does not immediately rearm. */
            if (BootFlash_Read(at+12)==0x4752) restore_reserved=1;
            if (n>best) { best=n; best_at=at; }
        }
    }
    if (best==UINT64_MAX) return 0;
    unsigned page=best_at/1024U,at=page*1024U;
    while (at<(page+1U)*1024U && !erased(at)) at+=16;
#ifndef LAPTUNE_HOST_TEST
    if (HAL_FLASH_Unlock()!=HAL_OK) return 0;
#endif
    uint8_t ok=1;
    if (at==(page+1U)*1024U) { page^=1; at=page*1024U; ok=BootFlash_Erase(page); }
    uint64_t n=best+1U; uint32_t crc=checksum(n);
    for (unsigned i=0;i<4 && ok;++i) ok=BootFlash_Program(at+2*i,(uint16_t)(n>>(16*i)));
    if (ok) ok=BootFlash_Program(at+8,(uint16_t)crc);
    if (ok) ok=BootFlash_Program(at+10,(uint16_t)(crc>>16));
    if (ok) ok=BootFlash_Program(at+12,0x4752);
    /* Commit last: an interrupted reservation is never exposed as a boot ID. */
    if (ok) ok=BootFlash_Program(at+14,0x4C54);
#ifndef LAPTUNE_HOST_TEST
    (void)HAL_FLASH_Lock();
    uint32_t a=HAL_GetUIDw0(),b=HAL_GetUIDw1(),c=HAL_GetUIDw2();
#else
    uint32_t a=1,b=2,c=3;
#endif
    if (!ok) return 0;
    gps_restore=!restore_reserved;
    boot_counter=n;
    (void)snprintf(directory,sizeof(directory),"S%08lX%08lX%08lX-%08lX%08lX",
        (unsigned long)a,(unsigned long)b,(unsigned long)c,(unsigned long)(n>>32),(unsigned long)(uint32_t)n);
    return 1;
}
const char *BootIdentity_Directory(void) { return directory; }
uint64_t BootIdentity_Counter(void) { return boot_counter; }
uint8_t BootIdentity_TakeGPSRestore(void)
{ uint8_t requested=gps_restore; gps_restore=0; return requested; }
