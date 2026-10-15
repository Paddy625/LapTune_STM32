#ifndef LAPTUNE_TFT_H
#define LAPTUNE_TFT_H

#include "main.h"

/* ST7735S, landscape RGB565. SPI1: PA5 SCK, PA7 MOSI; no MISO.
 * Defaults: 160x128 landscape module, green-tab address offsets. See docs/TFT_ST7735S.md.
 * Override for the exact module; the controller has 132x162 display RAM.
 */
#ifndef TFT_WIDTH
#define TFT_WIDTH 160U
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 128U
#endif
#ifndef TFT_X_OFFSET
#define TFT_X_OFFSET 0U
#endif
#ifndef TFT_Y_OFFSET
#define TFT_Y_OFFSET 0U
#endif
#ifndef TFT_MADCTL
#define TFT_MADCTL 0x68U /* MX | MV | BGR; landscape rotated 180 degrees */
#endif
#ifndef TFT_INVERT_COLORS
#define TFT_INVERT_COLORS 0U
#endif
#define TFT_CS_PORT TFT_CS_GPIO_Port
#define TFT_CS_PIN TFT_CS_Pin
#define TFT_DC_PORT TFT_DC_GPIO_Port
#define TFT_DC_PIN TFT_DC_Pin
#define TFT_RST_PORT TFT_RST_GPIO_Port
#define TFT_RST_PIN TFT_RST_Pin

#define TFT_BLACK 0x0000U
#define TFT_WHITE 0xFFFFU
#define TFT_CYAN  0x07FFU
#define TFT_RED   0xF800U
#define TFT_GREY  0x8410U

/* Blocking, main-loop only; every call releases CS, including on SPI errors. */
HAL_StatusTypeDef TFT_Init(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             uint16_t color);
/* Uppercase A-Z, digits, space, + - . / :; scale 1..3, clipped, no wrapping. */
HAL_StatusTypeDef TFT_Text(uint16_t x, uint16_t y, const char *text,
                         uint8_t scale, uint16_t fg, uint16_t bg);

#endif
