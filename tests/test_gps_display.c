#include "gps_display.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls;
static char last[32];
HAL_StatusTypeDef TFT_Text(uint16_t x, uint16_t y, const char *text,
                         uint8_t scale, uint16_t fg, uint16_t bg)
{
    (void)fg; (void)bg;
    assert(scale == 1 && strlen(text) == 25 && x + strlen(text)*6 <= TFT_WIDTH);
    assert(y >= 72 && y+8 <= 120);
    assert(y == 72U + (calls%6U)*8U);
    strcpy(last,text); calls++;
    return HAL_OK;
}
int main(void)
{
    GPS_Data data = {.connected=1, .position_valid=1, .altitude_valid=1, .datetime_valid=1,
                     .latitude_e7=-349333333, .longitude_e7=-583666667, .altitude_mm=-12345,
                     .year=2026, .month=10, .day=20, .hour=12, .minute=35, .second=19, .satellites=8};
    const char *expected[] = {"GPS FIX S:08", "LAT -34.9333333", "LON -058.3666667", "ALT -12.3 M", "UTC 12:35:19", "DATE 2026-10-20"};
    for (unsigned i=0; i<6; ++i) {
        assert(GPSDisplay_Update(0,&data)==HAL_OK);
        assert(strncmp(last,expected[i],strlen(expected[i]))==0);
        data.latitude_e7 = 0; /* Frame must retain the original snapshot. */
    }
    assert(GPSDisplay_Update(499,&data)==HAL_OK && calls==6);
    memset(&data,0,sizeof(data));
    const char *invalid[] = {"GPS NO DATA", "LAT ---", "LON ---", "ALT --- M", "UTC --:--:--", "DATE ----/--/--"};
    for (unsigned i=0; i<6; ++i) {
        assert(GPSDisplay_Update(500,&data)==HAL_OK);
        assert(strncmp(last,invalid[i],strlen(invalid[i]))==0);
        for (size_t j=strlen(invalid[i]); j<25; ++j) assert(last[j]==' ');
    }
    data.connected=1;
    assert(GPSDisplay_Update(1000,&data)==HAL_OK);
    assert(strncmp(last,"GPS NO FIX S:00",15)==0);
    puts("PASS GPS UI: bounds, snapshot, coordinates/MSL/UTC/date, no data/fix, old digits cleared");
}
