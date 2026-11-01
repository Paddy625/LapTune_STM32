#include "gps.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

#define RX_CAPACITY 1024U
static UART_HandleTypeDef *gps_uart;
static uint8_t rx_byte;
static volatile uint8_t queue[RX_CAPACITY];
static volatile uint16_t head, tail;
static volatile uint8_t restart_rx, overflow;
static GPS_Data data;
static GPS_ConfigStatus configuration;
typedef enum { DISCOVER_SEND, DISCOVER_WAIT, PORT_SET, SWITCH_WAIT,
               INFO_SEND,INFO_WAIT,SETTING_SEND, SETTING_DELAY, SETTING_WAIT, CONFIG_DONE, CONFIG_RETRY } Phase;
static Phase phase;
static uint32_t phase_ms;
static uint8_t probe,probes,setting,attempt,port_attempts,setting_ack;
static uint8_t port_config[20];
/* Disable default NMEA; coherent UBX POSLLH/VELNED/SOL at 5 Hz, UTC at 1 Hz. */
static const uint8_t classes[]={0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,1,1,1,1};
static const uint8_t messages[]={1,2,3,5,0,4,2,0x12,6,0x21};
static const uint8_t rates[]={0,0,0,0,0,0,1,1,1,5};
static char sw_version[31]="unknown",hw_version[11]="unknown";
static struct { uint64_t us; uint16_t end; } boundaries[16];
static volatile uint8_t boundary_head,boundary_tail,irq_ubx_state;
static uint16_t irq_length,irq_index;
static uint64_t packet_us;
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
static HAL_StatusTypeDef send_frame(uint8_t cls,uint8_t id,const uint8_t *payload,uint8_t length)
{
    uint8_t packet[28]={0xB5,0x62,cls,id,length,0};
    if (length) memcpy(packet+6,payload,length);
    uint8_t a=0,b=0;
    for (unsigned i=2;i<6U+length;++i) { a+=packet[i]; b+=a; }
    packet[6U+length]=a; packet[7U+length]=b;
    return HAL_UART_Transmit(gps_uart,packet,(uint16_t)(length+8U),80U);
}
static HAL_StatusTypeDef send_ubx(uint8_t id,const uint8_t *payload,uint8_t length)
{ return send_frame(6,id,payload,length); }
static uint8_t change_baud(uint32_t baud)
{
    if (HAL_UART_AbortReceive(gps_uart)!=HAL_OK) return 0;
    tail=head; ubx_state=0; restart_rx=overflow=0;
    boundary_tail=boundary_head; irq_ubx_state=0;
    gps_uart->Init.BaudRate=baud;
    if (HAL_UART_Init(gps_uart)!=HAL_OK) return 0;
    configuration.baud=baud;
    return HAL_UART_Receive_IT(gps_uart,&rx_byte,1U)==HAL_OK;
}
static void discover(void)
{
    GPS_Epoch_ResetStream(&data,Timebase_NowUs());
    configuration.state=GPS_CONFIG_PENDING; configuration.error=GPS_CFG_OK;
    configuration.measurement_ms=configuration.nav_rate=configuration.time_ref=0;
    configuration.attempts=0;
    probe=(gps_uart->Init.BaudRate==GPS_RUN_BAUD); probes=setting=attempt=port_attempts=0;
    phase=DISCOVER_SEND;
}
static void packet_received(uint32_t now)
{
    uint64_t us=packet_us;
    GPS_Epoch_Service(&data,us);
    GPS_Epoch_Packet(&data,ubx_class,ubx_id,ubx_payload,ubx_length,us);
    if (ubx_class==0x0A && ubx_id==4 && ubx_length>=40) {
        memcpy(sw_version,ubx_payload,30); sw_version[30]=0;
        memcpy(hw_version,ubx_payload+30,10); hw_version[10]=0;
        if (phase==INFO_WAIT) phase=SETTING_SEND;
    }
    if (ubx_class==5 && ubx_id==1 && ubx_length==2 && ubx_payload[0]==6 &&
        (phase==SETTING_DELAY || phase==SETTING_WAIT) && ubx_payload[1]==configuration.message_id)
        setting_ack=1;
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
            setting=attempt=0; phase=INFO_SEND;
        } else phase=PORT_SET;
    } else if (phase==SETTING_WAIT && setting<sizeof(messages) && ubx_id==1 &&
               (ubx_length==3 || ubx_length==8) && ubx_payload[0]==classes[setting] && ubx_payload[1]==messages[setting]) {
        uint8_t rate=ubx_payload[ubx_length==8 ? 3 : 2]; /* Receiver UART1/current port. */
        if (rate==rates[setting] && setting_ack) { ++setting; attempt=0; phase=SETTING_SEND; }
        else config_error(GPS_CFG_VERIFY,now);
    } else if (phase==SETTING_WAIT && setting==sizeof(messages) && ubx_id==8 && ubx_length==6) {
        configuration.measurement_ms=le16(ubx_payload);
        configuration.nav_rate=le16(ubx_payload+2); configuration.time_ref=le16(ubx_payload+4);
        if (configuration.measurement_ms==GPS_MEASUREMENT_MS && configuration.nav_rate==1 && configuration.time_ref==1 && setting_ack) {
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
        if (ubx_check==ubx_a && ch==ubx_b && (ubx_length<=sizeof(ubx_payload) || (ubx_class==0x0A && ubx_id==4))) packet_received(now);
        else if (ubx_check!=ubx_a || ch!=ubx_b) ++gps_diag.checksum_errors;
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
        setting_ack=0;
        configuration.attempts=++attempt;
        if (setting<sizeof(messages)) {
            uint8_t payload[]={classes[setting],messages[setting],rates[setting]};
            configuration.message_id=1; result=send_ubx(1,payload,sizeof(payload));
        } else { configuration.message_id=8; result=send_ubx(8,rate,sizeof(rate)); }
        phase=SETTING_DELAY; phase_ms=now; break;
    case SETTING_DELAY:
        if ((uint32_t)(now-phase_ms)>=40U) {
            if (setting<sizeof(messages)) { uint8_t payload[]={classes[setting],messages[setting]}; result=send_ubx(1,payload,2); }
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
    case INFO_SEND:
        result=send_frame(0x0A,4,NULL,0); phase=INFO_WAIT; phase_ms=now; break;
    case INFO_WAIT:
        if ((uint32_t)(now-phase_ms)>=500U) phase=SETTING_SEND;
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
    gps_uart = uart; head = tail = 0;
    restart_rx = overflow = 0;
    GPS_NMEA_Init(&data);
    GPS_Epoch_Init();
    boundary_head=boundary_tail=irq_ubx_state=0;
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
    /* Only track frame length in IRQ; parsing/checksums stay in foreground. */
    uint8_t ch=rx_byte;
    switch (irq_ubx_state) {
    case 0: if (ch==0xB5) irq_ubx_state=1; break;
    case 1: irq_ubx_state=ch==0x62 ? 2 : (ch==0xB5 ? 1 : 0); break;
    case 2: irq_ubx_state=3; break;
    case 3: irq_ubx_state=4; break;
    case 4: irq_length=ch; irq_ubx_state=5; break;
    case 5: irq_length|=(uint16_t)ch<<8; irq_index=0; irq_ubx_state=irq_length>1024 ? 0 : (irq_length ? 6 : 7); break;
    case 6: if (++irq_index==irq_length) irq_ubx_state=7; break;
    case 7: irq_ubx_state=8; break;
    default: {
        uint8_t n=(boundary_head+1U)%16U;
        if (n==boundary_tail) overflow=1;
        else { boundaries[boundary_head].us=Timebase_NowUs(); boundaries[boundary_head].end=head;
#ifndef LAPTUNE_HOST_TEST
            __DMB();
#endif
            boundary_head=n; }
        irq_ubx_state=0; break;
    }
    }
    if (!restart_rx && HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U) != HAL_OK)
        restart_rx = 1;
}

void GPS_OnError(UART_HandleTypeDef *uart)
{
    if (uart == gps_uart && gps_uart != NULL) { restart_rx = 1; ++gps_diag.uart_errors; }
}

void GPS_Process(uint32_t now)
{
    if (gps_uart == NULL) return;
    if (overflow) { overflow=0; ++gps_diag.rx_overflows; restart_rx=1; }
    if (restart_rx) {
        restart_rx = 0;
        (void)HAL_UART_AbortReceive(gps_uart);
        tail = head; ubx_state = 0;
        boundary_tail=boundary_head; irq_ubx_state=0;
        GPS_Epoch_ResetStream(&data,Timebase_NowUs());
        if (HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U) != HAL_OK) restart_rx = 1;
    }
    /* Bounded work even if bytes keep arriving during parsing. */
    for (unsigned n = 0; n < RX_CAPACITY && tail != head; ++n) {
        uint8_t ch = queue[tail]; tail = (tail + 1U) % RX_CAPACITY;
        if (boundary_tail!=boundary_head && boundaries[boundary_tail].end==tail) {
#ifndef LAPTUNE_HOST_TEST
            __DMB();
#endif
            packet_us=boundaries[boundary_tail].us; boundary_tail=(boundary_tail+1U)%16U;
        }
        if (ubx_state) { ubx_byte(ch,now); continue; }
        if (ch==0xB5) ubx_state=1; /* Ignore NMEA/other noise; UBX is the only source. */
    }
    GPS_Epoch_Service(&data,Timebase_NowUs());
    configure(now);
}

const GPS_Data *GPS_GetData(void) { return &data; }
const GPS_ConfigStatus *GPS_GetConfig(void) { return &configuration; }
const char *GPS_SoftwareVersion(void) { return sw_version; }
const char *GPS_HardwareVersion(void) { return hw_version; }
