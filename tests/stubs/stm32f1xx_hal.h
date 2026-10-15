#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
typedef struct { unsigned unused; } SPI_HandleTypeDef;
typedef struct { unsigned unused; } GPIO_TypeDef;
extern GPIO_TypeDef gpio_a, gpio_b;
#define GPIOA (&gpio_a)
#define GPIOB (&gpio_b)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_12 (1U << 12)
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *, const uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *, uint8_t *, uint16_t, uint32_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
void HAL_Delay(uint32_t);
uint32_t HAL_GetTick(void);
#endif
