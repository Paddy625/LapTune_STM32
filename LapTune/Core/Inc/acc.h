/*
 * acc.h
 *
 *  Created on: Nov 2, 2025
 *      Author: Adam
 */

#ifndef INC_ACC_H_
#define INC_ACC_H_

#include "stm32f1xx_hal.h"

#define LIS3DH_WHO_AM_I      0x0F
#define LIS3DH_CTRL_REG1     0x20
#define LIS3DH_CTRL_REG4     0x23
#define LIS3DH_OUT_X_L       0x28

#define LIS3DH_CS_PORT       GPIOB
#define LIS3DH_CS_PIN        GPIO_PIN_12

#define LIS3DH_CS_LOW()      HAL_GPIO_WritePin(LIS3DH_CS_PORT, LIS3DH_CS_PIN, GPIO_PIN_RESET)
#define LIS3DH_CS_HIGH()     HAL_GPIO_WritePin(LIS3DH_CS_PORT, LIS3DH_CS_PIN, GPIO_PIN_SET)

extern SPI_HandleTypeDef hspi2;

uint8_t LIS3DH_ReadReg(uint8_t reg);
void LIS3DH_WriteReg(uint8_t reg, uint8_t value);
uint8_t LIS3DH_Init(void);
void LIS3DH_ReadXYZ(float *ax, float *ay, float *az);

#endif /* INC_ACC_H_ */
