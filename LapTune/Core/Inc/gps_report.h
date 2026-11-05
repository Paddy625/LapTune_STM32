#ifndef LAPTUNE_GPS_REPORT_H
#define LAPTUNE_GPS_REPORT_H
#include <stdint.h>
/* Main-loop only, outside every FatFs/SD callback. Never called from GPS_Process. */
void GPSReport_Process(uint32_t now);
#endif
