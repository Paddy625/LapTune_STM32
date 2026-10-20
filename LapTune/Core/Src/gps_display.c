#include "gps_display.h"
#include <stdio.h>
#include <string.h>

HAL_StatusTypeDef GPSDisplay_Update(uint32_t now, const GPS_Data *data)
{
    static GPS_Data snapshot;
    static uint32_t last_refresh;
    static uint8_t row, pending, started;
    char text[32], padded[26];
    uint8_t valid = 1;
    if (!pending) {
        if (started && (uint32_t)(now-last_refresh) < 500U) return HAL_OK;
        snapshot = *data; last_refresh = now; row = 0; pending = started = 1;
    }
    switch (row) {
    case 0:
        if (!snapshot.connected) (void)snprintf(text, sizeof(text), "GPS NO DATA");
        else (void)snprintf(text, sizeof(text), "GPS %s S:%02u",
                           snapshot.position_valid ? "FIX" : "NO FIX", snapshot.satellites);
        valid = snapshot.position_valid;
        break;
    case 1:
    case 2: {
        int32_t coord = row == 1U ? snapshot.latitude_e7 : snapshot.longitude_e7;
        uint32_t magnitude = (uint32_t)(coord < 0 ? -coord : coord);
        valid = snapshot.position_valid;
        if (valid) (void)snprintf(text, sizeof(text), "%s %c%0*lu.%07lu",
                                 row == 1U ? "LAT" : "LON", coord < 0 ? '-' : '+',
                                 row == 1U ? 2 : 3, (unsigned long)(magnitude/10000000U),
                                 (unsigned long)(magnitude%10000000U));
        else (void)snprintf(text, sizeof(text), "%s ---", row == 1U ? "LAT" : "LON");
        break;
    }
    case 3: {
        int32_t dm = (snapshot.altitude_mm + (snapshot.altitude_mm < 0 ? -50 : 50))/100;
        uint32_t magnitude = (uint32_t)(dm < 0 ? -dm : dm);
        valid = snapshot.altitude_valid;
        if (valid) (void)snprintf(text, sizeof(text), "ALT %c%lu.%lu M",
                                 dm < 0 ? '-' : '+', (unsigned long)(magnitude/10U),
                                 (unsigned long)(magnitude%10U));
        else (void)snprintf(text, sizeof(text), "ALT --- M");
        break;
    }
    case 4:
        valid = snapshot.datetime_valid;
        if (valid) (void)snprintf(text, sizeof(text), "UTC %02u:%02u:%02u",
                                 snapshot.hour, snapshot.minute, snapshot.second);
        else (void)snprintf(text, sizeof(text), "UTC --:--:--");
        break;
    default:
        valid = snapshot.datetime_valid;
        if (valid) (void)snprintf(text, sizeof(text), "DATE %04u-%02u-%02u",
                                 snapshot.year, snapshot.month, snapshot.day);
        else (void)snprintf(text, sizeof(text), "DATE ----/--/--");
        break;
    }
    /* Fixed-width background clears digits remaining from longer old values. */
    memset(padded, ' ', sizeof(padded)-1U); padded[sizeof(padded)-1U] = '\0';
    size_t len = strlen(text);
    if (len > sizeof(padded)-1U) len = sizeof(padded)-1U;
    memcpy(padded, text, len);
    HAL_StatusTypeDef status = TFT_Text(4, 72U + row*8U, padded, 1,
                                      valid ? TFT_CYAN : TFT_GREY, TFT_BLACK);
    if (++row == 6U) pending = 0;
    return status;
}
