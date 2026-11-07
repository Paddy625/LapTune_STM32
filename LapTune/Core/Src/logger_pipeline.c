#include "sd_logger.h"
#include "timebase.h"
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

__attribute__((weak)) void Logger_ServiceIO(void) { }
uint8_t SDLogger_Backlogged(void)
{
    ImuRecord oldest;
    uint64_t now=Timebase_NowUs();
    uint64_t age=ImuStream_Peek(&oldest) && now>=oldest.t_us ? now-oldest.t_us : 0;
    sd_diag.oldest_age_us=age>UINT32_MAX ? UINT32_MAX : (uint32_t)age;
    if (sd_diag.oldest_age_us>sd_diag.max_oldest_age_us) sd_diag.max_oldest_age_us=sd_diag.oldest_age_us;
    /* Normal history holdback is 700 ms. Leave headroom for a slow card before
       the 255-record raw ring fills. Missing SD must not suppress diagnostics. */
    return SDLogger_Ready() && age>900000U;
}
void Logger_ServiceStorage(void)
{
    /* Multiple batches without TFT interleaving when needed. Bounded work;
       every round services UART/IMU, and SD busy waits also service UART. */
    for (unsigned round=0;round<8;++round) {
        Logger_ServiceIO(); SDLogger_ConsumeImu(); SDLogger_Process(HAL_GetTick());
        if (!SDLogger_Backlogged()) break;
    }
}
