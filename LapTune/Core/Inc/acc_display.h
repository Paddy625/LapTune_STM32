#ifndef LAPTUNE_ACC_DISPLAY_H
#define LAPTUNE_ACC_DISPLAY_H

#include "tft.h"

HAL_StatusTypeDef AccDisplay_Init(void);
typedef struct {
    float accel[3], accel_min[3], accel_max[3]; /* g */
    float gyro[3], gyro_min[3], gyro_max[3];    /* degrees/s */
} IMU_DisplayData;

/* Snapshot at 10 Hz. One row per call: AX/GX, AY/GY, AZ/GZ, MIN/NOW/MAX. */
HAL_StatusTypeDef AccDisplay_Update(uint32_t now, const IMU_DisplayData *data,
                                   uint8_t sensor_ok);

#endif
