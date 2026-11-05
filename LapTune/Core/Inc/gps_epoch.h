#ifndef LAPTUNE_GPS_EPOCH_H
#define LAPTUNE_GPS_EPOCH_H
#include "gps_nmea.h"
#define GPS_EPOCH_TIMEOUT_US 800000ULL
#define GPS_ASSEMBLY_DEADLINE_US 180000ULL
typedef struct {
    uint64_t published_us,rx_us;
    int32_t latitude_e7,longitude_e7,altitude_mm;
    uint32_t speed_millikmh,fix_id,itow_ms;
    uint32_t meas_delay_us; /* rx_us - measurement_us; UINT32_MAX when not mapped. */
    uint8_t position_valid:1,speed_valid:1,altitude_valid:1,epoch_known:1;
    uint8_t time_valid:1,time_sync_state:2,discarded_incomplete:1;
    uint8_t nmea; /* Local UTC-of-day epoch ID only; no GPS iTOW or PPS map. */
} GPS_Fix;
enum { GPS_RX_SOL=1, GPS_RX_UTC=2, GPS_RX_SKY=4, GPS_RX_HW=8, GPS_RX_NAV5=16, GPS_RX_POWER=32 };
typedef struct {
    uint64_t sky_rx_us,hw_rx_us;
    uint16_t noise_per_ms,agc_count,pdop_limit;
    int16_t week;
    uint8_t fix_type,sol_flags,used_sats,utc_flags;
    uint8_t sky_channels,cno_nonzero,code_locked,cno_max;
    uint8_t antenna_status,antenna_power,jamming,hw_flags;
    uint8_t dynamic_model,fix_mode,power_mode,seen;
    int8_t min_elevation;
} GPS_ReceiverStatus;
typedef struct {
    uint32_t checksum_errors,uart_errors,rx_overflows,incomplete,late,invalid_fixes,epoch_count,bad_period,history_misses;
    uint32_t min_period_ms,max_period_ms,received_epochs,valid_epochs;
    uint32_t uart_by_phase[2][5]; /* init/reconfigure, runtime; FE,NE,ORE,PE,other */
    uint64_t uart_first_us,uart_last_us,checksum_first_us,checksum_last_us;
    uint64_t uart_phase_first_us[2],uart_phase_last_us[2];
    uint32_t checksum_by_phase[2],tx_errors;
    uint32_t nmea_sentences,nmea_fix_sentences,recovery_attempts;
    uint64_t nmea_last_us,nmea_last_fix_us,recovery_us;
    GPS_ReceiverStatus receiver;
} GPS_Diagnostics;
extern GPS_Diagnostics gps_diag;
void GPS_Epoch_Init(void);
void GPS_Epoch_InitNMEA(void);
/* Called only for a sentence already accepted by the legacy NMEA parser. */
void GPS_Epoch_NMEA(const GPS_Data *data,const char *line,uint64_t rx_us);
void GPS_Epoch_ResetStream(GPS_Data *data,uint64_t now);
void GPS_Epoch_Packet(GPS_Data *data,uint8_t cls,uint8_t id,const uint8_t *payload,unsigned len,uint64_t now);
void GPS_Epoch_Service(GPS_Data *data,uint64_t now);
void GPS_Epoch_ForWindow(uint64_t sample_us,uint64_t midpoint_us,GPS_Fix *fix);
void GPS_Epoch_ForSample(uint64_t sample_us,GPS_Fix *fix);
#endif
