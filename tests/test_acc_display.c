#include "acc_display.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick;
static unsigned calls;
static char last[32];
static uint16_t last_color;
static uint16_t last_x, last_y, last_width, last_height;
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef TFT_Text(uint16_t x, uint16_t y, const char *text,
                         uint8_t scale, uint16_t fg, uint16_t bg)
{
    (void)bg;
    assert(x + strlen(text)*6U*scale <= TFT_WIDTH && y+8U*scale <= TFT_HEIGHT);
    assert(strlen(text) < sizeof(last));
    strcpy(last, text); last_color = fg;
    last_x = x; last_y = y;
    last_width = strlen(text)*6U*scale; last_height = 8U*scale;
    calls++;
    return HAL_OK;
}
int main(void)
{
    float values[] = {-1.2345f, 0.0f, 16.0f};
    float low[] = {-16, -2, -3}, high[] = {16, 2, 3};
    assert(AccDisplay_Init() == HAL_OK && calls == 2);
    assert(AccDisplay_Update(tick, values, low, high, 1) == HAL_OK);
    assert(strcmp(last, "X-01.235") == 0);
    uint16_t current_right = last_x + last_width;
    uint16_t current_top = last_y, current_bottom = last_y + last_height;
    low[0] = -5; /* An in-progress frame must keep its original snapshot. */
    assert(AccDisplay_Update(tick, values, low, high, 1) == HAL_OK);
    assert(strcmp(last, "+16.000") == 0);
    assert(last_x >= current_right && last_y == current_top);
    uint16_t maximum_bottom = last_y + last_height;
    assert(AccDisplay_Update(tick, values, low, high, 1) == HAL_OK);
    assert(strcmp(last, "-16.000") == 0);
    assert(last_x >= current_right && last_y >= maximum_bottom);
    assert(last_y + last_height <= current_bottom);
    for (unsigned i = 3; i < 9; ++i)
        assert(AccDisplay_Update(tick, values, low, high, 1) == HAL_OK);
    unsigned done = calls;
    assert(AccDisplay_Update(tick+99, values, low, high, 1) == HAL_OK && calls == done);
    assert(AccDisplay_Update(tick+100, values, low, high, 0) == HAL_OK);
    assert(strcmp(last, "X---.---") == 0 && last_color == TFT_RED);
    /* Unsigned tick arithmetic across the 32-bit SysTick wrap. */
    tick = UINT32_MAX-50U;
    assert(AccDisplay_Init() == HAL_OK);
    for (unsigned i = 0; i < 9; ++i)
        assert(AccDisplay_Update(tick, values, low, high, 1) == HAL_OK);
    done = calls;
    assert(AccDisplay_Update(48, values, low, high, 1) == HAL_OK && calls == done);
    assert(AccDisplay_Update(49, values, low, high, 1) == HAL_OK && calls == done+1);
    puts("PASS UI: layout, signed g/rounding, min/max snapshot, 10Hz, errors, tick wrap");
}
