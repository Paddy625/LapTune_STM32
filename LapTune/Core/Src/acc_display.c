#include "acc_display.h"
#include <stdio.h>

static IMU_DisplayData snapshot;
static uint32_t last_refresh;
static uint8_t field, pending, healthy;

/* Seven characters per column: +/-00.000 g or +/-0000.0 degrees/s. */
static void format_value(char *out, size_t size, float value, uint8_t gyro)
{
    uint32_t factor = gyro ? 10U : 1000U;
    float scaled = value * (float)factor;
    /* Keep invalid/out-of-range input from growing into an adjacent column. */
    if (!(scaled >= -99999.0f && scaled <= 99999.0f)) {
        (void)snprintf(out, size, "-------");
        return;
    }
    int32_t rounded = (int32_t)(scaled + (value < 0.0f ? -0.5f : 0.5f));
    uint32_t magnitude = (uint32_t)(rounded < 0 ? -rounded : rounded);
    (void)snprintf(out, size, gyro ? "%c%04lu.%01lu" : "%c%02lu.%03lu",
                   rounded < 0 ? '-' : '+', (unsigned long)(magnitude / factor),
                   (unsigned long)(magnitude % factor));
}

HAL_StatusTypeDef AccDisplay_Init(void)
{
    field = 0U; pending = 0U;
    last_refresh = HAL_GetTick() - 100U;
    if (TFT_Text(4, 0, "LAPTUNE ALPHA", 2, TFT_RED, TFT_BLACK) != HAL_OK)
        return HAL_ERROR;
    return TFT_Text(4, 120, "ACC +/-16G GPS", 1, TFT_GREY, TFT_BLACK);
}

HAL_StatusTypeDef AccDisplay_Update(uint32_t now, const IMU_DisplayData *data,
                                   uint8_t sensor_ok)
{
    char minimum[16], current[16], maximum[16], text[26];
    if (data == NULL) return HAL_ERROR;
    if (!pending) {
        if ((uint32_t)(now - last_refresh) < 100U) return HAL_OK;
        last_refresh = now;
        snapshot = *data;
        healthy = sensor_ok; field = 0U; pending = 1U;
    }
    /* Six 8px rows end at y=70; GPS retains its existing y=72..119 area. */
    uint8_t axis = field / 2U, gyro = field % 2U;
    uint16_t y = 18U + axis * 18U + gyro * 8U;
    if (healthy) {
        format_value(minimum, sizeof(minimum), gyro ? snapshot.gyro_min[axis] : snapshot.accel_min[axis], gyro);
        format_value(current, sizeof(current), gyro ? snapshot.gyro[axis] : snapshot.accel[axis], gyro);
        format_value(maximum, sizeof(maximum), gyro ? snapshot.gyro_max[axis] : snapshot.accel_max[axis], gyro);
    } else {
        const char *missing = gyro ? "-----.-" : "---.---";
        (void)snprintf(minimum, sizeof(minimum), "%s", missing);
        (void)snprintf(current, sizeof(current), "%s", missing);
        (void)snprintf(maximum, sizeof(maximum), "%s", missing);
    }
    /* 2 label characters + 3x7 digits + 2 spaces = 25 characters / 150px. */
    (void)snprintf(text, sizeof(text), "%c%c%.7s %.7s %.7s",
                   gyro ? 'G' : 'A', "XYZ"[axis], minimum, current, maximum);
    HAL_StatusTypeDef status = TFT_Text(4U, y, text, 1,
                                      healthy ? (gyro ? TFT_CYAN : TFT_WHITE) : TFT_RED,
                                      TFT_BLACK);
    if (++field == 6U) pending = 0U;
    return status;
}
