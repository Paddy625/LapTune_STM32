#ifndef LAPTUNE_ACC_DISPLAY_H
#define LAPTUNE_ACC_DISPLAY_H

#include "tft.h"

HAL_StatusTypeDef AccDisplay_Init(void);
/* Call each loop. Snapshot at 10 Hz, send only one text field per call. */
HAL_StatusTypeDef AccDisplay_Update(uint32_t now, const float current[3],
                                   const float minimum[3], const float maximum[3],
                                   uint8_t sensor_ok);

#endif
