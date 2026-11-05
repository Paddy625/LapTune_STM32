#include "gps.h"
#include "log_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static UART_HandleTypeDef uart,other;
static uint8_t *rx; static uint64_t now; static int fail_rearm;
uint64_t Timebase_NowUs(void) { return now; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *p,uint16_t len)
{ assert(u==&uart && len==1); rx=p; return fail_rearm ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u) { assert(u==&uart); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *u) { assert(u==&uart && u->Init.BaudRate==9600); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *u,const uint8_t *p,uint16_t n)
{ (void)u; (void)p; (void)n; assert(!"Passive GPS must never transmit"); return HAL_ERROR; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u) { assert(u==&uart); return HAL_OK; }
static void feed(uint8_t ch) { *rx=ch; GPS_OnRxComplete(&uart); }
static void sentence(const char *body,int corrupt)
{
    unsigned sum=0; char line[160];
    for(const char *p=body;*p;++p) sum^=(uint8_t)*p;
    snprintf(line,sizeof(line),"$%s*%02X\r\n",body,sum^corrupt);
    for(const char *p=line;*p;++p) feed(*p);
}
static void pair(const char *time)
{
    char body[128];
    snprintf(body,sizeof(body),"GPGGA,%s,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,",time); sentence(body,0);
    snprintf(body,sizeof(body),"GPRMC,%s,A,4807.038,N,01131.000,E,22.4,84.4,041126,,,A",time); sentence(body,0);
}
static void process(void) { GPS_Process(now/1000); }
int main(void)
{
    assert(GPS_Init(NULL)==HAL_ERROR); assert(GPS_Init(&uart)==HAL_OK);
    GPS_OnError(&other); GPS_OnRxComplete(&other); assert(!gps_diag.uart_errors);
    now=100000; pair("123519"); now=600000; process();
    GPS_Fix f; GPS_Epoch_ForSample(100000,&f);
    assert(f.position_valid && f.rx_us==100000 && f.published_us==100000 && f.nmea && f.fix_id==1);
    assert(f.altitude_mm==545400 && f.speed_millikmh==41485 && f.meas_delay_us==UINT32_MAX);
    assert(GPS_GetData()->position_valid && GPS_GetData()->datetime_valid && GPS_GetData()->satellites==8);
    LogTime t=LogClock_AtUs(now+100000); assert(t.valid && t.second==19 && t.millisecond==100);
    GPS_Epoch_ForSample(200000,&f); assert(f.fix_id==1);
    now=700000; pair("123519"); process(); assert(gps_diag.received_epochs==1);
    now=1100000; pair("123520"); process(); GPS_Epoch_ForSample(now,&f);
    assert(f.fix_id==2 && f.position_valid && gps_diag.received_epochs==2);
    /* RMC without GGA in next epoch must not inherit old altitude. */
    now=2100000; sentence("GPRMC,123521,A,4807.038,N,01131.000,E,1.0,84.4,041126,,,A",0); process();
    now=2900000; process(); GPS_Epoch_ForSample(now,&f);
    assert(f.fix_id==3 && f.position_valid && f.speed_valid && !f.altitude_valid);
    now=3100000; sentence("GPGGA,123522,,,,,0,00,99.99,,,,,,",0);
    sentence("GPRMC,123522,V,,,,,,,041126,,,N",0); process(); GPS_Epoch_ForSample(now,&f);
    assert(f.fix_id==4 && !f.position_valid && gps_diag.invalid_fixes==1);
    for(unsigned i=0;i<50;++i) { process(); }
    assert(gps_diag.invalid_fixes==1);
    assert(!GPS_GetData()->position_valid && !LogClock_AtUs(now).valid);
    now=4100000; pair("123523"); process(); assert(GPS_GetData()->position_valid);
    sentence("GPGGA,123523,,,,,0,00,99.99,,,,,,",1); process(); assert(GPS_GetData()->position_valid);
    assert(gps_diag.checksum_errors==1 && gps_diag.checksum_by_phase[1]==1);
    now+=3100000; process(); assert(!GPS_GetData()->position_valid && !GPS_GetData()->connected);
    uart.ErrorCode=HAL_UART_ERROR_FE|HAL_UART_ERROR_ORE; GPS_OnError(&uart); process();
    assert(gps_diag.uart_by_phase[1][0]==1 && gps_diag.uart_by_phase[1][2]==1);
    now+=100000; pair("123527"); process(); assert(GPS_GetData()->position_valid);
    for(unsigned i=0;i<600;++i) { feed('x'); }
    process();
    assert(gps_diag.rx_overflows==1 && !GPS_GetData()->position_valid);
    fail_rearm=1; feed('x'); process(); fail_rearm=0; process();
    now+=1000000; pair("123528"); process(); assert(GPS_GetData()->position_valid);
    puts("PASS legacy GPS: NMEA fix, IRQ reception time, immutable paired epochs, partial/invalid epochs, UTC, checksum rejection, overflow and UART recovery; no TX");
}
