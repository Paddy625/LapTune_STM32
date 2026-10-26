#ifndef LAPTUNE_SD_SPI_H
#define LAPTUNE_SD_SPI_H
#include "stm32f1xx_hal.h"
void SD_SPI_Bind(SPI_HandleTypeDef *spi);
/* Foreground hook: service IMU/GPS only. Never call TFT, FatFs or SD here. */
void SD_SPI_Service(void);
#endif
