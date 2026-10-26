#include "acc.h"
#include <stddef.h>

#define SPI_TIMEOUT_MS 10U
#define RESET_ATTEMPTS 100U

static uint8_t read_regs(uint8_t reg, uint8_t *data, uint16_t len)
{
    /* LSM6DS3 uses CTRL3_C.IF_INC, not LIS3DH's command bit 0x40. */
    uint8_t command = reg | 0x80U;
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT, LSM6DS3_CS_PIN, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi2, &command, 1U, SPI_TIMEOUT_MS);
    if (status == HAL_OK) status = HAL_SPI_Receive(&hspi2, data, len, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT, LSM6DS3_CS_PIN, GPIO_PIN_SET);
    return status == HAL_OK;
}

static uint8_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {reg & 0x7FU, value};
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT, LSM6DS3_CS_PIN, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi2, tx, sizeof(tx), SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT, LSM6DS3_CS_PIN, GPIO_PIN_SET);
    return status == HAL_OK;
}

uint8_t LSM6DS3_Init(void)
{
    static const uint8_t config[][2] = {
        {LSM6DS3_CTRL3_C,  LSM6DS3_CTRL3_C_CONF},
        {LSM6DS3_CTRL4_C,  LSM6DS3_CTRL4_C_CONF},
        {LSM6DS3_CTRL9_XL, LSM6DS3_XYZ_ENABLE},
        {LSM6DS3_CTRL10_C, LSM6DS3_XYZ_ENABLE},
        {LSM6DS3_CTRL1_XL, LSM6DS3_CTRL1_XL_CONF},
        {LSM6DS3_CTRL2_G,  LSM6DS3_CTRL2_G_CONF}
    };
    uint8_t value = 0;
    if (!read_regs(LSM6DS3_WHO_AM_I, &value, 1U) || value != LSM6DS3_DEVICE_ID) return 0;
    if (!write_reg(LSM6DS3_CTRL3_C, 0x01U)) return 0; /* Software reset. */
    unsigned attempt;
    for (attempt = 0; attempt < RESET_ATTEMPTS; ++attempt) {
        if (!read_regs(LSM6DS3_CTRL3_C, &value, 1U)) return 0;
        if (!(value & 0x01U)) break;
        HAL_Delay(1U);
    }
    if (attempt == RESET_ATTEMPTS) return 0;
    for (unsigned i = 0; i < sizeof(config)/sizeof(config[0]); ++i) {
        if (!write_reg(config[i][0], config[i][1]) ||
            !read_regs(config[i][0], &value, 1U) || value != config[i][1]) return 0;
    }
    return 1;
}

static int16_t signed_le16(const uint8_t *bytes)
{
    /* Explicit sign conversion, including -32768. No LIS3DH-style >>4 shift. */
    uint16_t value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return value < 0x8000U ? (int16_t)value : (int16_t)((int32_t)value - 65536);
}

LSM6DS3_ReadResult LSM6DS3_ReadSample(LSM6DS3_Sample *sample)
{
    uint8_t status = 0, raw[12] = {0};
    if (sample == NULL || !read_regs(LSM6DS3_STATUS_REG, &status, 1U))
        return LSM6DS3_READ_ERROR;
    if ((status & LSM6DS3_DATA_READY) != LSM6DS3_DATA_READY)
        return LSM6DS3_READ_NOT_READY;
    if (!read_regs(LSM6DS3_OUTX_L_G, raw, sizeof(raw))) return LSM6DS3_READ_ERROR;
    LSM6DS3_Sample next = {
        .ax = signed_le16(raw+6) * LSM6DS3_ACCEL_G_LSB,
        .ay = signed_le16(raw+8) * LSM6DS3_ACCEL_G_LSB,
        .az = signed_le16(raw+10) * LSM6DS3_ACCEL_G_LSB,
        .gx = signed_le16(raw) * LSM6DS3_GYRO_DPS_LSB,
        .gy = signed_le16(raw+2) * LSM6DS3_GYRO_DPS_LSB,
        .gz = signed_le16(raw+4) * LSM6DS3_GYRO_DPS_LSB
    };
    *sample = next;
    return LSM6DS3_READ_OK;
}
