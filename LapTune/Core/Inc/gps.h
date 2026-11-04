#ifndef LAPTUNE_GPS_H
#define LAPTUNE_GPS_H

#include "stm32f1xx_hal.h"
#include "gps_nmea.h"
#include "gps_epoch.h"

#define GPS_BOOT_BAUD 9600U
#define GPS_RUN_BAUD 38400U
#define GPS_MEASUREMENT_MS 200U
typedef enum { GPS_CONFIG_PENDING, GPS_CONFIG_READY, GPS_CONFIG_ERROR } GPS_ConfigState;
typedef enum { GPS_CFG_OK, GPS_CFG_NO_RESPONSE, GPS_CFG_NAK, GPS_CFG_VERIFY, GPS_CFG_UART } GPS_ConfigError;
typedef struct {
    GPS_ConfigState state;
    GPS_ConfigError error;
    uint32_t baud;
    uint16_t measurement_ms,nav_rate,time_ref;
    uint8_t message_id,attempts;
    uint8_t pps_state,pps_error; /* pending=0, verified=1, unavailable/error=2 */
    uint8_t acquisition_only; /* legacy GGA/RMC startup; UBX/PPS enabled after first fix */
} GPS_ConfigStatus;

HAL_StatusTypeDef GPS_Init(UART_HandleTypeDef *uart);
void GPS_OnRxComplete(UART_HandleTypeDef *uart);
void GPS_OnTxComplete(UART_HandleTypeDef *uart);
void GPS_DiagnosticsSnapshot(GPS_Diagnostics *out);
void GPS_OnError(UART_HandleTypeDef *uart);
/* Main loop only: drain IRQ queue, assemble UBX epochs and expire stale fields. */
void GPS_Process(uint32_t now);
const GPS_Data *GPS_GetData(void);
/* READY means UART, UBX/NMEA message rates and CFG-RATE were read back successfully. */
const GPS_ConfigStatus *GPS_GetConfig(void);
const char *GPS_SoftwareVersion(void);
const char *GPS_HardwareVersion(void);

#endif
