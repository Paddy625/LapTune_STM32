#include "boot_identity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t flash[1024]; static int writes=-1;
uint16_t BootFlash_Read(unsigned at) { assert(at<2048 && at%2==0); return flash[at/2]; }
uint8_t BootFlash_Program(unsigned at,uint16_t v)
{
    if (!writes) return 0;
    if (writes>0) --writes;
    assert((flash[at/2]&v)==v); flash[at/2]&=v; return 1;
}
uint8_t BootFlash_Erase(unsigned page)
{ assert(page<2); memset(flash+page*512,255,1024); return 1; }
int main(void)
{
    memset(flash,255,sizeof(flash));
    for (unsigned n=1;n<=130;++n) {
        assert(BootIdentity_Init()); assert(BootIdentity_Counter()==n);
        assert(BootIdentity_TakeGPSRestore()==(n==1)); assert(!BootIdentity_TakeGPSRestore());
    }
    char old[48]; strcpy(old,BootIdentity_Directory());
    for (int cut=0;cut<8;++cut) {
        writes=cut; assert(!BootIdentity_Init()); assert(!strcmp(old,BootIdentity_Directory()));
        writes=-1; assert(BootIdentity_Init()); assert(BootIdentity_Counter()==(uint64_t)(131+cut));
        assert(!BootIdentity_TakeGPSRestore());
        strcpy(old,BootIdentity_Directory());
    }
    assert(strlen(old)==42);
    /* First migration: old records used 0xFFFF at offset 12. */
    memset(flash,255,sizeof(flash)); assert(BootIdentity_Init()); flash[6]=0xFFFF;
    assert(BootIdentity_Init()); assert(BootIdentity_Counter()==2); assert(BootIdentity_TakeGPSRestore());
    /* Power loss after reservation, before UART: never restart GPS on every boot. */
    assert(BootIdentity_Init()); assert(!BootIdentity_TakeGPSRestore());
    /* A legacy boot after migration must not immediately rearm the repair. */
    flash[16+6]=0xFFFF;
    assert(BootIdentity_Init()); assert(!BootIdentity_TakeGPSRestore());
    for (int cut=0;cut<8;++cut) {
        memset(flash,255,sizeof(flash)); writes=cut; assert(!BootIdentity_Init());
        assert(!BootIdentity_TakeGPSRestore());
        writes=-1; assert(BootIdentity_Init()); assert(BootIdentity_TakeGPSRestore());
        assert(BootIdentity_Init()); assert(!BootIdentity_TakeGPSRestore());
    }
    puts("PASS boot identity: page rollover, CRC/commit journal, cut at each program operation");
}
