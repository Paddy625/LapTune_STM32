#ifndef LAPTUNE_GPS_NMEA_H
#define LAPTUNE_GPS_NMEA_H

#include <stdint.h>

#define GPS_FRESH_MS 3000U
#define GPS_NMEA_MAX_LENGTH 128U

typedef struct {
    int32_t latitude_e7, longitude_e7; /* Signed degrees times 10000000. */
    int32_t altitude_mm;              /* GGA altitude above mean sea level. */
    uint32_t speed_millikmh;          /* RMC speed over ground, 0.001 km/h. */
    uint16_t year;
    uint16_t millisecond;
    uint8_t month, day, hour, minute, second;
    uint8_t satellites, fix_quality;
    uint8_t connected, position_valid, altitude_valid, datetime_valid, speed_valid;
    uint32_t sentence_ms, position_ms, altitude_ms, datetime_ms, speed_ms;
} GPS_Data;

void GPS_NMEA_Init(GPS_Data *data);
/* Accept only checksummed GP/GN RMC and GGA; malformed input is ignored. */
uint8_t GPS_NMEA_Parse(GPS_Data *data, const char *line, uint32_t now);
void GPS_NMEA_Expire(GPS_Data *data, uint32_t now);

#endif
