#ifndef LAPTUNE_SD_SPI_H
#define LAPTUNE_SD_SPI_H
#include "stm32f1xx_hal.h"
/* SPI1 is 64 MHz: /256 during initialization, /32 (2 MHz) for SD data.
   The original TFT prescaler is restored after every SD operation. */
#define SD_SPI_DATA_PRESCALER SPI_BAUDRATEPRESCALER_32
typedef enum {
    SD_SPI_OK, SD_SPI_NO_BUS, SD_SPI_HAL, SD_SPI_CLOCK, SD_SPI_BUSY,
    SD_SPI_CMD0, SD_SPI_CMD8, SD_SPI_ACMD41, SD_SPI_OCR, SD_SPI_CMD16,
    SD_SPI_CMD59, SD_SPI_CSD, SD_SPI_CAPACITY, SD_SPI_READ, SD_SPI_TOKEN,
    SD_SPI_CRC, SD_SPI_WRITE, SD_SPI_REJECT, SD_SPI_STATUS
} SD_SPI_Error;
typedef struct {
    SD_SPI_Error error;
    uint8_t command, response, token, crc_enabled;
    uint8_t cmd8_response, cmd8_retries, legacy_probe;
    HAL_StatusTypeDef hal_status;
} SD_SPI_Diagnostics;
extern volatile SD_SPI_Diagnostics sd_spi_diag;
const char *SD_SPI_ErrorName(void);
void SD_SPI_Bind(SPI_HandleTypeDef *spi);
/* Foreground hook: service IMU/GPS only. Never call TFT, FatFs or SD here. */
void SD_SPI_Service(void);
#endif
