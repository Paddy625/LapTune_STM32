#include "gps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static UART_HandleTypeDef uart;
static uint8_t *rx;
static uint32_t tick;
static unsigned sent,mode,aborts;
static const uint8_t *held; static uint8_t saved[10]; static unsigned held_len;
uint64_t Timebase_NowUs(void) { return (uint64_t)tick*1000; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t n)
{ assert(u==&uart && n==1); rx=p; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u) { assert(u==&uart); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *u) { assert(u==&uart && u->Init.BaudRate==9600); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u) { assert(u==&uart); ++aborts; return HAL_OK; }
static void reply(uint8_t cls,uint8_t id,const uint8_t *p,unsigned n,int corrupt)
{
    uint8_t f[512]={0xB5,0x62,cls,id,n,n>>8},a=0,b=0;
    memcpy(f+6,p,n); for(unsigned i=2;i<6+n;++i) { a+=f[i]; b+=a; }
    f[6+n]=a; f[7+n]=b^corrupt;
    for(unsigned i=0;i<n+8;++i) { *rx=f[i]; GPS_OnRxComplete(&uart); }
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u,const uint8_t *f,uint16_t n)
{
    assert(u==&uart && f[0]==0xB5 && f[1]==0x62 && !f[5] && n==f[4]+8U);
    uint8_t a=0,b=0; for(unsigned i=2;i<n-2U;++i) { a+=f[i]; b+=a; }
    assert(f[n-2]==a && f[n-1]==b); ++sent;
    /* Independent protocol allowlist: not a single setter/reset is permitted. */
    if(f[2]==6) {
        if(f[3]==0) assert(f[4]==1 && f[6]==1);
        else if(f[3]==1) assert(f[4]==2 && (f[6]==0xF0 || f[6]==1 || f[6]==13));
        else assert(!f[4] && (f[3]==8 || f[3]==0x24 || f[3]==0x11 || f[3]==7 || f[3]==0x13));
    } else assert(!f[4] && ((f[2]==10 && (f[3]==4 || f[3]==9)) || (f[2]==1 && (f[3]==3 || f[3]==0x30))));
    if(mode==5) return HAL_ERROR;
    if(mode==6) { held=f; held_len=n; memcpy(saved,f,n); return HAL_OK; }
    GPS_OnTxComplete(u);
    if(mode==1) return HAL_OK;
    if(mode==2) { const uint8_t nak[]={f[2],f[3]}; reply(5,0,nak,2,0); return HAL_OK; }
    if(mode==8) { const uint8_t ack[]={f[2],f[3]}; reply(5,1,ack,2,0); return HAL_OK; }
    if(mode==9) { const uint8_t wrong[]={0xF0,5,1}; reply(6,1,wrong,3,0); return HAL_OK; }
    uint8_t p[420]={0}; unsigned len=0;
    if(f[2]==10) { len=f[3]==4 ? 40 : 68; memcpy(p,"7.03",4); }
    else if(f[2]==1) { len=f[3]==3 ? 16 : 8+32*12; if(f[3]==0x30) p[4]=32; }
    else switch(f[3]) {
    case 0: len=20; p[0]=1; p[8]=0x80; p[9]=0x25; p[12]=7; p[14]=3; break;
    case 1: len=8; p[0]=f[6]; p[1]=f[7]; p[3]=1; break;
    case 8: len=6; p[0]=0xE8; p[1]=3; p[2]=p[4]=1; break;
    case 0x24: len=36; p[3]=3; break;
    case 0x11: len=2; break;
    case 7: len=20; p[8]=p[9]=1; break;
    case 0x13: len=4; break;
    }
    if(mode==3) len=1;
    if(mode==4) len=420;
    reply(f[2],f[3],p,len,mode==7); return HAL_OK;
}
static void step(void) { GPS_Process(tick); tick+=5; }
static void run(unsigned ms) { uint32_t start=tick; while((uint32_t)(tick-start)<ms) step(); }
static void reset(unsigned m,uint32_t start)
{ tick=start; mode=m; sent=aborts=0; held=NULL; assert(GPS_Init(&uart)==HAL_OK); }
static const GPS_QueryResult *pending(void)
{ uint32_t start=tick; while(!GPS_QueryPending()) { step(); assert((uint32_t)(tick-start)<12000); } return GPS_QueryPending(); }
int main(void)
{
    reset(0,0); run(2900); assert(!sent);
    for(unsigned i=1;i<=21;++i) {
        const GPS_QueryResult *r=pending(); assert(r->index==i && r->status==GPS_QUERY_OK && r->attempts==1);
        assert(r->stored==r->length);
        uint8_t copy[400]; memcpy(copy,r->payload,r->stored);
        uint8_t other[40]; memset(other,0x77,sizeof(other)); reply(10,4,other,sizeof(other),0); run(2000);
        assert(!memcmp(copy,r->payload,r->stored)); /* Storage callbacks cannot mutate a pending report. */
        GPS_QuerySaved();
    }
    assert(GPS_QueryProgress()==21 && sent==21 && GPS_GetConfig()->measurement_ms==1000);
    run(900000); assert(sent==21); /* No repeating polls or recovery after completion. */
    reset(1,0);
    for(unsigned i=1;i<=21;++i) { const GPS_QueryResult *r=pending(); assert(r->status==GPS_QUERY_TIMEOUT && r->attempts==2 && !r->stored); GPS_QuerySaved(); }
    assert(sent==42); run(900000); assert(sent==42);
    reset(2,0); assert(pending()->status==GPS_QUERY_NAK);
    reset(3,0); assert(pending()->status==GPS_QUERY_MALFORMED);
    reset(4,0); assert(pending()->status==GPS_QUERY_TRUNCATED && GPS_QueryPending()->length==420 && GPS_QueryPending()->stored==400);
    reset(5,0); assert(pending()->status==GPS_QUERY_TX_ERROR);
    reset(6,0); run(3050); assert(held && !memcmp(saved,held,held_len)); run(100);
    assert(aborts==1 && GPS_QueryPending()->status==GPS_QUERY_TX_ERROR);
    reset(7,0); assert(pending()->status==GPS_QUERY_TIMEOUT && gps_diag.checksum_errors==2);
    reset(8,0); assert(pending()->status==GPS_QUERY_TIMEOUT); /* ACK is not a configuration readback. */
    reset(9,0); assert(pending()->status==GPS_QUERY_TIMEOUT); /* Unrelated reply is ignored. */
    reset(0,UINT32_MAX-1000U); assert(pending()->status==GPS_QUERY_OK);
    puts("PASS read-only GPS: 21 polls, strict TX allowlist, full SVINFO, immutable payload during SD, timeout/NAK/length/checksum/TX failures, bounded retries, wrap");
}
