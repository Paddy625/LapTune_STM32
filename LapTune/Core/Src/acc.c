/*
 * acc.c
 *
 *  Created on: Nov 2, 2025
 *      Author: Adam
 */


#include "acc.h"
#define LIS3DH_SPI_TIMEOUT_MS 10U

uint8_t LIS3DH_ReadReg(uint8_t reg)
{
    uint8_t tx = reg | 0x80; // READ bit
    uint8_t rx = 0;

    LIS3DH_CS_LOW();
    HAL_SPI_Transmit(&hspi2, &tx, 1, LIS3DH_SPI_TIMEOUT_MS);
    HAL_SPI_Receive(&hspi2, &rx, 1, LIS3DH_SPI_TIMEOUT_MS);
    LIS3DH_CS_HIGH();

    return rx;
}

void LIS3DH_WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    tx[0] = reg & 0x7F; // WRITE
    tx[1] = value;

    LIS3DH_CS_LOW();
    HAL_SPI_Transmit(&hspi2, tx, 2, LIS3DH_SPI_TIMEOUT_MS);
    LIS3DH_CS_HIGH();
}

uint8_t LIS3DH_Init(void)
{
    uint8_t id = LIS3DH_ReadReg(LIS3DH_WHO_AM_I);
    if (id != 0x33) return 0; // Wrong ID

    // 100Hz | XYZ enabled | normal mode
    LIS3DH_WriteReg(LIS3DH_CTRL_REG1, LIS3DH_CTRL_REG1_CONF);

    // BDU | +/-16g | 12-bit high resolution (left-aligned in 16 bits)
    LIS3DH_WriteReg(LIS3DH_CTRL_REG4, LIS3DH_CTRL_REG4_CONF);

    return LIS3DH_ReadReg(LIS3DH_CTRL_REG1) == LIS3DH_CTRL_REG1_CONF &&
           LIS3DH_ReadReg(LIS3DH_CTRL_REG4) == LIS3DH_CTRL_REG4_CONF;
}

uint8_t LIS3DH_ReadXYZ(float *ax, float *ay, float *az)
{
    uint8_t tx = LIS3DH_OUT_X_L | 0x80 | 0x40; // READ + MULTI
    uint8_t raw[6];

    LIS3DH_CS_LOW();
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi2, &tx, 1, LIS3DH_SPI_TIMEOUT_MS);
    if (status == HAL_OK)
        status = HAL_SPI_Receive(&hspi2, raw, 6, LIS3DH_SPI_TIMEOUT_MS);
    LIS3DH_CS_HIGH();
    if (status != HAL_OK) return 0U;

    int16_t x = (int16_t)((raw[1] << 8) | raw[0]) >> 4;
    int16_t y = (int16_t)((raw[3] << 8) | raw[2]) >> 4;
    int16_t z = (int16_t)((raw[5] << 8) | raw[4]) >> 4;

    // Convert to g
    *ax = x * LIS3DH_SENS_16G;
    *ay = y * LIS3DH_SENS_16G;
    *az = z * LIS3DH_SENS_16G;
    return 1U;
}
