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
typedef enum { GPS_QUERY_OK, GPS_QUERY_TIMEOUT, GPS_QUERY_NAK,
    GPS_QUERY_MALFORMED, GPS_QUERY_TRUNCATED, GPS_QUERY_TX_ERROR, GPS_QUERY_SKIPPED, GPS_QUERY_VERIFY_FAILED } GPS_QueryStatus;
typedef enum { GPS_MASK_NO_BASELINE, GPS_MASK_READY, GPS_MASK_ALREADY, GPS_MASK_UNEXPECTED,
    GPS_MASK_SENT, GPS_MASK_ACKED, GPS_MASK_REJECTED, GPS_MASK_VERIFIED, GPS_MASK_VERIFY_FAILED } GPS_MaskState;
typedef struct {
    const char *name;
    const uint8_t *payload; /* Immutable until GPS_QuerySaved; never owned by DMA. */
    uint32_t t_ms;
    const uint8_t *tx_frame;
    uint16_t length,stored,index,total;
    uint8_t attempts,query_class,query_id,rx_class,rx_id,ck_a,ck_b;
    uint8_t tx_length,rx_present,cycle;
    GPS_MaskState mask_state;
    GPS_QueryStatus status;
} GPS_QueryResult;
const GPS_QueryResult *GPS_QueryPending(void);
void GPS_QuerySaved(void); /* Only after successful report close/sync. */
unsigned GPS_QueryProgress(void);
unsigned GPS_QueryCount(void);
unsigned GPS_ConfigQueryCount(void);
void GPS_OnRxComplete(UART_HandleTypeDef *uart);
void GPS_OnTxComplete(UART_HandleTypeDef *uart);
void GPS_DiagnosticsSnapshot(GPS_Diagnostics *out);
void GPS_OnError(UART_HandleTypeDef *uart);
/* Main loop: legacy GGA/RMC receive path. Full public configuration polls and one verified UART1 mask edit. */
void GPS_Process(uint32_t now);
const GPS_Data *GPS_GetData(void);
/* State stays PASSIVE: legacy restore fields are unused; query results are separate. */
const GPS_ConfigStatus *GPS_GetConfig(void);
const char *GPS_SoftwareVersion(void);
const char *GPS_HardwareVersion(void);

#endif
