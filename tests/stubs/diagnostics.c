/* Storage test uses a synthetic acquisition clock; hardware paths have their own tests. */
#include "imu_stream.h"
#include "gps.h"
#include "timebase.h"
#ifndef LAPTUNE_PIPELINE_TEST
ImuDiagnostics ImuStream_Snapshot(void) { return imu_diag; }
#endif
GPS_Diagnostics GPS_DiagnosticsSnapshot(void) { return gps_diag; }
TimebaseConfig timebase_config={32000000,64000000,63,1000000,65535};
PPS_Diagnostics Timebase_PPSSnapshot(void) { return (PPS_Diagnostics){0}; }
