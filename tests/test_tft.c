#include "tft.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef gpio_a, gpio_b;
static SPI_HandleTypeDef spi;
static unsigned calls, fail_at;
static GPIO_PinState cs = GPIO_PIN_SET, dc;
static uint8_t bytes[65536], types[65536];
static size_t used;
static uint32_t delay_ms;

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (port == TFT_CS_PORT && pin == TFT_CS_PIN) cs = state;
    if (port == TFT_DC_PORT && pin == TFT_DC_PIN) dc = state;
}
void HAL_Delay(uint32_t ms) { delay_ms += ms; }
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle, const uint8_t *data,
                                   uint16_t size, uint32_t timeout)
{
    assert(handle == &spi && cs == GPIO_PIN_RESET && timeout == 100U);
    if (++calls == fail_at) return HAL_TIMEOUT;
    assert(used + size <= sizeof(bytes));
    memcpy(bytes + used, data, size);
    memset(types + used, dc, size);
    used += size;
    return HAL_OK;
}
static void reset_log(void) { calls = fail_at = 0; used = 0; }
static void expect_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    x += TFT_X_OFFSET; y += TFT_Y_OFFSET;
    uint16_t xe = x + w - 1, ye = y + h - 1;
    uint8_t expected[] = {0x2A, x >> 8, x & 255, xe >> 8, xe & 255,
                          0x2B, y >> 8, y & 255, ye >> 8, ye & 255, 0x2C};
    assert(memcmp(bytes, expected, sizeof(expected)) == 0);
    assert(types[0] == 0 && types[5] == 0 && types[10] == 0);
    for (size_t i = 11; i < used; ++i) assert(types[i] == 1);
    assert(cs == GPIO_PIN_SET);
}
int main(void)
{
    assert(TFT_Init(NULL) == HAL_ERROR);
    assert(TFT_Init(&spi) == HAL_OK);
    assert(delay_ms >= 500);
    unsigned seen_reset = 0, seen_sleep_out = 0, seen_rgb565 = 0, seen_on = 0;
    for (size_t i = 0; i < used; ++i) {
        if (types[i]) continue;
        if (bytes[i] == 0x01) seen_reset++;
        if (bytes[i] == 0x11) seen_sleep_out++;
        if (bytes[i] == 0x3A) { assert(bytes[i+1] == 5 && types[i+1]); seen_rgb565++; }
        if (bytes[i] == 0x29) seen_on++;
    }
    assert(seen_reset == 1 && seen_sleep_out == 1 && seen_rgb565 == 1 && seen_on == 1);

    reset_log();
    assert(TFT_FillRect(TFT_WIDTH-1, TFT_HEIGHT-1, 10, 10, TFT_RED) == HAL_OK);
    expect_window(TFT_WIDTH-1, TFT_HEIGHT-1, 1, 1);
    assert(used == 13 && bytes[11] == 0xF8 && bytes[12] == 0);
    reset_log();
    assert(TFT_FillRect(TFT_WIDTH, 0, 1, 1, TFT_RED) == HAL_OK && used == 0);
    assert(TFT_FillRect(0, 0, 0, 1, TFT_RED) == HAL_OK && used == 0);

    assert(TFT_Text(0, 0, "A", 1, TFT_RED, TFT_BLACK) == HAL_OK);
    expect_window(0, 0, 6, 8);
    assert(used == 11 + 6*8*2);
    assert(bytes[11] == 0 && bytes[13] == 0xF8); /* A top row: .###. */
    reset_log();
    assert(TFT_Text(TFT_WIDTH-1, TFT_HEIGHT-1, "ABCDEFG", 3, TFT_RED, TFT_BLACK) == HAL_OK);
    expect_window(TFT_WIDTH-1, TFT_HEIGHT-1, 1, 1);
    assert(used == 13);
    assert(TFT_Text(0, 0, NULL, 1, 0, 0) == HAL_ERROR);
    assert(TFT_Text(0, 0, "A", 0, 0, 0) == HAL_ERROR);

    for (unsigned fail = 1; fail <= 7; ++fail) {
        reset_log(); fail_at = fail;
        assert(TFT_FillRect(0, 0, 2, 2, TFT_RED) == HAL_TIMEOUT);
        assert(cs == GPIO_PIN_SET);
    }
    reset_log(); fail_at = 6;
    assert(TFT_Text(0, 0, "A", 1, 0, 0) == HAL_TIMEOUT && cs == GPIO_PIN_SET);
    puts("PASS TFT: ST7735S init, geometry/offsets, RGB565, text, clipping, CS on errors");
}
