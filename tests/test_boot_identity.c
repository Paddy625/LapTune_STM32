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
    for (unsigned n=1;n<=130;++n) { assert(BootIdentity_Init()); assert(BootIdentity_Counter()==n); }
    char old[48]; strcpy(old,BootIdentity_Directory());
    for (int cut=0;cut<7;++cut) {
        writes=cut; assert(!BootIdentity_Init()); assert(!strcmp(old,BootIdentity_Directory()));
        writes=-1; assert(BootIdentity_Init()); assert(BootIdentity_Counter()==(uint64_t)(131+cut));
        strcpy(old,BootIdentity_Directory());
    }
    assert(strlen(old)==42);
    puts("PASS boot identity: page rollover, CRC/commit journal, cut at each program operation");
}
