#include "sd_logger.h"
/* Application hook keeps extrema for every drained record, including SD backlogs. */
__attribute__((weak)) void Logger_OnImuRecord(const ImuRecord *record) { (void)record; }
void SDLogger_ConsumeImu(void)
{
    for (unsigned n=0;n<32 && SDLogger_CanAccept();++n) {
        ImuRecord record;
        if (!ImuStream_Peek(&record) || SDLogger_ShouldWait(record.t_us)) break;
        if (!SDLogger_AppendRaw(&record,LogClock_AtUs(record.t_us))) break;
        Logger_OnImuRecord(&record);
        ImuStream_Consume();
    }
}

uint8_t Logger_StartAcquisition(void)
{
    /* The sensor and live display must also work without a writable card. */
    if (!LSM6DS3_Init()) return 0;
    HAL_Delay(100U);
    return ImuStream_Start();
}
