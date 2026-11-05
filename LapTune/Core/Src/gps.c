#include "gps.h"
#include "timebase.h"
#include "log_clock.h"
#include <stddef.h>
#include <string.h>

/* One verified, volatile UART1 output-mask change. Every other transmission
   is a poll; no reset, restore, save, baud/rate or timepulse setters. */
#define RX_CAPACITY 512U
static UART_HandleTypeDef *gps_uart;
static uint8_t rx_byte,tx_packet[28];
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
typedef struct { const char *name; uint8_t cls,id,len,arg0,arg1; uint16_t reply_len; uint8_t op; } Query;
static const Query queries[]={
#include "gps_queries.inc"
};
static const Query monitor_queries[]={
    {"NAV-STATUS",1,3,0,0,0,16,0}, {"NAV-SVINFO",1,0x30,0,0,0,8,0},
    {"MON-HW",10,9,0,0,0,60,0}
};
#define CONFIG_COUNT (sizeof(queries)/sizeof(queries[0]))
#define MONITOR_ROUNDS 20U
#define QUERY_COUNT (CONFIG_COUNT+3U*MONITOR_ROUNDS)
static uint8_t port_before[20],port_valid;
static GPS_MaskState mask_state;
static uint16_t query_index;
static const Query *current_query(void)
{ return query_index<CONFIG_COUNT ? &queries[query_index] : &monitor_queries[(query_index-CONFIG_COUNT)%3U]; }
static GPS_QueryResult result;
static uint32_t query_ms;
static uint8_t query_waiting,query_attempts,query_pending,query_done;
static uint8_t ubx_capture,ubx_state,ubx_class,ubx_id,ubx_a,ubx_b,ubx_check,payload[400];
static uint16_t ubx_length,ubx_index;
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static void finish_query(GPS_QueryStatus status,uint32_t now)
{
    const Query *q=current_query();
    result.name=q->name; result.status=status; result.t_ms=now;
    result.index=query_index+1; result.total=QUERY_COUNT; result.attempts=query_attempts;
    result.query_class=q->cls; result.query_id=q->id;
    result.payload=payload; result.mask_state=mask_state;
    result.cycle=query_index<CONFIG_COUNT ? 0 : (query_index-CONFIG_COUNT)/3U+1U;
    query_waiting=0; query_pending=1;
}
const GPS_QueryResult *GPS_QueryPending(void) { return query_pending ? &result : NULL; }
void GPS_QuerySaved(void)
{
    if (!query_pending) return;
    query_pending=0; query_attempts=0;
    if (++query_index==QUERY_COUNT) query_done=1;
    query_ms=(uint32_t)(Timebase_NowUs()/1000U);
}
unsigned GPS_QueryProgress(void) { return query_index; }
unsigned GPS_QueryCount(void) { return QUERY_COUNT; }
unsigned GPS_ConfigQueryCount(void) { return CONFIG_COUNT; }
static uint8_t change_baud(uint32_t baud)
{
    if (tx_busy || HAL_UART_AbortReceive(gps_uart)!=HAL_OK) return 0;
    tail=head; line_len=ubx_state=restart_rx=overflow=0; boundary_tail=boundary_head;
    gps_uart->Init.BaudRate=baud; configuration.baud=baud;
    if (HAL_UART_Init(gps_uart)!=HAL_OK) return 0;
    return HAL_UART_Receive_IT(gps_uart,&rx_byte,1)==HAL_OK;
}
static HAL_StatusTypeDef send_poll(void)
{
    /* Only op=2 can send a setter: copy all UART1 bytes, alter just outProtoMask.
       query_service permits it only after a validated 9600/0x0007 baseline. */
    const Query *q=current_query();
    if (tx_busy) return HAL_BUSY;
    tx_packet[0]=0xB5; tx_packet[1]=0x62; tx_packet[2]=q->cls; tx_packet[3]=q->id;
    tx_packet[4]=q->len; tx_packet[5]=0;
    if (q->op==2) {
        if (!port_valid || mask_state!=GPS_MASK_READY) return HAL_ERROR;
        memcpy(tx_packet+6,port_before,20); tx_packet[20]=3; tx_packet[21]=0;
        mask_state=GPS_MASK_SENT;
    } else if (q->len) tx_packet[6]=q->arg0;
    if (q->len==2) tx_packet[7]=q->arg1;
    uint8_t a=0,b=0;
    for (unsigned i=2;i<6U+q->len;++i) { a+=tx_packet[i]; b+=a; }
    tx_packet[6U+q->len]=a; tx_packet[7U+q->len]=b;
    result.tx_frame=tx_packet; result.tx_length=q->len+8U;
    tx_started=Timebase_NowUs(); tx_busy=1;
    HAL_StatusTypeDef status=HAL_UART_Transmit_IT(gps_uart,tx_packet,q->len+8U);
    if (status!=HAL_OK) { tx_busy=0; ++gps_diag.tx_errors; }
    return status;
}
static void received(uint32_t now)
{
    if (!query_waiting || query_pending || query_done) return;
    const Query *q=current_query();
    uint8_t nak=ubx_class==5 && ubx_id==0 && ubx_length==2 && payload[0]==q->cls && payload[1]==q->id;
    uint8_t ack=ubx_class==5 && ubx_id==1 && ubx_length==2 && payload[0]==q->cls && payload[1]==q->id;
    if (q->op==2) {
        if (!ack && !nak) return;
        mask_state=nak ? GPS_MASK_REJECTED : GPS_MASK_ACKED;
    } else if (!nak) {
        if (ubx_class!=q->cls || ubx_id!=q->id) return;
        if (q->id==1 && q->cls==6 && ubx_length>=2 && (payload[0]!=q->arg0 || payload[1]!=q->arg1)) return;
        if (q->id==0 && q->cls==6 && ubx_length && payload[0]!=q->arg0) return;
        if (q->cls==6 && (q->id==2 || q->id==0x31) && ubx_length && payload[0]!=q->arg0) return;
    }
    result.rx_present=1; result.rx_class=ubx_class; result.rx_id=ubx_id; result.length=ubx_length;
    result.stored=ubx_length<sizeof(payload) ? ubx_length : sizeof(payload);
    result.ck_a=ubx_a; result.ck_b=ubx_b;
    GPS_QueryStatus status=GPS_QUERY_OK;
    if (nak) status=GPS_QUERY_NAK;
    else if (ubx_length>sizeof(payload)) status=GPS_QUERY_TRUNCATED;
    else if (ubx_length<q->reply_len || (q->cls==6 && q->id==1 && ubx_length!=3 && ubx_length!=8) ||
        (q->cls==1 && q->id==0x30 && ubx_length!=8U+12U*payload[4])) status=GPS_QUERY_MALFORMED;
    if (status==GPS_QUERY_OK && q->op==1) {
        if (ubx_length!=20 || memcmp(payload+8,"\x80\x25\x00\x00",4)) status=GPS_QUERY_MALFORMED;
        else {
            memcpy(port_before,payload,20); port_valid=1;
            mask_state=le16(payload+14)==7 ? GPS_MASK_READY :
                (le16(payload+14)==3 ? GPS_MASK_ALREADY : GPS_MASK_UNEXPECTED);
        }
    }
    if (status==GPS_QUERY_OK && q->op==3 && port_valid) {
        uint8_t same=ubx_length==20 && le16(payload+14)==3;
        for (unsigned i=0;i<20 && same;++i) if (i!=14 && i!=15 && payload[i]!=port_before[i]) same=0;
        if (same) mask_state=GPS_MASK_VERIFIED;
        else { mask_state=GPS_MASK_VERIFY_FAILED; status=GPS_QUERY_VERIFY_FAILED; }
    }
    if (status==GPS_QUERY_OK && q->cls==6 && q->id==8) {
        configuration.measurement_ms=le16(payload); configuration.nav_rate=le16(payload+2); configuration.time_ref=le16(payload+4);
    }
    finish_query(status,now);
}
static void query_service(uint32_t now)
{
    if (query_pending || query_done) return;
    if (query_waiting) {
        if ((uint32_t)(now-query_ms)<1500U) return;
        query_waiting=0;
        if (query_attempts>=2 || current_query()->op==2) { finish_query(GPS_QUERY_TIMEOUT,now); return; }
    } else {
        uint32_t delay=!query_index ? 3000U : 250U;
        if (query_index>=CONFIG_COUNT && (query_index-CONFIG_COUNT)%3U==0) delay=30000U;
        if ((uint32_t)(now-query_ms)<delay) return;
    }
    if (tx_busy) return;
    result=(GPS_QueryResult){0};
    if (current_query()->op==2 && (!port_valid || mask_state!=GPS_MASK_READY)) {
        finish_query(GPS_QUERY_SKIPPED,now); return;
    }
    ++query_attempts; query_waiting=1; query_ms=now;
    if (send_poll()!=HAL_OK) finish_query(GPS_QUERY_TX_ERROR,now);
}
static void checksum_error(void)
{
    uint64_t now=Timebase_NowUs();
    if (!gps_diag.checksum_errors) gps_diag.checksum_first_us=now;
    gps_diag.checksum_last_us=now; ++gps_diag.checksum_errors;
    ++gps_diag.checksum_by_phase[1];
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
    case 5: ubx_length|=(uint16_t)ch<<8; ubx_index=0; ubx_capture=!query_pending; ubx_state=ubx_length>1024 ? 0 : (ubx_length ? 6 : 7); break;
    case 6: if (ubx_capture && !query_pending && ubx_index<sizeof(payload)) payload[ubx_index]=ch; if (++ubx_index==ubx_length) ubx_state=7; break;
    case 7: ubx_check=ch; ubx_state=8; break;
    default:
        ubx_state=0;
        if (ubx_check!=ubx_a || ch!=ubx_b) checksum_error();
        else if (ubx_capture) received(now);
        break;
    }
}
HAL_StatusTypeDef GPS_Init(UART_HandleTypeDef *uart)
{
    if (!uart) return HAL_ERROR;
    gps_uart=uart; head=tail=line_len=0; restart_rx=overflow=tx_busy=0;
    boundary_head=boundary_tail=ubx_state=0; packet_us=0;
    GPS_NMEA_Init(&data); GPS_Epoch_InitNMEA(); LogClock_UseNMEA();
    memset(&configuration,0,sizeof(configuration)); configuration.baud=GPS_BOOT_BAUD;
    configuration.state=GPS_CONFIG_PASSIVE;
    query_index=query_waiting=query_attempts=query_pending=query_done=port_valid=0;
    mask_state=GPS_MASK_NO_BASELINE;
    query_ms=(uint32_t)(Timebase_NowUs()/1000U); result=(GPS_QueryResult){0};
    return change_baud(GPS_BOOT_BAUD) ? HAL_OK : HAL_ERROR;
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
    unsigned phase_index=1; /* Polling does not reconfigure the UART or receiver. */
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
        (void)HAL_UART_AbortTransmit(gps_uart); tx_busy=0; ++gps_diag.tx_errors;
        if (!query_pending && !query_done) finish_query(GPS_QUERY_TX_ERROR,now);
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
    query_service(now);
}
const GPS_Data *GPS_GetData(void) { return &data; }
const GPS_ConfigStatus *GPS_GetConfig(void) { return &configuration; }
const char *GPS_SoftwareVersion(void) { return "unknown"; }
const char *GPS_HardwareVersion(void) { return "unknown"; }
