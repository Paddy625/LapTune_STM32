#include "gps.h"
#include <stddef.h>

#define RX_CAPACITY 512U
static UART_HandleTypeDef *gps_uart;
static uint8_t rx_byte;
static volatile uint8_t queue[RX_CAPACITY];
static volatile uint16_t head, tail;
static volatile uint8_t restart_rx, overflow;
static char line[GPS_NMEA_MAX_LENGTH];
static uint16_t line_len;
static GPS_Data data;

HAL_StatusTypeDef GPS_Init(UART_HandleTypeDef *uart)
{
    if (uart == NULL) return HAL_ERROR;
    gps_uart = uart; head = tail = line_len = 0;
    restart_rx = overflow = 0;
    GPS_NMEA_Init(&data);
    return HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U);
}

void GPS_OnRxComplete(UART_HandleTypeDef *uart)
{
    if (uart != gps_uart || gps_uart == NULL) return;
    uint16_t next = (head + 1U) % RX_CAPACITY;
    if (next == tail) overflow = 1;
    else { queue[head] = rx_byte; head = next; }
    if (!restart_rx && HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U) != HAL_OK)
        restart_rx = 1;
}

void GPS_OnError(UART_HandleTypeDef *uart)
{
    if (uart == gps_uart && gps_uart != NULL) restart_rx = 1;
}

void GPS_Process(uint32_t now)
{
    if (gps_uart == NULL) return;
    if (restart_rx) {
        restart_rx = 0;
        (void)HAL_UART_AbortReceive(gps_uart);
        tail = head; line_len = 0;
        if (HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U) != HAL_OK) restart_rx = 1;
    }
    if (overflow) { overflow = 0; tail = head; line_len = 0; }
    /* Bounded work even if bytes keep arriving during parsing. */
    for (unsigned n = 0; n < RX_CAPACITY && tail != head; ++n) {
        uint8_t ch = queue[tail]; tail = (tail + 1U) % RX_CAPACITY;
        if (ch == '$') { line_len = 0; line[line_len++] = '$'; }
        else if (ch == '\n' && line_len) {
            line[line_len] = '\0';
            (void)GPS_NMEA_Parse(&data, line, now); line_len = 0;
        } else if (ch != '\r' && line_len) {
            if (line_len < sizeof(line)-1U) line[line_len++] = (char)ch;
            else line_len = 0;
        }
    }
    GPS_NMEA_Expire(&data, now);
}

const GPS_Data *GPS_GetData(void) { return &data; }
