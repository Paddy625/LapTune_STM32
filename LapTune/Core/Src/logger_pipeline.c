#include "sd_logger.h"
__attribute__((weak)) void Logger_OnImuRecord(const ImuRecord *r) { (void)r; }
void SDLogger_ConsumeImu(void)
{
    for (unsigned n=0;n<32 && SDLogger_CanAccept();++n) {
        ImuRecord record;
        if (!ImuStream_Peek(&record)) break;
        if (!SDLogger_AppendRaw(&record,LogClock_At((uint32_t)(record.t_us/1000U)))) break;
        ImuStream_Consume(); Logger_OnImuRecord(&record);
    }
}

uint8_t SDLogger_StartAcquisition(uint8_t already_started)
{
    if (!already_started && !SDLogger_Ready()) return 0;
    if (!LSM6DS3_Init()) return 0;
    HAL_Delay(100U);
    return ImuStream_Start();
}
