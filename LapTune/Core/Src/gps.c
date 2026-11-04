#include "gps.h"
#include "timebase.h"
#include "gps_time.h"
#include <stddef.h>
#include <string.h>

#define RX_CAPACITY 1024U
static UART_HandleTypeDef *gps_uart;
static uint8_t rx_byte;
static uint8_t tx_packet[28];
static volatile uint8_t tx_busy;
static uint64_t tx_started;
static volatile uint8_t queue[RX_CAPACITY];
static volatile uint16_t head, tail;
static volatile uint8_t restart_rx, overflow;
static GPS_Data data;
static GPS_ConfigStatus configuration;
typedef enum { DISCOVER_SEND, DISCOVER_WAIT, PORT_SET, SWITCH_WAIT,
               INFO_SEND,INFO_WAIT,SETTING_SEND, SETTING_DELAY, SETTING_WAIT, ACQUIRE, RECOVERY_WAIT, CONFIG_DONE, CONFIG_RETRY } Phase;
static Phase phase;
static uint32_t phase_ms;
static uint8_t probe,probes,setting,attempt,port_attempts,setting_ack,pulse_loaded;
static uint8_t port_config[20];
/* Restore the proven GGA/RMC startup profile. Add coherent UBX only after fix. */
static const uint8_t classes[]={0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,1,1,1,1};
static const uint8_t messages[]={1,2,3,5,0,4,2,0x12,6,0x21};
static const uint8_t rates[]={0,0,0,0,1,1,1,1,1,5};
enum { NAV_RATE_SETTING=sizeof(messages), PULSE_SETTING, TIMTP_SETTING, ALL_SETTINGS };
static char sw_version[31]="unknown",hw_version[11]="unknown";
static uint64_t boundary_times[16];
static uint16_t boundary_ends[16];
static volatile uint8_t boundary_head,boundary_tail,irq_ubx_state;
static uint16_t irq_length,irq_index;
static uint64_t packet_us;
static uint8_t ubx_state,ubx_class,ubx_id,ubx_a,ubx_b,ubx_check;
static uint16_t ubx_length,ubx_index;
static uint8_t ubx_payload[64];
static uint8_t diagnostic_index,sky_nonzero,sky_locked,sky_max;
static uint64_t no_signal_since;
/* NMEA is a startup/health signal only. Stream fields without a line buffer or
   GPS_NMEA_Parse's stack frame (this runs inside the storage service hook). */
static struct {
    char type[6];
    uint8_t state,sum,check,field,length,fix,status,satellites,satellites_valid;
} nmea;

static uint8_t message_rate(void)
{
    if (setting==TIMTP_SETTING) return configuration.acquisition_only ? 0 : 1;
    return configuration.acquisition_only && setting>=6 ? 0 : rates[setting];
}
static int hex_digit(uint8_t ch)
{
    if (ch>='0' && ch<='9') return ch-'0';
    if (ch>='A' && ch<='F') return ch-'A'+10;
    if (ch>='a' && ch<='f') return ch-'a'+10;
    return -1;
}
static void nmea_byte(uint8_t ch)
{
    if (ch=='$') { memset(&nmea,0,sizeof(nmea)); nmea.state=1; return; }
    if (!nmea.state) return;
    if (nmea.state>=2) {
        int digit=hex_digit(ch);
        if (digit<0) { nmea.state=0; return; }
        if (nmea.state==2) { nmea.check=(uint8_t)(digit<<4); nmea.state=3; return; }
        nmea.state=0;
        if ((nmea.check|(uint8_t)digit)!=nmea.sum || !nmea.type[0] ||
            nmea.field<(nmea.type[2]=='R' ? 9 : 10)) return;
        uint64_t us=Timebase_NowUs();
        ++gps_diag.nmea_sentences; gps_diag.nmea_last_us=us;
        if (nmea.fix) { ++gps_diag.nmea_fix_sentences; gps_diag.nmea_last_fix_us=us; }
        if (configuration.acquisition_only) {
            /* NAV-SOL is disabled here. Display the actual receiver status
               without publishing NMEA coordinates into the UBX log history. */
            data.connected=1; data.sentence_ms=(uint32_t)(us/1000U);
            data.receiver_fix_valid=nmea.fix;
            if (nmea.type[2]=='G') data.satellites=nmea.satellites_valid ? nmea.satellites : 0;
        }
        return;
    }
    if (ch=='*') { nmea.state=2; return; }
    if (ch<' ' || ch>'~') { nmea.state=0; return; }
    nmea.sum^=ch;
    if (ch==',') {
        if (!nmea.field) {
            if (nmea.length!=5 || (memcmp(nmea.type,"GP",2) && memcmp(nmea.type,"GN",2)) ||
                (memcmp(nmea.type+2,"RMC",3) && memcmp(nmea.type+2,"GGA",3))) { nmea.state=0; return; }
        } else if ((nmea.type[2]=='R' && nmea.field==2) || (nmea.type[2]=='G' && nmea.field==6)) {
            nmea.fix=nmea.length==1 && (nmea.type[2]=='R' ? nmea.status=='A' : (nmea.status>='1' && nmea.status<='5'));
        }
        if (nmea.field==7 && !nmea.length) nmea.satellites_valid=0;
        if (++nmea.field>20) { nmea.state=0; return; }
        if (nmea.field==7) nmea.satellites_valid=1;
        nmea.length=0; return;
    }
    if (!nmea.field && nmea.length<5) nmea.type[nmea.length]=(char)ch;
    if (nmea.type[2]=='G' && nmea.field==7) {
        if (ch>='0' && ch<='9' && nmea.length<2) nmea.satellites=(uint8_t)(nmea.satellites*10U+ch-'0');
        else nmea.satellites_valid=0;
    }
    nmea.status=ch;
    if (++nmea.length>100) nmea.state=0;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p)|((uint32_t)le16(p+2)<<16); }
static void receiver_status(uint64_t us)
{
    GPS_ReceiverStatus *r=&gps_diag.receiver;
    if (ubx_class==1 && ubx_id==0x30 && ubx_length>=8 &&
        ubx_length==8U+12U*ubx_payload[4]) {
        r->sky_channels=ubx_payload[4]; r->cno_nonzero=sky_nonzero;
        r->code_locked=sky_locked; r->cno_max=sky_max; r->sky_rx_us=us; r->seen|=GPS_RX_SKY;
        if (sky_nonzero || sky_locked) no_signal_since=0;
    } else if (ubx_class==0x0A && ubx_id==9 && (ubx_length==60 || ubx_length==68)) {
        r->noise_per_ms=le16(ubx_payload+16); r->agc_count=le16(ubx_payload+18);
        r->antenna_status=ubx_payload[20]; r->antenna_power=ubx_payload[21]; r->hw_flags=ubx_payload[22];
        r->jamming=ubx_payload[ubx_length==68 ? 53 : 45];
        r->hw_rx_us=us; r->seen|=GPS_RX_HW;
    } else if (ubx_class==6 && ubx_id==0x24 && ubx_length==36) {
        r->dynamic_model=ubx_payload[2]; r->fix_mode=ubx_payload[3];
        r->min_elevation=(int8_t)((int16_t)ubx_payload[12]-(ubx_payload[12]>=128 ? 256 : 0));
        r->pdop_limit=le16(ubx_payload+14); r->seen|=GPS_RX_NAV5;
    } else if (ubx_class==6 && ubx_id==0x11 && ubx_length==2) {
        r->power_mode=ubx_payload[1]; r->seen|=GPS_RX_POWER;
    }
}
static void config_error(GPS_ConfigError reason,uint32_t now)
{
    if (!configuration.acquisition_only && setting>=PULSE_SETTING && configuration.state==GPS_CONFIG_READY) {
        configuration.pps_state=2; configuration.pps_error=reason;
        phase=CONFIG_DONE; phase_ms=now; return;
    }
    configuration.state=GPS_CONFIG_ERROR; configuration.error=reason;
    phase=CONFIG_RETRY; phase_ms=now;
}
static HAL_StatusTypeDef send_frame(uint8_t cls,uint8_t id,const uint8_t *payload,uint8_t length)
{
    if (tx_busy || length>20) return HAL_BUSY;
    uint8_t *packet=tx_packet;
    packet[0]=0xB5; packet[1]=0x62; packet[2]=cls; packet[3]=id; packet[4]=length; packet[5]=0;
    if (length) memcpy(packet+6,payload,length);
    uint8_t a=0,b=0;
    for (unsigned i=2;i<6U+length;++i) { a+=packet[i]; b+=a; }
    packet[6U+length]=a; packet[7U+length]=b;
    tx_started=Timebase_NowUs(); tx_busy=1;
    HAL_StatusTypeDef result=HAL_UART_Transmit_IT(gps_uart,packet,(uint16_t)(length+8U));
    if (result!=HAL_OK) { tx_busy=0; ++gps_diag.tx_errors; }
    return result;
}
static HAL_StatusTypeDef send_ubx(uint8_t id,const uint8_t *payload,uint8_t length)
{ return send_frame(6,id,payload,length); }
static void diagnostic_poll(void)
{
    static const uint8_t classes[]={1,0x0A,6,6},ids[]={0x30,9,0x24,0x11};
    if (tx_busy || data.position_valid || data.receiver_fix_valid) return;
    /* Poll only: never change acquisition, antenna, dynamics or power settings. */
    (void)send_frame(classes[diagnostic_index],ids[diagnostic_index],NULL,0);
    diagnostic_index=(diagnostic_index+1U)%4U;
}
static uint8_t change_baud(uint32_t baud)
{
    if (tx_busy || HAL_UART_AbortReceive(gps_uart)!=HAL_OK) return 0;
    tail=head; ubx_state=nmea.state=0; restart_rx=overflow=0;
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
    configuration.attempts=0; configuration.pps_state=configuration.pps_error=pulse_loaded=0;
    configuration.acquisition_only=1; no_signal_since=0;
    probe=(gps_uart->Init.BaudRate==GPS_RUN_BAUD); probes=setting=attempt=port_attempts=0;
    phase=DISCOVER_SEND;
}
static void packet_received(uint32_t now)
{
    uint64_t us=packet_us;
    GPS_Epoch_Service(&data,us);
    GPS_Epoch_Packet(&data,ubx_class,ubx_id,ubx_payload,ubx_length,us);
    receiver_status(us);
    if (ubx_class==0x0D && ubx_id==1 && configuration.pps_state==1)
        GPS_Time_PulseMessage(ubx_payload,ubx_length,us);
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
    } else if (phase==SETTING_WAIT) {
        uint8_t message_setting=setting<NAV_RATE_SETTING || setting==TIMTP_SETTING;
        if (message_setting && ubx_id==1 && (ubx_length==3 || ubx_length==8)) {
            uint8_t cls=setting<NAV_RATE_SETTING ? classes[setting] : 0x0D;
            uint8_t id=setting<NAV_RATE_SETTING ? messages[setting] : 1;
            uint8_t expected=message_rate();
            if (ubx_payload[0]!=cls || ubx_payload[1]!=id) return;
            if (ubx_payload[ubx_length==8 ? 3 : 2]!=expected || !setting_ack) { config_error(GPS_CFG_VERIFY,now); return; }
        } else if (setting==NAV_RATE_SETTING && ubx_id==8 && ubx_length==6) {
            configuration.measurement_ms=le16(ubx_payload);
            configuration.nav_rate=le16(ubx_payload+2); configuration.time_ref=le16(ubx_payload+4);
            if (configuration.measurement_ms!=GPS_MEASUREMENT_MS || configuration.nav_rate!=1 || configuration.time_ref!=1 || !setting_ack) {
                config_error(GPS_CFG_VERIFY,now); return;
            }
            configuration.state=GPS_CONFIG_READY; configuration.error=GPS_CFG_OK;
        } else if (setting==PULSE_SETTING && ubx_id==7 && ubx_length==20) {
            if (!pulse_loaded) {
                /* Preserve antenna/RF/user delays and undocumented legacy flags. */
                memcpy(port_config,ubx_payload,20);
                const uint8_t desired[]={0x40,0x42,0x0F,0,0xA0,0x86,1,0,1,1}; /* 1 Hz, 100 ms, positive, GPS time. */
                memcpy(port_config,desired,sizeof(desired)); pulse_loaded=1; attempt=0; phase=SETTING_SEND; return;
            }
            if (memcmp(port_config,ubx_payload,10) || !setting_ack) { config_error(GPS_CFG_VERIFY,now); return; }
        } else return;
        ++setting; attempt=0; phase=SETTING_SEND;
        if (configuration.acquisition_only && setting==PULSE_SETTING) setting=TIMTP_SETTING;
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
        sky_nonzero=sky_locked=sky_max=0;
        ubx_state=ubx_length>1024U ? 0 : (ubx_length ? 6 : 7); break;
    case 6:
        if (ubx_index<sizeof(ubx_payload)) ubx_payload[ubx_index]=ch;
        /* Summarise every SV block, including those beyond the 64-byte buffer.
           Results become visible only after the full frame passes checksum. */
        if (ubx_class==1 && ubx_id==0x30 && ubx_index>=8) {
            unsigned offset=(ubx_index-8U)%12U;
            if (offset==3 && (ch&7U)>=4) ++sky_locked;
            if (offset==4 && ch) { ++sky_nonzero; if (ch>sky_max) sky_max=ch; }
        }
        if (++ubx_index==ubx_length) ubx_state=7;
        break;
    case 7: ubx_check=ch; ubx_state=8; break;
    case 8:
        ubx_state=0;
        if (ubx_check==ubx_a && ch==ubx_b &&
            (ubx_length<=sizeof(ubx_payload) || (ubx_class==0x0A && (ubx_id==4 || ubx_id==9)) ||
             (ubx_class==1 && ubx_id==0x30))) packet_received(now);
        else if (ubx_check!=ubx_a || ch!=ubx_b) {
            if (!gps_diag.checksum_errors) gps_diag.checksum_first_us=packet_us;
            gps_diag.checksum_last_us=packet_us;
            ++gps_diag.checksum_errors; ++gps_diag.checksum_by_phase[configuration.state==GPS_CONFIG_READY];
        }
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
        if (setting==ALL_SETTINGS) {
            if (configuration.acquisition_only) { phase=ACQUIRE; phase_ms=now; break; }
            configuration.pps_state=1; configuration.pps_error=GPS_CFG_OK;
            phase=CONFIG_DONE; phase_ms=now; break;
        }
        setting_ack=0; configuration.attempts=++attempt;
        if (setting<NAV_RATE_SETTING || setting==TIMTP_SETTING) {
            uint8_t payload[]={setting<NAV_RATE_SETTING ? classes[setting] : 0x0D,
                setting<NAV_RATE_SETTING ? messages[setting] : 1,
                message_rate()};
            configuration.message_id=1; result=send_ubx(1,payload,3);
        } else if (setting==NAV_RATE_SETTING) {
            configuration.message_id=8; result=send_ubx(8,rate,sizeof(rate));
        } else {
            configuration.message_id=7;
            result=send_ubx(7,pulse_loaded ? port_config : NULL,pulse_loaded ? 20 : 0);
            if (!pulse_loaded) { phase=SETTING_WAIT; phase_ms=now; break; }
        }
        phase=SETTING_DELAY; phase_ms=now; break;
    case SETTING_DELAY:
        if ((uint32_t)(now-phase_ms)>=40U) {
            if (setting<NAV_RATE_SETTING || setting==TIMTP_SETTING) {
                uint8_t payload[]={setting<NAV_RATE_SETTING ? classes[setting] : 0x0D,
                    setting<NAV_RATE_SETTING ? messages[setting] : 1}; result=send_ubx(1,payload,2);
            } else result=send_ubx(configuration.message_id,NULL,0);
            phase=SETTING_WAIT; phase_ms=now;
        }
        break;
    case SETTING_WAIT:
        if ((uint32_t)(now-phase_ms)>=500U) {
            if (attempt>=3) config_error(GPS_CFG_NO_RESPONSE,now); else phase=SETTING_SEND;
        }
        break;
    case ACQUIRE:
        if (gps_diag.nmea_fix_sentences && Timebase_NowUs()-gps_diag.nmea_last_fix_us<1000000U) {
            configuration.acquisition_only=0; setting=6; attempt=0;
            configuration.state=GPS_CONFIG_PENDING; phase=SETTING_SEND; no_signal_since=0; break;
        }
        /* fall through */
    case CONFIG_DONE:
        if (!data.connected && (uint32_t)(now-data.sentence_ms)>=GPS_FRESH_MS) discover();
        else if ((uint32_t)(now-phase_ms)>=1000U) { diagnostic_poll(); phase_ms=now; }
        break;
    case RECOVERY_WAIT:
        /* CFG-RST need not ACK. Wait for TX and GNSS restart, never block IMU/SD. */
        if (!tx_busy && (uint32_t)(now-phase_ms)>=1000U) discover();
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

static void recover_stalled_receiver(uint32_t now)
{
    uint64_t us=Timebase_NowUs(); const GPS_ReceiverStatus *r=&gps_diag.receiver;
    if ((phase!=ACQUIRE && phase!=CONFIG_DONE) || gps_diag.recovery_attempts ||
        !(r->seen&GPS_RX_SKY) || us<r->sky_rx_us || us-r->sky_rx_us>6000000U ||
        r->cno_nonzero || r->code_locked || data.position_valid || data.receiver_fix_valid ||
        (gps_diag.nmea_fix_sentences && us-gps_diag.nmea_last_fix_us<3000000U)) {
        no_signal_since=0; return;
    }
    if (!no_signal_since) no_signal_since=us;
    if (us-no_signal_since<120000000U || tx_busy) return;
    /* Controlled GNSS-only hot restart. Preserve BBR, almanac and ephemerides.
       One attempt per MCU boot, including a failed TX: no endless reset loop. */
    ++gps_diag.recovery_attempts; gps_diag.recovery_us=us;
    const uint8_t hot_start[]={0,0,2,0};
    if (send_ubx(4,hot_start,sizeof(hot_start))!=HAL_OK) return;
    GPS_Epoch_ResetStream(&data,us);
    configuration.state=GPS_CONFIG_PENDING; configuration.pps_state=0;
    phase=RECOVERY_WAIT; phase_ms=now;
}

HAL_StatusTypeDef GPS_Init(UART_HandleTypeDef *uart)
{
    if (uart == NULL) return HAL_ERROR;
    gps_uart = uart; head = tail = 0;
    restart_rx = overflow = tx_busy = 0;
    diagnostic_index=0; memset(&nmea,0,sizeof(nmea));
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
        else { boundary_times[boundary_head]=Timebase_NowUs(); boundary_ends[boundary_head]=head;
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

void GPS_OnTxComplete(UART_HandleTypeDef *uart)
{ if (uart==gps_uart) tx_busy=0; }
void GPS_OnError(UART_HandleTypeDef *uart)
{
    if (uart!=gps_uart || !gps_uart) return;
    uint64_t t=Timebase_NowUs();
    if (!gps_diag.uart_errors) gps_diag.uart_first_us=t;
    gps_diag.uart_last_us=t; ++gps_diag.uart_errors;
    const uint32_t bits[]={HAL_UART_ERROR_FE,HAL_UART_ERROR_NE,HAL_UART_ERROR_ORE,HAL_UART_ERROR_PE};
    uint32_t error=uart->ErrorCode,known=0;
    unsigned phase_index=configuration.state==GPS_CONFIG_READY;
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
    if (gps_uart == NULL) return;
    if (tx_busy && Timebase_NowUs()-tx_started>100000U) {
        (void)HAL_UART_AbortTransmit(gps_uart); tx_busy=0; ++gps_diag.tx_errors;
        if (phase!=CONFIG_DONE && phase!=ACQUIRE) config_error(GPS_CFG_UART,now);
    }
    if (overflow) { overflow=0; ++gps_diag.rx_overflows; restart_rx=1; }
    if (restart_rx) {
        restart_rx = 0;
        (void)HAL_UART_AbortReceive(gps_uart);
        tail = head; ubx_state = nmea.state = 0;
        boundary_tail=boundary_head; irq_ubx_state=0;
        GPS_Epoch_ResetStream(&data,Timebase_NowUs());
        if (HAL_UART_Receive_IT(gps_uart, &rx_byte, 1U) != HAL_OK) restart_rx = 1;
    }
    /* Bounded work even if bytes keep arriving during parsing. */
    for (unsigned n = 0; n < RX_CAPACITY && tail != head; ++n) {
        uint8_t ch = queue[tail]; tail = (tail + 1U) % RX_CAPACITY;
        if (boundary_tail!=boundary_head && boundary_ends[boundary_tail]==tail) {
#ifndef LAPTUNE_HOST_TEST
            __DMB();
#endif
            packet_us=boundary_times[boundary_tail]; boundary_tail=(boundary_tail+1U)%16U;
        }
        if (ubx_state) { ubx_byte(ch,now); continue; }
        if (ch==0xB5) { ubx_state=1; nmea.state=0; }
        else nmea_byte(ch);
    }
    uint64_t current=Timebase_NowUs();
    GPS_Epoch_Service(&data,current); GPS_Time_Service(current);
    LogTime utc;
    if (GPS_Time_UTCAt(current,&utc)) {
        data.year=utc.year; data.month=utc.month; data.day=utc.day;
        data.hour=utc.hour; data.minute=utc.minute; data.second=utc.second; data.millisecond=utc.millisecond;
        data.datetime_ms=(uint32_t)(current/1000U); data.datetime_valid=1;
    }
    data.time_sync_state=GPS_Time_Diagnostics().state;
    if (configuration.acquisition_only && gps_diag.nmea_sentences &&
        current-gps_diag.nmea_last_us<3000000U) {
        data.connected=1; data.sentence_ms=(uint32_t)(gps_diag.nmea_last_us/1000U);
    }
    configure(now);
    recover_stalled_receiver(now);
}

const GPS_Data *GPS_GetData(void) { return &data; }
const GPS_ConfigStatus *GPS_GetConfig(void) { return &configuration; }
const char *GPS_SoftwareVersion(void) { return sw_version; }
const char *GPS_HardwareVersion(void) { return hw_version; }
