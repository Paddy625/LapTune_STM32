#include "acc.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef gpio_a, gpio_b;
SPI_HandleTypeDef hspi2;
static uint8_t registers[64], selected, last_reg, read_command;
static uint8_t sample[6] = {0x00,0x10,0x00,0xF0,0xF0,0x7F};
static unsigned fail_tx, fail_rx, receive_calls;
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    assert(port == LIS3DH_CS_PORT && pin == LIS3DH_CS_PIN);
    selected = state == GPIO_PIN_RESET;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, const uint8_t *data,
                                   uint16_t len, uint32_t timeout)
{
    assert(spi == &hspi2 && selected && timeout == 10);
    if (fail_tx) return HAL_TIMEOUT;
    last_reg = data[0] & 0x3F; read_command = data[0];
    if (len == 2) registers[last_reg] = data[1];
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *spi, uint8_t *data,
                                  uint16_t len, uint32_t timeout)
{
    assert(spi == &hspi2 && selected && timeout == 10);
    receive_calls++;
    if (fail_rx) return HAL_TIMEOUT;
    if (len == 6) { assert(read_command == 0xE8); memcpy(data, sample, 6); }
    else { assert(len == 1); *data = registers[last_reg]; }
    return HAL_OK;
}
int main(void)
{
    float x=0, y=0, z=0;
    assert(!LIS3DH_Init() && !selected);
    registers[LIS3DH_WHO_AM_I] = 0x33;
    assert(LIS3DH_Init() && !selected);
    assert(registers[LIS3DH_CTRL_REG1] == 0x57 && registers[LIS3DH_CTRL_REG4] == 0xB8);
    assert(LIS3DH_ReadXYZ(&x,&y,&z) && !selected);
    assert(fabsf(x-3.072f) < 0.00001f && fabsf(y+3.072f) < 0.00001f);
    assert(fabsf(z-24.564f) < 0.00001f);
    float old_x=x, old_y=y, old_z=z;
    fail_rx=1;
    assert(!LIS3DH_ReadXYZ(&x,&y,&z) && !selected);
    assert(x==old_x && y==old_y && z==old_z);
    fail_rx=0; fail_tx=1;
    unsigned previous=receive_calls;
    assert(!LIS3DH_ReadXYZ(&x,&y,&z) && !selected && receive_calls==previous);
    assert(x==old_x && y==old_y && z==old_z);
    puts("PASS LIS3DH: identity/config, signed 12-bit conversion, SPI failure preserves sample");
}
