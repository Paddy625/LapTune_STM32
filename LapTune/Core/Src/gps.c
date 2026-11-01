#include "gps.h"
#include <stddef.h>
#include <string.h>

#define RX_CAPACITY 512U
static UART_HandleTypeDef *gps_uart;
static uint8_t rx_byte;
static volatile uint8_t queue[RX_CAPACITY];
static volatile uint16_t head, tail;
static volatile uint8_t restart_rx, overflow;
static char line[GPS_NMEA_MAX_LENGTH];
static uint16_t line_len;
static GPS_Data data;
static GPS_ConfigStatus configuration;
typedef enum { DISCOVER_SEND, DISCOVER_WAIT, PORT_SET, SWITCH_WAIT,
               SETTING_SEND, SETTING_DELAY, SETTING_WAIT, CONFIG_DONE, CONFIG_RETRY } Phase;
static Phase phase;
static uint32_t phase_ms;
static uint8_t probe,probes,setting,attempt,port_attempts;
static uint8_t port_config[20];
/* Default NEO-6 NMEA: disable GLL/GSA/GSV/VTG, enable GGA/RMC each solution. */
static const uint8_t messages[]={1,2,3,5,0,4};
static const uint8_t rates[]={0,0,0,0,1,1};
static uint8_t ubx_state,ubx_class,ubx_id,ubx_a,ubx_b,ubx_check;
static uint16_t ubx_length,ubx_index;
static uint8_t ubx_payload[64];

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p)|((uint32_t)le16(p+2)<<16); }
static void config_error(GPS_ConfigError reason,uint32_t now)
{
    configuration.state=GPS_CONFIG_ERROR; configuration.error=reason;
    phase=CONFIG_RETRY; phase_ms=now;
}
static HAL_StatusTypeDef send_ubx(uint8_t id,const uint8_t *payload,uint8_t length)
{
    uint8_t packet[28]={0xB5,0x62,0x06,id,length,0};
    if (length) memcpy(packet+6,payload,length);
    uint8_t a=0,b=0;
    for (unsigned i=2;i<6U+length;++i) { a+=packet[i]; b+=a; }
    packet[6U+length]=a; packet[7U+length]=b;
    return HAL_UART_Transmit(gps_uart,packet,(uint16_t)(length+8U),80U);
}
static uint8_t change_baud(uint32_t baud)
{
    if (HAL_UART_AbortReceive(gps_uart)!=HAL_OK) return 0;
    tail=head; line_len=ubx_state=0; restart_rx=overflow=0;
    gps_uart->Init.BaudRate=baud;
    if (HAL_UART_Init(gps_uart)!=HAL_OK) return 0;
    configuration.baud=baud;
    return HAL_UART_Receive_IT(gps_uart,&rx_byte,1U)==HAL_OK;
}
static void discover(void)
{
    configuration.state=GPS_CONFIG_PENDING; configuration.error=GPS_CFG_OK;
    configuration.measurement_ms=configuration.nav_rate=configuration.time_ref=0;
    configuration.attempts=0;
    probe=(gps_uart->Init.BaudRate==GPS_RUN_BAUD); probes=setting=attempt=port_attempts=0;
    phase=DISCOVER_SEND;
}
static void packet_received(uint32_t now)
{
    if (ubx_class==5 && ubx_id==0 && ubx_length==2 && ubx_payload[0]==6 &&
        (((phase==DISCOVER_WAIT || phase==SWITCH_WAIT) && ubx_payload[1]==0) ||
         ((phase==SETTING_DELAY || phase==SETTING_WAIT) && ubx_payload[1]==configuration.message_id))) {
        config_error(GPS_CFG_NAK,now); return;
    }
    if (ubx_class!=6) return;
    if (phase==DISCOVER_WAIT && ubx_id==0 && ubx_length==20 && ubx_payload[0]==1) {
        if (le32(ubx_payload+8)!=gps_uart->Init.BaudRate) return;
        memcpy(port_config,ubx_payload,sizeof(port_config));
        if (configuration.baud==GPS_RUN_BAUD && (le16(port_config+12)&3U)==3U &&
            (le16(port_config+14)&3U)==3U) {
            setting=attempt=0; phase=SETTING_SEND;
        } else phase=PORT_SET;
    } else if (phase==SETTING_WAIT && setting<sizeof(messages) && ubx_id==1 &&
               (ubx_length==3 || ubx_length==8) && ubx_payload[0]==0xF0 && ubx_payload[1]==messages[setting]) {
        uint8_t rate=ubx_payload[ubx_length==8 ? 3 : 2]; /* Receiver UART1/current port. */
        if (rate==rates[setting]) { ++setting; attempt=0; phase=SETTING_SEND; }
        else config_error(GPS_CFG_VERIFY,now);
    } else if (phase==SETTING_WAIT && setting==sizeof(messages) && ubx_id==8 && ubx_length==6) {
        configuration.measurement_ms=le16(ubx_payload);
        configuration.nav_rate=le16(ubx_payload+2); configuration.time_ref=le16(ubx_payload+4);
        if (configuration.measurement_ms==GPS_MEASUREMENT_MS && configuration.nav_rate==1 && configuration.time_ref==1) {
            configuration.state=GPS_CONFIG_READY; configuration.error=GPS_CFG_OK;
            phase=CONFIG_DONE; phase_ms=now;
        } else config_error(GPS_CFG_VERIFY,now);
    }
}
static void ubx_byte(uint8_t ch,uint32_t now)
{
    if (ubx_state==1) { ubx_state=ch==0x62 ? 2 : (ch==0xB5 ? 1 : 0); ubx_a=ubx_b=0; return; }
    if (ubx_state<=6) { ubx_a+=ch; ubx_b+=ubx_a; }
    switch (ubx_state) {
    case 2: ubx_class=ch; ubx_state=3; break;
    case 3: ubx_id=ch; ubx_state=4; break;
    case 4: ubx_length=ch; ubx_state=5; break;
    case 5:
        ubx_length|=(uint16_t)ch<<8; ubx_index=0;
        ubx_state=ubx_length>1024U ? 0 : (ubx_length ? 6 : 7); break;
    case 6:
        if (ubx_index<sizeof(ubx_payload)) ubx_payload[ubx_index]=ch;
        if (++ubx_index==ubx_length) ubx_state=7;
        break;
    case 7: ubx_check=ch; ubx_state=8; break;
    case 8:
        ubx_state=0;
        if (ubx_check==ubx_a && ch==ubx_b && ubx_length<=sizeof(ubx_payload)) packet_received(now);
        break;
    default: ubx_state=0; break;
    }
}
static void configure(uint32_t now)
{
    static const uint8_t rate[]={0xC8,0,1,0,1,0}; /* 200 ms, every cycle, GPS time. */
    HAL_StatusTypeDef result=HAL_OK;
    switch (phase) {
    case DISCOVER_SEND: {
        uint8_t port=1; configuration.message_id=0;
        result=send_ubx(0,&port,1); phase=DISCOVER_WAIT; phase_ms=now; break;
    }
    case DISCOVER_WAIT:
        if ((uint32_t)(now-phase_ms)>=500U) {
            if (++probes>=4) config_error(GPS_CFG_NO_RESPONSE,now);
            else {
                probe^=1;
                if (!change_baud(probe ? GPS_RUN_BAUD : GPS_BOOT_BAUD)) config_error(GPS_CFG_UART,now);
                else phase=DISCOVER_SEND;
            }
        }
        break;
    case PORT_SET:
        if (port_attempts>=3) { config_error(GPS_CFG_VERIFY,now); break; }
        configuration.attempts=++port_attempts;
        port_config[8]=(uint8_t)GPS_RUN_BAUD; port_config[9]=(uint8_t)(GPS_RUN_BAUD>>8);
        port_config[10]=port_config[11]=0;
        port_config[12]|=3; port_config[14]|=3;
        result=send_ubx(0,port_config,sizeof(port_config)); phase=SWITCH_WAIT; phase_ms=now; break;
    case SWITCH_WAIT:
        if ((uint32_t)(now-phase_ms)>=60U) {
            if (!change_baud(GPS_RUN_BAUD)) config_error(GPS_CFG_UART,now);
            else { probe=1; probes=0; phase=DISCOVER_SEND; }
        }
        break;
    case SETTING_SEND:
        configuration.attempts=++attempt;
        if (setting<sizeof(messages)) {
            uint8_t payload[]={0xF0,messages[setting],rates[setting]};
            configuration.message_id=1; result=send_ubx(1,payload,sizeof(payload));
        } else { configuration.message_id=8; result=send_ubx(8,rate,sizeof(rate)); }
        phase=SETTING_DELAY; phase_ms=now; break;
    case SETTING_DELAY:
        if ((uint32_t)(now-phase_ms)>=40U) {
            if (setting<sizeof(messages)) { uint8_t payload[]={0xF0,messages[setting]}; result=send_ubx(1,payload,2); }
            else result=send_ubx(8,NULL,0);
            phase=SETTING_WAIT; phase_ms=now;
        }
        break;
    case SETTING_WAIT:
        if ((uint32_t)(now-phase_ms)>=500U) {
            if (attempt>=3) config_error(GPS_CFG_NO_RESPONSE,now); else phase=SETTING_SEND;
        }
        break;
    case CONFIG_DONE:
        if (!data.connected && (uint32_t)(now-phase_ms)>=GPS_FRESH_MS) discover();
        break;
    default:
        if ((uint32_t)(now-phase_ms)>=2000U) {
            if (!change_baud(gps_uart->Init.BaudRate)) config_error(GPS_CFG_UART,now); else discover();
        }
        break;
    }
    if (result!=HAL_OK) config_error(GPS_CFG_UART,now);
}

HAL_StatusTypeDef GPS_Init(UART_HandleTypeDef *uart)
{
    if (uart == NULL) return HAL_ERROR;
    gps_uart = uart; head = tail = line_len = 0;
    restart_rx = overflow = 0;
    GPS_NMEA_Init(&data);
    memset(&configuration,0,sizeof(configuration)); configuration.baud=uart->Init.BaudRate;
    ubx_state=0; discover();
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
        tail = head; line_len = ubx_state = 0;
        if (HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U) != HAL_OK) restart_rx = 1;
    }
    if (overflow) { overflow = 0; tail = head; line_len = ubx_state = 0; }
    /* Bounded work even if bytes keep arriving during parsing. */
    for (unsigned n = 0; n < RX_CAPACITY && tail != head; ++n) {
        uint8_t ch = queue[tail]; tail = (tail + 1U) % RX_CAPACITY;
        if (ubx_state) { ubx_byte(ch,now); continue; }
        if (ch==0xB5) { ubx_state=1; line_len=0; continue; }
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
    configure(now);
}

const GPS_Data *GPS_GetData(void) { return &data; }
const GPS_ConfigStatus *GPS_GetConfig(void) { return &configuration; }
