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

#define LSM6DS3_CTRL1_XL_CONF  0x4CU /* 104 Hz, +/-8 g; FS_XL=11. */
#define LSM6DS3_CTRL2_G_CONF   0x44U /* 104 Hz, +/-500 degrees/s; FS_G=01, FS_125=0. */
#define LSM6DS3_CTRL3_C_CONF   0x44U /* BDU, auto-increment; little-endian, 4-wire. */
#define LSM6DS3_CTRL4_C_CONF   0x04U /* Disable I2C; use SPI. */
#define LSM6DS3_XYZ_ENABLE     0x38U
#define LSM6DS3_DATA_READY     0x03U /* XLDA and GDA. */
#define LSM6DS3_ACCEL_RANGE_G  8U
#define LSM6DS3_GYRO_RANGE_DPS 500U
#define LSM6DS3_ACCEL_UG_LSB   244U
#define LSM6DS3_GYRO_UDPS_LSB  17500U
#define LSM6DS3_ACCEL_G_LSB    (LSM6DS3_ACCEL_UG_LSB / 1000000.0f)
#define LSM6DS3_GYRO_DPS_LSB   (LSM6DS3_GYRO_UDPS_LSB / 1000000.0f)

#define LSM6DS3_CS_PORT GPIOB
#define LSM6DS3_CS_PIN  GPIO_PIN_12

typedef struct {
    float ax, ay, az; /* Acceleration in g, including gravity. */
    float gx, gy, gz; /* Angular velocity in degrees/s, not angles. */
} LSM6DS3_Sample;

typedef enum {
    LSM6DS3_READ_ERROR = 0,
    LSM6DS3_READ_OK = 1,
    LSM6DS3_READ_NOT_READY = 2,
    LSM6DS3_READ_OVERFLOW = 3
} LSM6DS3_ReadResult;

extern SPI_HandleTypeDef hspi2;

extern uint8_t lsm6ds3_registers[16],lsm6ds3_config_verified; /* Addresses 0x0A..0x19. */
uint8_t LSM6DS3_ReadConfiguration(void);
uint8_t LSM6DS3_Init(void);
/* One burst reads gyro XYZ then accel XYZ. Failure/not-ready preserves *sample. */
LSM6DS3_ReadResult LSM6DS3_ReadSample(LSM6DS3_Sample *sample);
uint8_t LSM6DS3_EnableFIFO(void);
/* FIFO contains gyro XYZ / accel XYZ at 104 Hz; pending includes this sample. */
LSM6DS3_ReadResult LSM6DS3_ReadFIFO(LSM6DS3_Sample *sample, uint16_t *pending);

#endif
