#include "gps.h"
#include "gps_fixtures.h"
#include <assert.h>
#include <stdio.h>
static UART_HandleTypeDef uart,other;
static uint8_t *rx; static uint64_t now; static int fail_rearm,hold_tx;
static const uint8_t *held_tx; static uint8_t held_bytes[28]; static unsigned held_length;
uint64_t Timebase_NowUs(void) { return now; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t len)
{ assert(u==&uart && len==1); rx=p; return fail_rearm ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u) { assert(u==&uart); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *u) { assert(u==&uart); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u,const uint8_t *p,uint16_t n)
{ assert(u==&uart && p && n>=8); held_tx=p; held_length=n; memcpy(held_bytes,p,n);
    if (!hold_tx) GPS_OnTxComplete(u);
    return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u) { assert(u==&uart); return HAL_OK; }
static void feed(uint8_t ch) { *rx=ch; GPS_OnRxComplete(&uart); }
static void frame(uint8_t id,const uint8_t *p,unsigned n,int corrupt)
{
    uint8_t f[100]={0xB5,0x62,1,id,(uint8_t)n,0}; memcpy(f+6,p,n);
    uint8_t a=0,b=0; for (unsigned i=2;i<n+6;++i) { a+=f[i]; b+=a; }
    f[n+6]=a; f[n+7]=b^corrupt; for (unsigned i=0;i<n+8;++i) feed(f[i]);
}
static void epoch(uint32_t itow,int corrupt)
{
    uint8_t p[28],v[36],s[52]; nav_payloads(itow,p,v,s);
    frame(2,p,28,0); frame(0x12,v,36,0); frame(6,s,52,corrupt);
}
int main(void)
{
    assert(GPS_Init(NULL)==HAL_ERROR); uart.Init.BaudRate=9600; assert(GPS_Init(&uart)==HAL_OK);
    GPS_OnError(&other); GPS_OnRxComplete(&other); assert(!gps_diag.uart_errors);
    hold_tx=1; GPS_Process(0); assert(held_tx && held_length);
    now=10000; GPS_Process(10); assert(!memcmp(held_tx,held_bytes,held_length));
    GPS_OnTxComplete(&uart); hold_tx=0;
    uart.ErrorCode=HAL_UART_ERROR_FE|HAL_UART_ERROR_NE; now=15000; GPS_OnError(&uart); GPS_Process(15);
    assert(gps_diag.uart_by_phase[0][0]==1 && gps_diag.uart_by_phase[0][1]==1);
    uart.ErrorCode=0;
    now=100000; epoch(1000,0); now=600000; GPS_Process(600);
    GPS_Fix f; GPS_Epoch_ForSample(100000,&f);
    assert(f.position_valid && f.published_us==100000 && GPS_GetData()->position_valid);
    now=700000; epoch(1200,1); GPS_Process(700); assert(gps_diag.checksum_errors==1);
    now=781000; GPS_Process(781); assert(!GPS_GetData()->position_valid && gps_diag.incomplete==1);
    now=900000; epoch(1400,0); GPS_Process(900); assert(GPS_GetData()->position_valid);
    GPS_OnError(&uart); GPS_Process(901); assert(gps_diag.uart_errors==2 && !GPS_GetData()->position_valid);
    now=1100000; epoch(100,0); GPS_Process(1100); assert(GPS_GetData()->position_valid);
    for (unsigned i=0;i<1100;++i) feed('x');
    GPS_Process(1101);
    assert(gps_diag.rx_overflows==1 && !GPS_GetData()->position_valid);
    fail_rearm=1; feed(0xB5); GPS_Process(1102); fail_rearm=0; GPS_Process(1103);
    now=1300000; epoch(300,0); GPS_Process(1300); assert(GPS_GetData()->position_valid);
    puts("PASS GPS UART: IRQ packet timestamps survive delayed parsing, checksums, overflow, recovery, receiver epoch reset");
}
