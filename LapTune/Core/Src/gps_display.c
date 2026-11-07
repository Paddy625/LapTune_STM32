#include "gps_display.h"
#include <stdio.h>
#include <string.h>

HAL_StatusTypeDef GPSDisplay_Update(uint32_t now, const GPS_Data *data,const GPS_ConfigStatus *config)
{
    static uint32_t last_refresh;
    static uint8_t started,pending,row,has_config;
    static GPS_Data snapshot;
    static GPS_ConfigStatus config_snapshot;
    if (!pending) {
        if (started && (uint32_t)(now-last_refresh)<1000U) return HAL_OK;
        snapshot=*data; config_snapshot=(GPS_ConfigStatus){0}; has_config=config!=NULL;
        if (has_config) config_snapshot=*config;
        last_refresh=now; started=pending=1; row=0;
    }
    /* One row per foreground slice; the six rows retain one coherent snapshot. */
    char text[32],padded[26]; uint8_t valid=1;
    switch (row) {
    case 0:
        if (has_config && config_snapshot.restore_state>=GPS_RESTORE_PENDING &&
            config_snapshot.restore_state<=GPS_RESTORE_COLD_SENT) {
            (void)snprintf(text,sizeof(text),"GPS RESTORE %u/3",config_snapshot.restore_state);
            valid=0; break;
        }
        if (!snapshot.connected) (void)snprintf(text,sizeof(text),"GPS NO DATA");
        else (void)snprintf(text,sizeof(text),"GPS %s S:%02u",snapshot.position_valid ? "FIX" : "NO FIX",snapshot.satellites);
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
        if (valid) (void)snprintf(text, sizeof(text), snapshot.time_sync_state ? "UTC %02u:%02u:%02u" : "UTC %02u:%02u:%02u PPS?",
                                 snapshot.hour, snapshot.minute, snapshot.second);
        else (void)snprintf(text, sizeof(text), snapshot.connected ? "UTC WAIT" : "UTC --:--:--");
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
                                      valid ? TFT_CYAN : TFT_BLUE, TFT_BLACK);
    if (status!=HAL_OK) return status;
    if (++row==6U) pending=0;
    return HAL_OK;
}
