#ifndef LAPTUNE_GPS_DISPLAY_H
#define LAPTUNE_GPS_DISPLAY_H
#include "gps.h"
#include "tft.h"

/* Six 8px lines, y=72..119; ACC rows finish at y=70. */
HAL_StatusTypeDef GPSDisplay_Update(uint32_t now, const GPS_Data *data,const GPS_ConfigStatus *config);

#endif
