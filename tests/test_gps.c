#include "gps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef uart, other;
static uint8_t *target;
static unsigned aborts;
static uint8_t fail_receive;
static const char gga[] = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *handle, uint8_t *ptr, uint16_t size)
{
    assert(handle == &uart && size == 1);
    if (fail_receive) { fail_receive = 0; return HAL_BUSY; }
    target = ptr;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *handle)
{
    assert(handle == &uart); aborts++; return HAL_OK;
}
static void feed(const char *bytes)
{
    for (; *bytes; ++bytes) { assert(target); *target = (uint8_t)*bytes; GPS_OnRxComplete(&uart); }
}
int main(void)
{
    GPS_Process(0); GPS_OnRxComplete(&other); GPS_OnError(&other);
    assert(GPS_Init(NULL) == HAL_ERROR);
    assert(GPS_Init(&uart) == HAL_OK);
    feed("noise$garbage"); feed(gga);
    assert(!GPS_GetData()->connected); /* Parsing must happen in foreground. */
    GPS_Process(100);
    assert(GPS_GetData()->altitude_mm == 545400 && GPS_GetData()->altitude_valid);
    feed("$GPGGA,partial"); GPS_Process(101);
    feed(gga); GPS_Process(102);
    assert(GPS_GetData()->position_ms == 102);
    feed("$GPGGA,123519*00\r\n"); GPS_Process(103);
    assert(GPS_GetData()->position_ms == 102);
    char long_line[220]; memset(long_line, 'x', sizeof(long_line)-1); long_line[sizeof(long_line)-1] = 0;
    feed("$"); feed(long_line); feed("\n"); feed(gga); GPS_Process(104);
    assert(GPS_GetData()->position_ms == 104);
    for (unsigned i=0; i<600; ++i) feed("x");
    GPS_Process(105); feed(gga); GPS_Process(106);
    assert(GPS_GetData()->position_ms == 106);
    GPS_OnError(&other); GPS_Process(107); assert(aborts == 0);
    GPS_OnError(&uart); GPS_Process(108); assert(aborts == 1);
    feed(gga); GPS_Process(109); assert(GPS_GetData()->position_ms == 109);
    fail_receive=1; feed("x"); GPS_Process(110); assert(aborts == 2);
    feed(gga); GPS_Process(111); assert(GPS_GetData()->position_ms == 111);
    GPS_Process(3111); assert(!GPS_GetData()->position_valid && !GPS_GetData()->connected);
    puts("PASS GPS UART: IRQ queue, fragmented NMEA, resync, overflow, UART errors/rearm, stale data");
}
