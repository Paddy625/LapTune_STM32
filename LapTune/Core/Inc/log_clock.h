#ifndef LAPTUNE_LOG_CLOCK_H
#define LAPTUNE_LOG_CLOCK_H
#include "gps_nmea.h"
typedef struct {
    uint16_t year, millisecond;
    uint8_t month, day, hour, minute, second, valid;
} LogTime;
LogTime LogClock_AtUs(uint64_t sample_us);
LogTime LogClock_ShiftUs(LogTime base,int64_t elapsed_us);
void LogClock_UseNMEA(void);
void LogClock_Update(const GPS_Data *data);
#endif
