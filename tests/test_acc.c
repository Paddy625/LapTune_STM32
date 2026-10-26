#include "acc.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef gpio_a, gpio_b;
SPI_HandleTypeDef hspi2;
static uint8_t registers[128], selected, last_reg, read_command;
static const uint8_t sample_bytes[12] = {
    0xE8,0x03, 0x18,0xFC, 0x00,0x80, /* gyro: 1000, -1000, -32768 */
    0x00,0x08, 0x00,0xF8, 0xFF,0x7F  /* accel: 2048, -2048, 32767 */
};
static unsigned tx_calls, receive_calls, bursts, delays, reset_reads;
static unsigned fail_tx_at, fail_rx_at;
static uint8_t stuck_reset, corrupt_config;

void HAL_Delay(uint32_t ms) { assert(ms == 1); delays++; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    assert(port == LSM6DS3_CS_PORT && pin == LSM6DS3_CS_PIN);
    selected = state == GPIO_PIN_RESET;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, const uint8_t *data,
                                   uint16_t len, uint32_t timeout)
{
    assert(spi == &hspi2 && selected && timeout == 10);
    if (++tx_calls == fail_tx_at) return HAL_TIMEOUT;
    assert(len == 1 || len == 2);
    last_reg = data[0] & 0x7F; read_command = data[0];
    if (len == 2) {
        assert(!(data[0] & 0x80));
        registers[last_reg] = data[1];
        if (last_reg == 0x12 && data[1] == 1) reset_reads = 2;
    } else assert(data[0] & 0x80);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *spi, uint8_t *data,
                                  uint16_t len, uint32_t timeout)
{
    assert(spi == &hspi2 && selected && timeout == 10);
    if (++receive_calls == fail_rx_at) {
        memset(data, 0xFF, len); /* Partial/invalid input must never become a sample. */
        return HAL_TIMEOUT;
    }
    if (len == 12) {
        assert(read_command == 0xA2 || read_command == 0xBE);
        assert(registers[0x12] & 4); /* IF_INC is required for the 12-byte burst. */
        memcpy(data, sample_bytes, 12); bursts++;
        if (read_command==0xBE) registers[0x3A]-=6;
    } else if (last_reg==0x3A) {
        assert(len==2 || len==4); memcpy(data,registers+0x3A,len);
    } else {
        assert(len == 1);
        if (last_reg == 0x12 && (registers[0x12] & 1) && !stuck_reset) {
            if (reset_reads) reset_reads--;
            else registers[0x12] = 0;
        }
        *data = registers[last_reg];
        if (corrupt_config && last_reg == 0x11) *data ^= 4;
    }
    return HAL_OK;
}
static void reset_mock(void)
{
    memset(registers, 0, sizeof(registers));
    registers[0x0F] = 0x69;
    tx_calls=receive_calls=bursts=delays=reset_reads=0;
    fail_tx_at=fail_rx_at=selected=stuck_reset=corrupt_config=0;
}
static void unchanged(const LSM6DS3_Sample *s, const LSM6DS3_Sample *old)
{
    assert(s->ax==old->ax && s->ay==old->ay && s->az==old->az);
    assert(s->gx==old->gx && s->gy==old->gy && s->gz==old->gz && !selected);
}
int main(void)
{
    reset_mock(); registers[0x0F] = 0x33; /* Reject the old LIS3DH. */
    assert(!LSM6DS3_Init() && !selected);
    reset_mock(); assert(LSM6DS3_Init() && !selected && delays==2);
    assert(registers[0x10]==0x44 && registers[0x11]==0x4C);
    assert(registers[0x12]==0x44 && registers[0x13]==0x04);
    assert(registers[0x18]==0x38 && registers[0x19]==0x38);
    LSM6DS3_Sample s = {1,2,3,4,5,6}, old=s;
    assert(LSM6DS3_ReadSample(NULL)==LSM6DS3_READ_ERROR && !selected);
    for (uint8_t ready=0; ready<3; ++ready) {
        registers[0x1E] = ready;
        assert(LSM6DS3_ReadSample(&s)==LSM6DS3_READ_NOT_READY && bursts==0);
        unchanged(&s,&old);
    }
    registers[0x1E] = 3;
    assert(LSM6DS3_ReadSample(&s)==LSM6DS3_READ_OK && !selected && bursts==1);
    assert(fabsf(s.ax-0.999424f)<0.00001f && fabsf(s.ay+0.999424f)<0.00001f);
    assert(fabsf(s.az-15.990296f)<0.00001f);
    assert(fabsf(s.gx-70.0f)<0.0001f && fabsf(s.gy+70.0f)<0.0001f);
    assert(fabsf(s.gz+2293.76f)<0.001f);
    old=s;
    fail_tx_at=tx_calls+1;
    assert(LSM6DS3_ReadSample(&s)==LSM6DS3_READ_ERROR); unchanged(&s,&old);
    fail_tx_at=tx_calls+2; /* Fail the burst command after a successful status read. */
    assert(LSM6DS3_ReadSample(&s)==LSM6DS3_READ_ERROR); unchanged(&s,&old);
    fail_tx_at=0; fail_rx_at=receive_calls+1;
    assert(LSM6DS3_ReadSample(&s)==LSM6DS3_READ_ERROR); unchanged(&s,&old);
    fail_rx_at=receive_calls+2;
    assert(LSM6DS3_ReadSample(&s)==LSM6DS3_READ_ERROR); unchanged(&s,&old);
    reset_mock(); stuck_reset=1;
    assert(!LSM6DS3_Init() && !selected && delays==100);
    reset_mock(); corrupt_config=1;
    assert(!LSM6DS3_Init() && !selected);
    reset_mock(); fail_tx_at=2;
    assert(!LSM6DS3_Init() && !selected);
    reset_mock(); fail_rx_at=1;
    assert(!LSM6DS3_Init() && !selected);
    reset_mock(); assert(LSM6DS3_Init() && LSM6DS3_EnableFIFO());
    assert(registers[8]==9 && registers[9]==0 && registers[10]==0x26);
    uint16_t pending=99;
    assert(LSM6DS3_ReadFIFO(&s,&pending)==LSM6DS3_READ_NOT_READY && pending==0);
    registers[0x3A]=12;
    assert(LSM6DS3_ReadFIFO(&s,&pending)==LSM6DS3_READ_OK && pending==2);
    assert(registers[0x3A]==6 && fabsf(s.gx-70)<0.0001f);
    old=s; registers[0x3B]=0x40;
    assert(LSM6DS3_ReadFIFO(&s,&pending)==LSM6DS3_READ_OVERFLOW); unchanged(&s,&old);
    registers[0x3B]=0; registers[0x3C]=1;
    assert(LSM6DS3_ReadFIFO(&s,&pending)==LSM6DS3_READ_ERROR); unchanged(&s,&old);
    registers[0x3C]=0; fail_rx_at=receive_calls+3;
    assert(LSM6DS3_ReadFIFO(&s,&pending)==LSM6DS3_READ_ERROR); unchanged(&s,&old);
    puts("PASS LSM6DS3: identity/reset/config, ready flags, 6-axis burst/scaling, signed limits, SPI errors preserve sample");
}
