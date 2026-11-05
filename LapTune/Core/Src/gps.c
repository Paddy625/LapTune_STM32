#include "gps.h"
#include "timebase.h"
#include "log_clock.h"
#include <stddef.h>
#include <string.h>

/* Normal operation is the receive-only NMEA path from 1fd016f, at 9600.
   UBX below is used ONLY for the explicitly reserved factory rollback. */
#define RX_CAPACITY 512U
static UART_HandleTypeDef *gps_uart;
static uint8_t rx_byte,tx_packet[21];
static volatile uint8_t tx_busy,restart_rx,overflow;
static volatile uint16_t head,tail;
static volatile uint8_t queue[RX_CAPACITY];
static uint64_t boundary_times[16],packet_us,tx_started;
static uint16_t boundary_ends[16];
static volatile uint8_t boundary_head,boundary_tail;
static char line[GPS_NMEA_MAX_LENGTH];
static uint16_t line_len;
static GPS_Data data;
static GPS_ConfigStatus configuration;
typedef enum { PASSIVE, FIND_SEND,FIND_WAIT,DEFAULTS_SEND,DEFAULTS_WAIT,
               COLD_SEND,COLD_WAIT,VERIFY_RATE_SEND,VERIFY_RATE_WAIT,
               VERIFY_MSG_SEND,VERIFY_MSG_WAIT } Phase;
static Phase phase;
static uint32_t phase_ms;
static uint8_t probes,message_index;
static uint8_t ubx_state,ubx_class,ubx_id,ubx_a,ubx_b,ubx_check,payload[20];
static uint16_t ubx_length,ubx_index;
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p)|((uint32_t)le16(p+2)<<16); }
static uint8_t change_baud(uint32_t baud)
{
    if (tx_busy || HAL_UART_AbortReceive(gps_uart)!=HAL_OK) return 0;
    tail=head; line_len=ubx_state=restart_rx=overflow=0; boundary_tail=boundary_head;
    gps_uart->Init.BaudRate=baud; configuration.baud=baud;
    if (HAL_UART_Init(gps_uart)!=HAL_OK) return 0;
    return HAL_UART_Receive_IT(gps_uart,&rx_byte,1)==HAL_OK;
}
static void fail(GPS_ConfigError error)
{
    configuration.state=GPS_CONFIG_ERROR; configuration.error=error;
    configuration.restore_state=GPS_RESTORE_FAILED; configuration.restore_error=error;
    phase=PASSIVE;
    /* Failure must never start another reset/configuration cycle. */
    if (gps_uart->Init.BaudRate!=GPS_BOOT_BAUD) (void)change_baud(GPS_BOOT_BAUD);
}
static HAL_StatusTypeDef send(uint8_t id,const uint8_t *bytes,uint8_t length)
{
    if (tx_busy || length>13) return HAL_BUSY;
    tx_packet[0]=0xB5; tx_packet[1]=0x62; tx_packet[2]=6; tx_packet[3]=id;
    tx_packet[4]=length; tx_packet[5]=0;
    if (length) memcpy(tx_packet+6,bytes,length);
    uint8_t a=0,b=0;
    for (unsigned i=2;i<6U+length;++i) { a+=tx_packet[i]; b+=a; }
    tx_packet[6U+length]=a; tx_packet[7U+length]=b;
    tx_started=Timebase_NowUs(); tx_busy=1;
    HAL_StatusTypeDef status=HAL_UART_Transmit_IT(gps_uart,tx_packet,length+8U);
    if (status!=HAL_OK) { tx_busy=0; ++gps_diag.tx_errors; }
    return status;
}
static void received(uint32_t now)
{
    (void)now;
    if (phase==PASSIVE) return;
    if (ubx_class==5 && ubx_length==2 && payload[0]==6) {
        uint8_t expected=phase==DEFAULTS_WAIT ? 9 : (phase==COLD_WAIT ? 4 :
            (phase==FIND_WAIT ? 0 : (phase==VERIFY_RATE_WAIT ? 8 : 1)));
        if (payload[1]==expected) {
            if (ubx_id==0) { fail(GPS_CFG_NAK); return; }
            if (ubx_id==1 && phase==DEFAULTS_WAIT) configuration.restore_ack|=1;
            if (ubx_id==1 && phase==COLD_WAIT) configuration.restore_ack|=2;
        }
    }
    if (ubx_class!=6) return;
    if (phase==FIND_WAIT && ubx_id==0 && ubx_length==20 && payload[0]==1) {
        if (le32(payload+8)!=gps_uart->Init.BaudRate) return;
        if (configuration.restore_state==GPS_RESTORE_PENDING) phase=DEFAULTS_SEND;
        else if (configuration.restore_state==GPS_RESTORE_DEFAULTS_SENT) phase=COLD_SEND;
        else {
            if (le32(payload+8)!=GPS_BOOT_BAUD || !(le16(payload+14)&2U)) { fail(GPS_CFG_VERIFY); return; }
            phase=VERIFY_RATE_SEND;
        }
    } else if (phase==VERIFY_RATE_WAIT && ubx_id==8 && ubx_length==6) {
        configuration.measurement_ms=le16(payload); configuration.nav_rate=le16(payload+2); configuration.time_ref=le16(payload+4);
        if (configuration.measurement_ms!=1000 || configuration.nav_rate!=1 || configuration.time_ref!=1) { fail(GPS_CFG_VERIFY); return; }
        message_index=0; phase=VERIFY_MSG_SEND;
    } else if (phase==VERIFY_MSG_WAIT && ubx_id==1 && (ubx_length==3 || ubx_length==8) &&
               payload[0]==0xF0 && payload[1]==message_index) {
        if (payload[ubx_length==8 ? 3 : 2]!=1) { fail(GPS_CFG_VERIFY); return; }
        if (++message_index<6) phase=VERIFY_MSG_SEND;
        else {
            configuration.state=GPS_CONFIG_READY; configuration.restore_state=GPS_RESTORE_RECONFIGURED;
            phase=PASSIVE; /* No more GPS transmissions, including after fix or an outage. */
        }
    }
}
static void checksum_error(void)
{
    uint64_t now=Timebase_NowUs();
    if (!gps_diag.checksum_errors) gps_diag.checksum_first_us=now;
    gps_diag.checksum_last_us=now; ++gps_diag.checksum_errors;
    ++gps_diag.checksum_by_phase[phase==PASSIVE];
}
static int hex_digit(char ch)
{
    if (ch>='0' && ch<='9') return ch-'0';
    if (ch>='A' && ch<='F') return ch-'A'+10;
    if (ch>='a' && ch<='f') return ch-'a'+10;
    return -1;
}
static uint8_t nmea_checksum_ok(const char *text)
{
    const char *star=strchr(text,'*');
    if (!star || strlen(star)!=3 || hex_digit(star[1])<0 || hex_digit(star[2])<0) return 0;
    uint8_t sum=0;
    for (const char *p=text+1;p<star;++p) sum^=(uint8_t)*p;
    return sum==(uint8_t)((hex_digit(star[1])<<4)|hex_digit(star[2]));
}
static void ubx_byte(uint8_t ch,uint32_t now)
{
    if (ubx_state==1) { ubx_state=ch==0x62 ? 2 : (ch==0xB5 ? 1 : 0); ubx_a=ubx_b=0; return; }
    if (ubx_state<=6) { ubx_a+=ch; ubx_b+=ubx_a; }
    switch (ubx_state) {
    case 2: ubx_class=ch; ubx_state=3; break;
    case 3: ubx_id=ch; ubx_state=4; break;
    case 4: ubx_length=ch; ubx_state=5; break;
    case 5: ubx_length|=(uint16_t)ch<<8; ubx_index=0; ubx_state=ubx_length>1024 ? 0 : (ubx_length ? 6 : 7); break;
    case 6: if (ubx_index<sizeof(payload)) payload[ubx_index]=ch; if (++ubx_index==ubx_length) ubx_state=7; break;
    case 7: ubx_check=ch; ubx_state=8; break;
    default:
        ubx_state=0;
        if (ubx_check!=ubx_a || ch!=ubx_b) checksum_error();
        else if (ubx_length<=sizeof(payload)) received(now);
        break;
    }
}
static void restore(uint32_t now)
{
    HAL_StatusTypeDef result=HAL_OK;
    switch (phase) {
    case PASSIVE: return;
    case FIND_SEND: {
        const uint8_t port=1; result=send(0,&port,1); phase=FIND_WAIT; phase_ms=now; break;
    }
    case FIND_WAIT:
        if ((uint32_t)(now-phase_ms)>=500U) {
            if (++probes>=4) fail(GPS_CFG_NO_RESPONSE);
            else if (!change_baud(gps_uart->Init.BaudRate==9600 ? 38400 : 9600)) fail(GPS_CFG_UART);
            else phase=FIND_SEND;
        }
        break;
    case DEFAULTS_SEND: {
        /* NEO-6 CFG-CFG: defined sections 0..4, 9, 10; reserved bits zero. */
        static const uint8_t defaults[]={0x1F,0x06,0,0,0,0,0,0,0x1F,0x06,0,0,0x17};
        configuration.restore_state=GPS_RESTORE_DEFAULTS_SENT;
        phase=DEFAULTS_WAIT; phase_ms=now; result=send(9,defaults,sizeof(defaults)); break;
    }
    case COLD_SEND: {
        static const uint8_t cold[]={255,255,1,0};
        configuration.restore_state=GPS_RESTORE_COLD_SENT;
        phase=COLD_WAIT; phase_ms=now;
        ++gps_diag.recovery_attempts; gps_diag.recovery_us=Timebase_NowUs();
        result=send(4,cold,sizeof(cold)); break;
    }
    case DEFAULTS_WAIT:
    case COLD_WAIT:
        if (!tx_busy && (uint32_t)(now-phase_ms)>=1500U) {
            if (!change_baud(GPS_BOOT_BAUD)) fail(GPS_CFG_UART);
            else { probes=0; phase=FIND_SEND; }
        }
        break;
    case VERIFY_RATE_SEND: result=send(8,NULL,0); phase=VERIFY_RATE_WAIT; phase_ms=now; break;
    case VERIFY_MSG_SEND: {
        const uint8_t msg[]={0xF0,message_index}; result=send(1,msg,2); phase=VERIFY_MSG_WAIT; phase_ms=now; break;
    }
    default: if ((uint32_t)(now-phase_ms)>=500U) fail(GPS_CFG_NO_RESPONSE); break;
    }
    if (result!=HAL_OK) fail(GPS_CFG_UART);
}
HAL_StatusTypeDef GPS_Init(UART_HandleTypeDef *uart)
{
    if (!uart) return HAL_ERROR;
    gps_uart=uart; head=tail=line_len=0; restart_rx=overflow=tx_busy=0;
    boundary_head=boundary_tail=ubx_state=0; packet_us=0;
    GPS_NMEA_Init(&data); GPS_Epoch_InitNMEA(); LogClock_UseNMEA();
    memset(&configuration,0,sizeof(configuration)); configuration.baud=GPS_BOOT_BAUD;
    configuration.state=GPS_CONFIG_PASSIVE; phase=PASSIVE;
    return change_baud(GPS_BOOT_BAUD) ? HAL_OK : HAL_ERROR;
}
void GPS_RequestRestore(void)
{
    if (gps_uart && phase==PASSIVE && configuration.restore_state==GPS_RESTORE_NONE) {
        configuration.restore_state=GPS_RESTORE_PENDING; configuration.state=GPS_CONFIG_PENDING;
        probes=0; phase=FIND_SEND;
    }
}
void GPS_OnRxComplete(UART_HandleTypeDef *uart)
{
    if (uart!=gps_uart || !gps_uart) return;
    uint16_t next=(head+1U)%RX_CAPACITY;
    if (next==tail) overflow=1;
    else {
        queue[head]=rx_byte; head=next;
        if (rx_byte=='\n') {
            uint8_t n=(boundary_head+1U)%16U;
            if (n==boundary_tail) overflow=1;
            else {
                boundary_times[boundary_head]=Timebase_NowUs(); boundary_ends[boundary_head]=head;
#ifndef LAPTUNE_HOST_TEST
                __DMB();
#endif
                boundary_head=n;
            }
        }
    }
    if (!restart_rx && HAL_UART_Receive_IT(gps_uart,&rx_byte,1)!=HAL_OK) restart_rx=1;
}
void GPS_OnTxComplete(UART_HandleTypeDef *uart) { if (uart==gps_uart) tx_busy=0; }
void GPS_OnError(UART_HandleTypeDef *uart)
{
    if (uart!=gps_uart || !gps_uart) return;
    uint64_t t=Timebase_NowUs();
    if (!gps_diag.uart_errors) gps_diag.uart_first_us=t;
    gps_diag.uart_last_us=t; ++gps_diag.uart_errors;
    const uint32_t bits[]={HAL_UART_ERROR_FE,HAL_UART_ERROR_NE,HAL_UART_ERROR_ORE,HAL_UART_ERROR_PE};
    uint32_t error=uart->ErrorCode,known=0;
    unsigned phase_index=phase==PASSIVE;
    uint32_t previous=0; for (unsigned i=0;i<5;++i) previous+=gps_diag.uart_by_phase[phase_index][i];
    if (!previous) gps_diag.uart_phase_first_us[phase_index]=t;
    gps_diag.uart_phase_last_us[phase_index]=t;
    for (unsigned i=0;i<4;++i) { known|=bits[i]; if (error&bits[i]) ++gps_diag.uart_by_phase[phase_index][i]; }
    if (!error || (error&~known)) ++gps_diag.uart_by_phase[phase_index][4];
    restart_rx=1;
}
void GPS_DiagnosticsSnapshot(GPS_Diagnostics *out)
{
#ifndef LAPTUNE_HOST_TEST
    uint32_t mask=__get_PRIMASK(); __disable_irq();
#endif
    *out=gps_diag;
#ifndef LAPTUNE_HOST_TEST
    __set_PRIMASK(mask);
#endif
}
void GPS_Process(uint32_t now)
{
    if (!gps_uart) return;
    if (tx_busy && Timebase_NowUs()-tx_started>100000U) {
        (void)HAL_UART_AbortTransmit(gps_uart); tx_busy=0; ++gps_diag.tx_errors; fail(GPS_CFG_UART);
    }
    if (overflow) { overflow=0; ++gps_diag.rx_overflows; restart_rx=1; }
    if (restart_rx) {
        restart_rx=0; (void)HAL_UART_AbortReceive(gps_uart);
        tail=head; line_len=ubx_state=0; boundary_tail=boundary_head;
        GPS_Epoch_ResetStream(&data,Timebase_NowUs());
        if (HAL_UART_Receive_IT(gps_uart,&rx_byte,1)!=HAL_OK) restart_rx=1;
    }
    for (unsigned n=0;n<RX_CAPACITY && tail!=head;++n) {
        uint8_t ch=queue[tail]; tail=(tail+1U)%RX_CAPACITY;
        if (boundary_tail!=boundary_head && boundary_ends[boundary_tail]==tail) {
#ifndef LAPTUNE_HOST_TEST
            __DMB();
#endif
            packet_us=boundary_times[boundary_tail]; boundary_tail=(boundary_tail+1U)%16U;
        }
        if (ubx_state) { ubx_byte(ch,now); continue; }
        if (ch==0xB5) { ubx_state=1; line_len=0; continue; }
        if (ch=='$') { line_len=0; line[line_len++]='$'; }
        else if (ch=='\n' && line_len) {
            line[line_len]=0;
            if (!nmea_checksum_ok(line)) checksum_error();
            else if (GPS_NMEA_Parse(&data,line,now)) {
                ++gps_diag.nmea_sentences; gps_diag.nmea_last_us=packet_us;
                if (data.position_valid) { ++gps_diag.nmea_fix_sentences; gps_diag.nmea_last_fix_us=packet_us; }
                GPS_Epoch_NMEA(&data,line,packet_us); LogClock_Update(&data);
            }
            line_len=0;
        } else if (ch!='\r' && line_len) {
            if (line_len<sizeof(line)-1U) line[line_len++]=(char)ch; else line_len=0;
        }
    }
    GPS_NMEA_Expire(&data,now);
    data.receiver_fix_valid=data.position_valid;
    GPS_Epoch_Service(&data,Timebase_NowUs());
    restore(now);
}
const GPS_Data *GPS_GetData(void) { return &data; }
const GPS_ConfigStatus *GPS_GetConfig(void) { return &configuration; }
const char *GPS_SoftwareVersion(void) { return "unknown"; }
const char *GPS_HardwareVersion(void) { return "unknown"; }
