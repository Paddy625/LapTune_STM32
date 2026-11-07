#ifndef LAPTUNE_LOGGER_WORKSPACE_H
#define LAPTUNE_LOGGER_WORKSPACE_H
#include "sd_logger.h"
#include "gps.h"
#include "gps_time.h"
#include "timebase.h"
/* Foreground-only union: Session_Write runs only after the data batch has been
   accepted by synchronous f_write. SD service hooks handle GPS, never storage.
   Neither member is passed to DMA. This reuses RAM instead of growing buffers. */
typedef struct {
    ImuDiagnostics imu;
    GPS_Diagnostics gps;
    SDLogger_Diagnostics sd;
    PPS_Diagnostics pps;
    GPS_TimeDiagnostics sync;
    GPS_ConfigStatus config;
    char text[224];
} SessionWorkspace;
typedef union { char batch[1024]; SessionWorkspace session; } LoggerWorkspace;
_Static_assert(sizeof(SessionWorkspace)<=1024,"session must fit the existing SD batch RAM");
extern LoggerWorkspace logger_workspace;
#endif
