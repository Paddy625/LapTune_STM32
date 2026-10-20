#ifndef LAPTUNE_GPS_H
#define LAPTUNE_GPS_H

#include "stm32f1xx_hal.h"
#include "gps_nmea.h"

HAL_StatusTypeDef GPS_Init(UART_HandleTypeDef *uart);
void GPS_OnRxComplete(UART_HandleTypeDef *uart);
void GPS_OnError(UART_HandleTypeDef *uart);
/* Main loop only: drain IRQ queue, parse NMEA and expire stale fields. */
void GPS_Process(uint32_t now);
const GPS_Data *GPS_GetData(void);

#endif
