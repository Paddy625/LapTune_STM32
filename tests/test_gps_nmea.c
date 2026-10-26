#include "gps_nmea.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t parse(GPS_Data *data, const char *body, uint32_t now)
{
    char line[256]; unsigned checksum = 0;
    for (const char *p = body; *p; ++p) checksum ^= (unsigned char)*p;
    snprintf(line, sizeof(line), "$%s*%02X\r\n", body, checksum);
    return GPS_NMEA_Parse(data, line, now);
}
int main(void)
{
    GPS_Data data;
    GPS_NMEA_Init(&data);
    assert(!data.connected && !data.position_valid && !data.datetime_valid);
    assert(GPS_NMEA_Parse(&data, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", 100));
    assert(data.latitude_e7 == 481173000 && data.longitude_e7 == 115166667);
    assert(data.altitude_mm == 545400 && data.altitude_valid && data.satellites == 8);
    assert(!data.datetime_valid);
    assert(parse(&data, "GPRMC,123519.00,A,4807.038,N,01131.000,E,0.0,0.0,201026,,,A", 101));
    assert(data.year == 2026 && data.month == 10 && data.day == 20);
    assert(data.hour == 12 && data.minute == 35 && data.second == 19 && data.datetime_valid);
    assert(parse(&data,"GPRMC,123519.1234,A,4807.038,N,01131.000,E,0,0,201026",101));
    assert(data.millisecond==123);
    assert(parse(&data, "GNGGA,123520,3456.0000,S,05822.0000,W,1,10,1.0,-12.345,M,0,M,,", 102));
    assert(data.latitude_e7 == -349333333 && data.longitude_e7 == -583666667);
    assert(data.altitude_mm == -12345);

    GPS_Data previous = data;
    assert(!GPS_NMEA_Parse(&data, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00", 200));
    assert(!GPS_NMEA_Parse(&data, "$GPRMC,foo*Z0", 200));
    assert(!GPS_NMEA_Parse(&data, "$GPRMC,foo*", 200));
    assert(!GPS_NMEA_Parse(&data, "$GPRMC,foo", 200));
    assert(!GPS_NMEA_Parse(&data, NULL, 200));
    assert(!parse(&data, "GPRMC,250000,A,4807.038,N,01131.000,E,0,0,201026", 200));
    assert(!parse(&data, "GPRMC,123519,A,4807.038,N,01131.000,E,0,0,290225", 200));
    assert(!parse(&data, "GPGGA,123519,4860.000,N,01131.000,E,1,08,1,100,M", 200));
    assert(!parse(&data, "GPGGA,123519,9000.001,N,01131.000,E,1,08,1,100,M", 200));
    assert(!parse(&data, "GPGGA,123519,4807.038,X,01131.000,E,1,08,1,100,M", 200));
    assert(!parse(&data, "GPGGA,123519,4807.038,N,18100.000,E,1,08,1,100,M", 200));
    assert(!parse(&data, "GPGGA,123519,4807.038,N,01131.000,E,1,08,1,100,F", 200));
    assert(!parse(&data, "GPGGA,123519,4807.038,N,01131.000,E,1,08,1,999999999999,M", 200));
    assert(memcmp(&data, &previous, sizeof(data)) == 0);
    assert(parse(&data, "GNRMC,000000,A,9000.000,N,18000.000,E,0,0,290224", 300));
    assert(data.latitude_e7 == 900000000 && data.longitude_e7 == 1800000000);
    assert(data.month == 2 && data.day == 29 && data.year == 2024);
    assert(parse(&data, "GNRMC,000001,A,0000.000,N,00000.000,E,0,0,010324", 400));
    assert(data.latitude_e7 == 0 && data.longitude_e7 == 0 && data.position_valid);
    assert(parse(&data, "GPGGA,000001,,,,,0,00,99.99,,,,,,", 401));
    assert(data.connected && !data.position_valid && !data.altitude_valid);
    assert(parse(&data, "GPRMC,000001,V,,,,,,,010324,,,N", 402));
    assert(!data.datetime_valid);

    GPS_NMEA_Init(&data);
    uint32_t start = UINT32_MAX-1000U;
    assert(parse(&data, "GPGGA,123519,4807.038,N,01131.000,E,1,08,1,100,M", start));
    assert(parse(&data, "GPRMC,123519,A,4807.038,N,01131.000,E,0,0,201026", start+1U));
    GPS_NMEA_Expire(&data, 1998U);
    assert(data.position_valid && data.altitude_valid && data.datetime_valid);
    GPS_NMEA_Expire(&data, 1999U);
    assert(!data.altitude_valid && data.position_valid);
    GPS_NMEA_Expire(&data, 2000U);
    assert(!data.connected && !data.position_valid && !data.datetime_valid);
    puts("PASS NMEA: checksums, GP/GN, coordinates, MSL altitude, UTC/date, no fix, invalid input, expiry/wrap");
}
