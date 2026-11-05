#ifndef LAPTUNE_GPS_H
#define LAPTUNE_GPS_H

#include "stm32f1xx_hal.h"
#include "gps_nmea.h"
#include "gps_epoch.h"

#define GPS_BOOT_BAUD 9600U
#define GPS_RUN_BAUD 9600U
#define GPS_MEASUREMENT_MS 1000U
typedef enum { GPS_CONFIG_PENDING, GPS_CONFIG_READY, GPS_CONFIG_ERROR, GPS_CONFIG_PASSIVE } GPS_ConfigState;
typedef enum { GPS_CFG_OK, GPS_CFG_NO_RESPONSE, GPS_CFG_NAK, GPS_CFG_VERIFY, GPS_CFG_UART } GPS_ConfigError;
typedef enum { GPS_RESTORE_NONE, GPS_RESTORE_PENDING, GPS_RESTORE_DEFAULTS_SENT,
               GPS_RESTORE_COLD_SENT, GPS_RESTORE_RECONFIGURED, GPS_RESTORE_FAILED } GPS_RestoreState;
typedef struct {
    GPS_ConfigState state;
    GPS_ConfigError error;
    uint32_t baud;
    uint16_t measurement_ms,nav_rate,time_ref;
    uint8_t message_id,attempts;
    uint8_t pps_state,pps_error; /* Legacy metadata field; PPS disabled in the NMEA rollback. */
    uint8_t acquisition_only; /* Legacy metadata field; no post-fix mode transition. */
    uint8_t restore_state,restore_error,restore_ack;
} GPS_ConfigStatus;

HAL_StatusTypeDef GPS_Init(UART_HandleTypeDef *uart);
/* Call once immediately after Init, only with a persistent reservation. */
void GPS_RequestRestore(void);
void GPS_OnRxComplete(UART_HandleTypeDef *uart);
void GPS_OnTxComplete(UART_HandleTypeDef *uart);
void GPS_DiagnosticsSnapshot(GPS_Diagnostics *out);
void GPS_OnError(UART_HandleTypeDef *uart);
/* Main loop: legacy GGA/RMC receive path. No runtime GPS configuration. */
void GPS_Process(uint32_t now);
const GPS_Data *GPS_GetData(void);
/* READY = defaults read back this boot; PASSIVE = receive only, no readback. */
const GPS_ConfigStatus *GPS_GetConfig(void);
const char *GPS_SoftwareVersion(void);
const char *GPS_HardwareVersion(void);

#endif
