#include "gps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static UART_HandleTypeDef uart;
static uint8_t *target;
static uint32_t tick,baud;
static unsigned transmissions,defaults,cold,aborts;
static int missing,nak,wrong_rate,wrong_msg,fail_tx,hold_tx,keep_baud,short_reply;
static const uint8_t *held; static uint8_t saved[21]; static unsigned held_len;
uint64_t Timebase_NowUs(void) { return (uint64_t)tick*1000; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t n)
{ assert(u==&uart && n==1); target=p; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u) { assert(u==&uart); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *u)
{ assert(u==&uart && (u->Init.BaudRate==9600 || u->Init.BaudRate==38400)); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u)
{ assert(u==&uart); ++aborts; return HAL_OK; }
static void reply(uint8_t cls,uint8_t id,const uint8_t *p,unsigned n)
{
    uint8_t f[32]={0xB5,0x62,cls,id,n,0},a=0,b=0;
    memcpy(f+6,p,n);
    for(unsigned i=2;i<6+n;++i) { a+=f[i]; b+=a; }
    f[6+n]=a; f[7+n]=b;
    for(unsigned i=0;i<n+8;++i) { *target=f[i]; GPS_OnRxComplete(&uart); }
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u,const uint8_t *f,uint16_t n)
{
    assert(u==&uart && n>=8 && f[0]==0xB5 && f[1]==0x62 && f[2]==6);
    unsigned len=f[4]; assert(n==len+8 && !f[5]);
    uint8_t a=0,b=0; for(unsigned i=2;i<n-2U;++i) { a+=f[i]; b+=a; }
    assert(f[n-2]==a && f[n-1]==b); ++transmissions;
    if(fail_tx) return HAL_ERROR;
    if(hold_tx) { held=f; held_len=n; memcpy(saved,f,n); return HAL_OK; }
    GPS_OnTxComplete(u);
    if(missing || u->Init.BaudRate!=baud) return HAL_OK;
    const uint8_t *p=f+6;
    switch(f[3]) {
    case 0: {
        assert(len==1 && p[0]==1); /* Never set a port. */
        uint8_t port[20]={1}; port[14]=3;
        for(unsigned i=0;i<4;++i) port[8+i]=baud>>(8*i);
        reply(6,0,port,20); break;
    }
    case 9: {
        const uint8_t expected[]={0x1F,6,0,0,0,0,0,0,0x1F,6,0,0,0x17};
        assert(len==sizeof(expected) && !memcmp(p,expected,len)); ++defaults;
        if(nak) { const uint8_t no[]={6,9}; reply(5,0,no,2); }
        else if(!keep_baud) baud=9600; /* ACK lost during port change. */
        break;
    }
    case 4: {
        const uint8_t expected[]={255,255,1,0};
        assert(defaults==1 && len==4 && !memcmp(p,expected,4)); ++cold; baud=9600; break;
    }
    case 8: {
        assert(!len); /* Never set rate, including after fix or outage. */
        const uint8_t rate[]={wrong_rate ? 200 : 0xE8,wrong_rate ? 0 : 3,1,0,1,0};
        reply(6,8,rate,6); break;
    }
    case 1: {
        assert(len==2 && p[0]==0xF0 && p[1]<6); /* Poll only, no MSG setters. */
        uint8_t msg[]={p[0],p[1],0,wrong_msg ? 0 : 1,0,0,0,0};
        if(short_reply) { msg[2]=msg[3]; reply(6,1,msg,3); } else reply(6,1,msg,8);
        break;
    }
    default: assert(!"Unexpected GPS transmission in rollback");
    }
    return HAL_OK;
}
static void step(void) { GPS_Process(tick); tick+=5; }
static void run(unsigned ms) { uint32_t start=tick; while((uint32_t)(tick-start)<ms) step(); }
static void until(GPS_ConfigState state)
{ uint32_t start=tick; while(GPS_GetConfig()->state!=state) { step(); assert((uint32_t)(tick-start)<10000); } }
static void reset(uint32_t receiver_baud,uint32_t start)
{
    tick=start; baud=receiver_baud; transmissions=defaults=cold=aborts=0;
    missing=nak=wrong_rate=wrong_msg=fail_tx=hold_tx=keep_baud=short_reply=0;
    held=NULL; uart.Init.BaudRate=9600; assert(GPS_Init(&uart)==HAL_OK);
}
int main(void)
{
    reset(9600,0); run(900000); assert(!transmissions && GPS_GetConfig()->state==GPS_CONFIG_PASSIVE);
    for(unsigned mode=0;mode<4;++mode) {
        reset(mode&1 ? 38400 : 9600,mode==3 ? UINT32_MAX-1000U : 0);
        keep_baud=mode&1; short_reply=mode&2;
        GPS_RequestRestore(); GPS_RequestRestore(); until(GPS_CONFIG_READY);
        assert(defaults==1 && cold==1 && GPS_GetConfig()->measurement_ms==1000 && baud==9600);
        assert(GPS_GetConfig()->restore_state==GPS_RESTORE_RECONFIGURED);
        unsigned count=transmissions; GPS_RequestRestore(); run(900000);
        assert(transmissions==count); /* NO FIX for 15 minutes: no TX or restart. */
        missing=1; run(4000); missing=0; run(4000); assert(transmissions==count);
        assert(GPS_Init(&uart)==HAL_OK); run(4000); assert(transmissions==count);
    }
    reset(38400,0); nak=1; GPS_RequestRestore(); until(GPS_CONFIG_ERROR);
    assert(defaults==1 && !cold && GPS_GetConfig()->restore_error==GPS_CFG_NAK);
    unsigned count=transmissions; run(20000); assert(count==transmissions);
    reset(9600,0); missing=1; GPS_RequestRestore(); until(GPS_CONFIG_ERROR);
    assert(!defaults && !cold); missing=0; count=transmissions; run(20000); assert(count==transmissions);
    reset(9600,0); wrong_rate=1; GPS_RequestRestore(); until(GPS_CONFIG_ERROR);
    assert(GPS_GetConfig()->restore_error==GPS_CFG_VERIFY);
    reset(9600,0); wrong_msg=1; GPS_RequestRestore(); until(GPS_CONFIG_ERROR);
    assert(GPS_GetConfig()->restore_error==GPS_CFG_VERIFY);
    reset(9600,0); fail_tx=1; GPS_RequestRestore(); until(GPS_CONFIG_ERROR);
    assert(GPS_GetConfig()->restore_error==GPS_CFG_UART && !defaults);
    reset(9600,0); hold_tx=1; GPS_RequestRestore(); step(); run(50);
    assert(held && !memcmp(held,saved,held_len)); run(100);
    assert(aborts==1 && GPS_GetConfig()->state==GPS_CONFIG_ERROR);
    count=transmissions; run(20000); assert(count==transmissions);
    puts("PASS GPS rollback: passive UART, factory masks, both baud rates, readback, no retry/reset loop, persistent TX buffer, failures and tick wrap");
}
