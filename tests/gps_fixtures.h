#ifndef GPS_FIXTURES_H
#define GPS_FIXTURES_H
#include <stdint.h>
#include <string.h>
static inline void put32(uint8_t *p,uint32_t v)
{ for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static inline void nav_payloads(uint32_t itow,uint8_t pos[28],uint8_t vel[36],uint8_t sol[52])
{
    memset(pos,0,28); memset(vel,0,36); memset(sol,0,52);
    put32(pos,itow); put32(vel,itow); put32(sol,itow);
    put32(pos+4,215678901); put32(pos+8,521234567);
    put32(pos+12,165456); put32(pos+16,123456); /* Ellipsoid deliberately differs. */
    put32(vel+20,1234); sol[10]=3; sol[11]=0x0D; sol[47]=9;
}
#endif
