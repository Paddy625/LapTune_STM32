#ifndef LAPTUNE_LOG_CLOCK_H
#define LAPTUNE_LOG_CLOCK_H
#include "gps_nmea.h"
typedef struct {
    uint16_t year, millisecond;
    uint8_t month, day, hour, minute, second, valid;
} LogTime;
void LogClock_Update(const GPS_Data *data);
LogTime LogClock_At(uint32_t sample_ms);
#endif
