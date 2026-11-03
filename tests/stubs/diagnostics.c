/* Storage test uses a synthetic acquisition clock; hardware paths have their own tests. */
#include "imu_stream.h"
#include "gps.h"
#include "timebase.h"
#ifndef LAPTUNE_PIPELINE_TEST
ImuDiagnostics ImuStream_Snapshot(void) { return imu_diag; }
uint8_t ImuStream_Peek(ImuRecord *r) { (void)r; return 0; }
uint8_t ImuStream_PeekNext(ImuRecord *r) { (void)r; return 0; }
void ImuStream_Consume(void) {}
uint8_t LSM6DS3_Init(void) { return 1; }
uint8_t ImuStream_Start(void) { return 1; }
void HAL_Delay(uint32_t ms) { (void)ms; }
#endif
GPS_Diagnostics GPS_DiagnosticsSnapshot(void) { return gps_diag; }
uint64_t BootIdentity_Counter(void) { return 1; }
TimebaseConfig timebase_config={32000000,64000000,63,1000000,65535};
PPS_Diagnostics Timebase_PPSSnapshot(void) { return (PPS_Diagnostics){0}; }
