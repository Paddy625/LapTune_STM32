#include "acc_display.h"
#include <stdio.h>

static float values[3], lows[3], highs[3];
static uint32_t last_refresh;
static uint8_t field, pending, healthy;

/* Signed g, three decimals, no dependency on printf floating-point support. */
static void format_g(char *out, size_t size, float value)
{
    int32_t mg = (int32_t)(value * 1000.0f + (value < 0.0f ? -0.5f : 0.5f));
    uint32_t magnitude = (uint32_t)(mg < 0 ? -mg : mg);
    (void)snprintf(out, size, "%c%02lu.%03lu", mg < 0 ? '-' : '+',
                   (unsigned long)(magnitude / 1000U),
                   (unsigned long)(magnitude % 1000U));
}

HAL_StatusTypeDef AccDisplay_Init(void)
{
    field = 0U; pending = 0U;
    last_refresh = HAL_GetTick() - 100U;
    if (TFT_Text(3, 2, "LAPTUNE ALPHA", 2, TFT_CYAN, TFT_BLACK) != HAL_OK)
        return HAL_ERROR;
    if (TFT_Text(20, 21, "ACCELEROMETER +/-16G", 1, TFT_WHITE, TFT_BLACK) != HAL_OK)
        return HAL_ERROR;
    return TFT_Text(4, 120, "ADAM TWARDOWSKI", 1, TFT_GREY, TFT_BLACK);
}

HAL_StatusTypeDef AccDisplay_Update(uint32_t now, const float current[3],
                                   const float minimum[3], const float maximum[3],
                                   uint8_t sensor_ok)
{
    char number[16], text[22];
    if (!pending) {
        if ((uint32_t)(now - last_refresh) < 100U) return HAL_OK;
        last_refresh = now;
        for (unsigned i = 0; i < 3U; ++i) {
            values[i] = current[i]; lows[i] = minimum[i]; highs[i] = maximum[i];
        }
        healthy = sensor_ok; field = 0U; pending = 1U;
    }
    /* 3 fields per axis: current (large), min and max (small). */
    uint8_t axis = field / 3U, part = field % 3U;
    uint16_t y = 34U + axis * 28U;
    uint16_t x = part == 2U ? 84U : 4U;
    uint8_t scale = part == 0U ? 2U : 1U;
    if (part == 0U) {
        if (healthy) format_g(number, sizeof(number), values[axis]);
        else (void)snprintf(number, sizeof(number), " --.---");
        (void)snprintf(text, sizeof(text), "%c %s", "XYZ"[axis], number);
    } else {
        if (healthy) format_g(number, sizeof(number), part == 1U ? lows[axis] : highs[axis]);
        else (void)snprintf(number, sizeof(number), " --.---");
        (void)snprintf(text, sizeof(text), "%s%s", part == 1U ? "MIN" : "MAX", number);
        y += 17U;
    }
    HAL_StatusTypeDef status = TFT_Text(x, y, text, scale,
                                      healthy ? TFT_WHITE : TFT_RED, TFT_BLACK);
    if (++field == 9U) pending = 0U;
    return status;
}
