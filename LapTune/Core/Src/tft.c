#include "tft.h"
#include <stddef.h>
#include <string.h>

static SPI_HandleTypeDef *display_spi;
/* One scanline (320 bytes at 160px), no framebuffer in the 20KB SRAM. */
static uint8_t pixels[TFT_WIDTH * 2U];

static HAL_StatusTypeDef transmit(const uint8_t *data, uint16_t size)
{
    if (display_spi == NULL) return HAL_ERROR;
    return HAL_SPI_Transmit(display_spi, data, size, 100U);
}

static HAL_StatusTypeDef command(uint8_t cmd, const uint8_t *data, uint16_t size)
{
    HAL_StatusTypeDef status;
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_RESET);
    status = transmit(&cmd, 1U);
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);
    if (status == HAL_OK && size != 0U) status = transmit(data, size);
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
    return status;
}

static HAL_StatusTypeDef window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    x += TFT_X_OFFSET;
    y += TFT_Y_OFFSET;
    uint16_t end = x + w - 1U;
    uint8_t data[4] = {x >> 8, x & 255U, end >> 8, end & 255U};
    HAL_StatusTypeDef status = command(0x2AU, data, sizeof(data));
    if (status != HAL_OK) return status;
    end = y + h - 1U;
    data[0] = y >> 8; data[1] = y & 255U;
    data[2] = end >> 8; data[3] = end & 255U;
    status = command(0x2BU, data, sizeof(data));
    if (status != HAL_OK) return status;
    return command(0x2CU, NULL, 0U);
}

HAL_StatusTypeDef TFT_Init(SPI_HandleTypeDef *spi)
{
    /* ST7735S power/frame-rate and gamma sequence, 16-bit pixel mode. */
    static const struct {
        uint8_t cmd, size, data[16];
    } init[] = {
        {0xB1, 3, {0x01, 0x2C, 0x2D}},
        {0xB2, 3, {0x01, 0x2C, 0x2D}},
        {0xB3, 6, {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}},
        {0xB4, 1, {0x07}},
        {0xC0, 3, {0xA2, 0x02, 0x84}}, {0xC1, 1, {0xC5}},
        {0xC2, 2, {0x0A, 0x00}}, {0xC3, 2, {0x8A, 0x2A}},
        {0xC4, 2, {0x8A, 0xEE}}, {0xC5, 1, {0x0E}},
        {0x36, 1, {TFT_MADCTL}}, {0x3A, 1, {0x05}},
        {0xE0, 16, {0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,
                     0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10}},
        {0xE1, 16, {0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,
                     0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10}}
    };
    if (spi == NULL) return HAL_ERROR;
    display_spi = spi;
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(150U);
    if (command(0x01U, NULL, 0U) != HAL_OK) return HAL_ERROR;
    HAL_Delay(150U);
    if (command(0x11U, NULL, 0U) != HAL_OK) return HAL_ERROR;
    HAL_Delay(120U);
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        if (command(init[i].cmd, init[i].data, init[i].size) != HAL_OK)
            return HAL_ERROR;
    }
    if (command(TFT_INVERT_COLORS ? 0x21U : 0x20U, NULL, 0U) != HAL_OK) return HAL_ERROR;
    if (command(0x13U, NULL, 0U) != HAL_OK) return HAL_ERROR;
    HAL_Delay(10U);
    if (command(0x29U, NULL, 0U) != HAL_OK) return HAL_ERROR;
    HAL_Delay(100U);
    return TFT_FillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BLACK);
}

HAL_StatusTypeDef TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             uint16_t color)
{
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT || w == 0U || h == 0U) return HAL_OK;
    if (w > TFT_WIDTH - x) w = TFT_WIDTH - x;
    if (h > TFT_HEIGHT - y) h = TFT_HEIGHT - y;
    HAL_StatusTypeDef status = window(x, y, w, h);
    if (status != HAL_OK) return status;
    for (uint16_t i = 0; i < w; ++i) {
        pixels[i * 2U] = color >> 8;
        pixels[i * 2U + 1U] = color & 255U;
    }
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET);
    for (uint16_t row = 0; row < h && status == HAL_OK; ++row)
        status = transmit(pixels, w * 2U);
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
    return status;
}

/* Original compact 5x7 column font, least significant bit at the top. */
static const uint8_t digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, {0,0x42,0x7F,0x40,0},
    {0x62,0x51,0x49,0x49,0x46}, {0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3E,0x49,0x49,0x49,0x32}, {1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36}, {0x26,0x49,0x49,0x49,0x3E}
};
static const uint8_t letters[26][5] = {
    {0x7E,9,9,9,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,9,9,9,1},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,8,8,8,0x7F},
    {0,0x41,0x7F,0x41,0}, {0x20,0x40,0x41,0x3F,1},
    {0x7F,8,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,2,0x0C,2,0x7F}, {0x7F,4,8,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,9,9,9,6},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,9,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32}, {1,1,0x7F,1,1},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x7F,0x20,0x18,0x20,0x7F}, {0x63,0x14,8,0x14,0x63},
    {3,4,0x78,4,3}, {0x61,0x51,0x49,0x45,0x43}
};

static uint8_t column(char ch, uint16_t col)
{
    if (col >= 5U) return 0;
    if (ch >= '0' && ch <= '9') return digits[ch - '0'][col];
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'][col];
    if (ch == '-') return 8U;
    if (ch == '+') return col == 2U ? 0x3EU : 8U;
    if (ch == '.') return col == 2U ? 0x40U : 0U;
    if (ch == ':') return col == 2U ? 0x24U : 0U;
    if (ch == '/') return (uint8_t)(0x40U >> col);
    return 0;
}

HAL_StatusTypeDef TFT_Text(uint16_t x, uint16_t y, const char *text,
                         uint8_t scale, uint16_t fg, uint16_t bg)
{
    if (text == NULL || scale == 0U || scale > 3U) return HAL_ERROR;
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT || text[0] == '\0') return HAL_OK;
    size_t len = strlen(text);
    uint16_t w = TFT_WIDTH - x, h = 8U * scale;
    if (len < (w + 6U * scale - 1U) / (6U * scale)) w = len * 6U * scale;
    if (h > TFT_HEIGHT - y) h = TFT_HEIGHT - y;
    HAL_StatusTypeDef status = window(x, y, w, h);
    if (status != HAL_OK) return status;
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET);
    for (uint16_t row = 0; row < h && status == HAL_OK; ++row) {
        for (uint16_t px = 0; px < w; ++px) {
            uint16_t col = px / scale;
            uint16_t color = (column(text[col / 6U], col % 6U) & (1U << (row / scale))) ? fg : bg;
            pixels[2U * px] = color >> 8;
            pixels[2U * px + 1U] = color & 255U;
        }
        status = transmit(pixels, w * 2U);
    }
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
    return status;
}
