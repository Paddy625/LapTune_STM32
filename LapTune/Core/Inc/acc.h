#ifndef INC_ACC_H_
#define INC_ACC_H_

#include "stm32f1xx_hal.h"

/* KAmodLSM6DS3: four-wire SPI2, mode 3, 8 MHz. */
#define LSM6DS3_WHO_AM_I       0x0FU
#define LSM6DS3_DEVICE_ID      0x69U
#define LSM6DS3_CTRL1_XL       0x10U
#define LSM6DS3_CTRL2_G        0x11U
#define LSM6DS3_CTRL3_C        0x12U
#define LSM6DS3_CTRL4_C        0x13U
#define LSM6DS3_CTRL9_XL       0x18U
#define LSM6DS3_CTRL10_C       0x19U
#define LSM6DS3_STATUS_REG     0x1EU
#define LSM6DS3_OUTX_L_G       0x22U

#define LSM6DS3_CTRL1_XL_CONF  0x44U /* 104 Hz, +/-16 g. */
#define LSM6DS3_CTRL2_G_CONF   0x4CU /* 104 Hz, +/-2000 degrees/s. */
#define LSM6DS3_CTRL3_C_CONF   0x44U /* BDU, auto-increment; little-endian, 4-wire. */
#define LSM6DS3_CTRL4_C_CONF   0x04U /* Disable I2C; use SPI. */
#define LSM6DS3_XYZ_ENABLE     0x38U
#define LSM6DS3_DATA_READY     0x03U /* XLDA and GDA. */
#define LSM6DS3_ACCEL_G_LSB    0.000488f
#define LSM6DS3_GYRO_DPS_LSB   0.070f

#define LSM6DS3_CS_PORT GPIOB
#define LSM6DS3_CS_PIN  GPIO_PIN_12

typedef struct {
    float ax, ay, az; /* Acceleration in g, including gravity. */
    float gx, gy, gz; /* Angular velocity in degrees/s, not angles. */
} LSM6DS3_Sample;

typedef enum {
    LSM6DS3_READ_ERROR = 0,
    LSM6DS3_READ_OK = 1,
    LSM6DS3_READ_NOT_READY = 2
} LSM6DS3_ReadResult;

extern SPI_HandleTypeDef hspi2;

uint8_t LSM6DS3_Init(void);
/* One burst reads gyro XYZ then accel XYZ. Failure/not-ready preserves *sample. */
LSM6DS3_ReadResult LSM6DS3_ReadSample(LSM6DS3_Sample *sample);

#endif
