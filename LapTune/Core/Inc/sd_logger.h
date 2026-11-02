#ifndef LAPTUNE_SD_LOGGER_H
#define LAPTUNE_SD_LOGGER_H
#include "acc.h"
#include "log_clock.h"
#include "imu_stream.h"
/* Elapsed STM32 time, independent of GPS reception and UTC corrections. */
#define SD_LOG_FILE_DURATION_MS 180000U
#define SD_LOG_SYNC_INTERVAL_MS 1000U
#define SD_LOG_RETRY_INTERVAL_MS 2000U
typedef enum { LOG_OFF,LOG_RECORDING,LOG_STOPPED,LOG_CARD_ERROR,LOG_FULL,LOG_DATA_LOSS } LogState;
extern volatile LogState sd_log_state;
extern volatile uint32_t sd_log_written, sd_log_dropped;
extern volatile unsigned sd_log_error;
extern char sd_log_filename[13];
extern char sd_log_session_dir[64];
extern volatile uint32_t sd_max_pause_us;
#define SD_LOG_QUEUE_CAPACITY 16U
#define SD_LOG_SNAPSHOT_INTERVAL_MS 5000U
typedef struct {
    uint64_t sd_ready_us,first_written_us,first_written_sample_us,last_drop_us;
    uint64_t last_synced_us,last_synced_sample_us;
    uint32_t first_written_nr,last_written_nr,last_synced_nr,synced_records;
    uint32_t last_drop_nr,last_drop_reason,queue_high_water,queue_depth;
    uint32_t mount_attempts,init_failures,first_error_code,last_error_code;
    uint64_t first_error_us,last_error_us;
    uint8_t clean_shutdown;
} SDLogger_Diagnostics;
extern SDLogger_Diagnostics sd_diag;
void SDLogger_Start(void);
uint8_t SDLogger_Ready(void);
unsigned SDLogger_RecordBytes(void);
uint8_t SDLogger_StartAcquisition(uint8_t already_started);
void SDLogger_ConsumeImu(void); /* Single foreground consumer; never called from SD I/O. */
uint8_t SDLogger_CanAccept(void);
uint8_t SDLogger_AppendRaw(const ImuRecord *record,LogTime time);
void SDLogger_Process(uint32_t now);
void SDLogger_RequestStop(void);
void SDLogger_RequestResume(void);
/* Debounced B1 release: stop safely, or resume into a new file. ISR-safe. */
void SDLogger_Button(void);
void SDLogger_DataLoss(void);
/* 25 printable ASCII characters plus NUL, for the existing TFT footer. */
void SDLogger_StatusText(char text[26]);
#endif
