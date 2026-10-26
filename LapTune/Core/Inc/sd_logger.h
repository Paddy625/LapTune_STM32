#ifndef LAPTUNE_SD_LOGGER_H
#define LAPTUNE_SD_LOGGER_H
#include "acc.h"
#include "log_clock.h"
typedef enum { LOG_OFF,LOG_RECORDING,LOG_STOPPED,LOG_CARD_ERROR,LOG_FULL,LOG_DATA_LOSS } LogState;
extern volatile LogState sd_log_state;
extern volatile uint32_t sd_log_written, sd_log_dropped;
extern volatile unsigned sd_log_error;
extern char sd_log_filename[13];
void SDLogger_Start(void);
void SDLogger_Append(const LSM6DS3_Sample *sample,LogTime time);
void SDLogger_Process(uint32_t now);
void SDLogger_RequestStop(void);
void SDLogger_DataLoss(void);
#endif
