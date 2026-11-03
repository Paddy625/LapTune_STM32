#ifndef LAPTUNE_GPS_TIME_H
#define LAPTUNE_GPS_TIME_H
#include "log_clock.h"
#include <stdint.h>
typedef struct {
    uint32_t timtp_packets,rejected,matched,locks,losses,period_ticks,period_change_ticks;
    uint8_t state;
} GPS_TimeDiagnostics;
void GPS_Time_Init(void);
void GPS_Time_Reset(void);
void GPS_Time_Service(uint64_t now_us);
void GPS_Time_PulseMessage(const uint8_t *payload,unsigned length,uint64_t rx_us);
void GPS_Time_Navigation(uint16_t week,uint32_t itow_ms,uint64_t rx_us);
void GPS_Time_Utc(uint32_t itow_ms,const GPS_Data *utc,uint64_t rx_us);
uint8_t GPS_Time_Map(uint16_t week,uint32_t itow_ms,uint64_t rx_us,uint64_t *measurement_us);
uint8_t GPS_Time_UTCAt(uint64_t sample_us,LogTime *utc);
GPS_TimeDiagnostics GPS_Time_Diagnostics(void);
#endif
