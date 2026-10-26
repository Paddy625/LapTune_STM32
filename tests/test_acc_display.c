#include "acc_display.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick;
static unsigned calls, rows;
static uint8_t checking_rows;
static char last[32];
static uint16_t last_color;
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef TFT_Text(uint16_t x, uint16_t y, const char *text,
                         uint8_t scale, uint16_t fg, uint16_t bg)
{
    assert(bg == TFT_BLACK);
    assert(x + strlen(text)*6U*scale <= TFT_WIDTH && y+8U*scale <= TFT_HEIGHT);
    assert(strlen(text) < sizeof(last));
    if (checking_rows) {
        static const uint16_t positions[] = {18,26,36,44,54,62};
        unsigned row = rows % 6U;
        assert(scale == 1U && strlen(text) == 25U);
        assert(y == positions[row] && y >= 16U && y+8U <= 72U); /* Never enter GPS. */
        assert(text[0] == (row % 2U ? 'G' : 'A'));
        assert(text[1] == "XYZ"[row / 2U]);
        assert(text[9] == ' ' && text[17] == ' ');
        rows++;
    }
    strcpy(last, text); last_color = fg; calls++;
    return HAL_OK;
}
static void init_display(void)
{
    checking_rows = 0;
    assert(AccDisplay_Init() == HAL_OK);
    rows = 0; checking_rows = 1;
}
int main(void)
{
    IMU_DisplayData data = {
        .accel = {-1.2345f, 0, 16}, .accel_min = {-16,-2,-3}, .accel_max = {16,2,16},
        .gyro = {-70.05f,0,2293.76f}, .gyro_min = {-2000,-100,-2293.76f},
        .gyro_max = {2000,100,2293.76f}
    };
    assert(AccDisplay_Update(0, NULL, 1) == HAL_ERROR);
    init_display(); assert(calls == 2);
    const char *expected[] = {
        "AX-16.000 -01.235 +16.000",
        "GX-2000.0 -0070.1 +2000.0",
        "AY-02.000 +00.000 +02.000",
        "GY-0100.0 +0000.0 +0100.0",
        "AZ-03.000 +16.000 +16.000",
        "GZ-2293.8 +2293.8 +2293.8"
    };
    for (unsigned i=0; i<6; ++i) {
        assert(AccDisplay_Update(tick, &data, 1) == HAL_OK);
        assert(strcmp(last, expected[i]) == 0);
        assert(last_color == (i%2U ? TFT_CYAN : TFT_WHITE));
        if (i == 0) {
            data.accel[1] = 10; data.accel_min[1] = -10;
            data.gyro[0] = 100; data.gyro_min[0] = -100;
        } /* All six rows must retain the original complete IMU snapshot. */
    }
    unsigned done = calls;
    assert(AccDisplay_Update(tick+99, &data, 1) == HAL_OK && calls == done);
    for (unsigned i=0; i<6; ++i) {
        assert(AccDisplay_Update(tick+100, &data, 0) == HAL_OK);
        assert(strcmp(last+2, i%2U ? "-----.- -----.- -----.-" : "---.--- ---.--- ---.---") == 0);
        assert(last_color == TFT_RED);
    }
    /* A valid shorter/smaller number must overwrite the full old field. */
    data = (IMU_DisplayData){0};
    for (unsigned i=0; i<6; ++i) {
        assert(AccDisplay_Update(tick+200, &data, 1) == HAL_OK);
        assert(strcmp(last+2, i%2U ? "+0000.0 +0000.0 +0000.0" : "+00.000 +00.000 +00.000") == 0);
    }
    /* Corrupt numbers cannot overrun a column or produce floating conversion UB. */
    data.accel[0] = NAN; data.gyro[0] = INFINITY;
    data.accel_min[0] = -1000; data.gyro_max[0] = 10000;
    assert(AccDisplay_Update(tick+300, &data, 1) == HAL_OK);
    assert(strcmp(last, "AX------- ------- +00.000") == 0);
    assert(AccDisplay_Update(tick+300, &data, 1) == HAL_OK);
    assert(strcmp(last, "GX+0000.0 ------- -------") == 0);
    /* Unsigned tick arithmetic across the 32-bit SysTick wrap. */
    tick = UINT32_MAX-50U;
    init_display();
    for (unsigned i=0; i<6; ++i) assert(AccDisplay_Update(tick,&data,1)==HAL_OK);
    done=calls;
    assert(AccDisplay_Update(48,&data,1)==HAL_OK && calls==done);
    assert(AccDisplay_Update(49,&data,1)==HAL_OK && calls==done+1);
    puts("PASS IMU UI: AX/GX AY/GY AZ/GZ, MIN/NOW/MAX, units/precision, GPS bounds, snapshot, errors, 10Hz/wrap");
}
