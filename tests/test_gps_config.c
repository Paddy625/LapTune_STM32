#include "gps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef uart;
static uint8_t *target;
static uint32_t tick,receiver_baud,last_nav;
static uint16_t measurement,nav_rate,time_ref;
static uint8_t message_rates[3][256];
uint64_t Timebase_NowUs(void) { return (uint64_t)tick*1000U; }
static int powered,rx_connected,corrupt_reply,nak,nak_port,ignore_port,ignore_message,ignore_rate,fail_tx,fail_init,short_reply;
static int drop_ack,unsupported_pps,unsupported_diagnostics,no_fix_receiver,hold_diagnostic_tx;
static unsigned diagnostic_polls;
static unsigned tx_aborts,recovery_commands;
static int sky_tracking,suppress_sky,nmea_bad_checksum,suppress_nmea;
static uint32_t recovery_command_tick;
static uint8_t pulse_configuration[20];
static unsigned transmissions,port_sets,rate_polls;
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u) { assert(u==&uart); ++tx_aborts; return HAL_OK; }
static void feed(const uint8_t *bytes,unsigned length)
{
    if (!powered || !target || uart.Init.BaudRate!=receiver_baud) return;
    for (unsigned i=0;i<length;++i) { *target=bytes[i]; GPS_OnRxComplete(&uart); }
}
static void reply(uint8_t cls,uint8_t id,const uint8_t *payload,unsigned len)
{
    uint8_t frame[768]={0xB5,0x62,cls,id,(uint8_t)len,(uint8_t)(len>>8)};
    assert(len+8<=sizeof(frame)); if (len) memcpy(frame+6,payload,len);
    uint8_t a=0,b=0;
    for (unsigned i=2;i<6+len;++i) { a+=frame[i]; b+=a; }
    frame[6+len]=a; frame[7+len]=b;
    if (corrupt_reply) { corrupt_reply=0; frame[7+len]^=1; }
    feed(frame,len+8);
}
static void ack(uint8_t id) { const uint8_t p[]={6,id}; if (!drop_ack) reply(5,1,p,2); }
static void sentence(const char *body)
{
    uint8_t sum=0; char line[160];
    for (const char *p=body;*p;++p) sum^=(uint8_t)*p;
    if (nmea_bad_checksum) sum^=1;
    snprintf(line,sizeof(line),"$%s*%02X\r\n",body,sum); feed((const uint8_t*)line,strlen(line));
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *handle,uint8_t *ptr,uint16_t length)
{
    assert(handle==&uart && length==1); target=ptr; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *handle)
{
    assert(handle==&uart); target=NULL; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *handle)
{
    assert(handle==&uart);
    if (fail_init) { --fail_init; return HAL_ERROR; }
    assert(handle->Init.BaudRate==GPS_BOOT_BAUD || handle->Init.BaudRate==GPS_RUN_BAUD);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *handle,const uint8_t *p,uint16_t len)
{
    assert(handle==&uart && len>=8 && p[0]==0xB5 && p[1]==0x62 && (p[2]==6 || p[2]==0x0A || p[2]==1));
    int diagnostic=p[2]==1 || (p[2]==0x0A && p[3]==9) || (p[2]==6 && (p[3]==0x24 || p[3]==0x11));
    if (!hold_diagnostic_tx || !diagnostic) GPS_OnTxComplete(handle);
    ++transmissions;
    if (fail_tx) return HAL_TIMEOUT;
    unsigned size=(unsigned)p[4]|((unsigned)p[5]<<8); assert(size+8==len);
    uint8_t a=0,b=0;
    for (unsigned i=2;i<len-2U;++i) { a+=p[i]; b+=a; }
    assert(p[len-2]==a && p[len-1]==b);
    if (!powered || !rx_connected || receiver_baud!=uart.Init.BaudRate) return HAL_OK;
    if (p[2]==1 || (p[2]==0x0A && p[3]==9) || (p[2]==6 && (p[3]==0x24 || p[3]==0x11))) {
        assert(!size); ++diagnostic_polls;
        if (unsupported_diagnostics) { const uint8_t nak[]={p[2],p[3]}; reply(5,0,nak,2); return HAL_OK; }
        if (p[2]==1) {
            assert(p[3]==0x30); uint8_t sky[8+12*20]={0}; sky[4]=20;
            if (sky_tracking) { sky[8+19*12+3]=5; sky[8+19*12+4]=49; }
            if (!suppress_sky) reply(1,0x30,sky,sizeof(sky));
        } else if (p[2]==0x0A) {
            uint8_t hw[68]={0}; hw[20]=2; hw[21]=1; hw[53]=10; reply(0x0A,9,hw,68);
        } else if (p[3]==0x24) {
            uint8_t nav5[36]={0}; nav5[2]=4; nav5[3]=3; nav5[12]=5; nav5[14]=250; reply(6,0x24,nav5,36);
        } else { const uint8_t rxm[]={8,0}; reply(6,0x11,rxm,2); }
        return HAL_OK;
    }
    if (p[2]==0x0A) {
        uint8_t version[70]={0}; memcpy(version,"7.03 TEST",9); memcpy(version+30,"00040007",8);
        reply(0x0A,4,version,sizeof(version)); return HAL_OK;
    }
    const uint8_t *payload=p+6;
    if (nak && p[3]==1 && size==3) { const uint8_t n[]={6,1}; reply(5,0,n,2); return HAL_OK; }
    switch (p[3]) {
    case 0:
        if (size==1) {
            assert(payload[0]==1);
            uint8_t port[20]={1,0,0,0,0xD0,8,0,0,0,0,0,0,3,0,3,0,0,0,0,0};
            for (unsigned i=0;i<4;++i) port[8+i]=(uint8_t)(receiver_baud>>(i*8));
            reply(6,0,port,sizeof(port));
        } else {
            assert(size==20 && payload[0]==1 && payload[4]==0xD0 && payload[5]==8);
            if (nak_port) { const uint8_t n[]={6,0}; reply(5,0,n,2); break; }
            if (ignore_port) { ack(0); break; }
            receiver_baud=(uint32_t)payload[8]|((uint32_t)payload[9]<<8)|((uint32_t)payload[10]<<16)|((uint32_t)payload[11]<<24);
            assert(receiver_baud==GPS_RUN_BAUD && (payload[12]&3)==3 && (payload[14]&3)==3);
            ++port_sets; ack(0); /* At the new baud: host may legitimately miss this ACK. */
        }
        break;
    case 1:
        assert(payload[0]==0xF0 || payload[0]==1 || payload[0]==0x0D);
        unsigned cls=payload[0]==0xF0 ? 0 : (payload[0]==1 ? 1 : 2);
        if (size==3) { if (!ignore_message) message_rates[cls][payload[1]]=payload[2]; ack(1); }
        else {
            assert(size==2);
            uint8_t response[]={payload[0],payload[1],0,message_rates[cls][payload[1]],0,0,0,0};
            if (short_reply) { response[2]=response[3]; reply(6,1,response,3); }
            else reply(6,1,response,sizeof(response));
        }
        break;
    case 4:
        /* Controlled GNSS-only hot start: never clear backup data. No ACK. */
        assert(size==4 && payload[0]==0 && payload[1]==0 && payload[2]==2 && payload[3]==0);
        ++recovery_commands; recovery_command_tick=tick;
        break;
    case 7:
        if (unsupported_pps) { const uint8_t n[]={6,7}; reply(5,0,n,2); break; }
        if (!size) reply(6,7,pulse_configuration,20);
        else { assert(size==20); memcpy(pulse_configuration,payload,20); ack(7); }
        break;
    case 8:
        if (size==6) {
            assert(payload[0]==200 && payload[1]==0 && payload[2]==1 && payload[3]==0 && payload[4]==1 && payload[5]==0);
            if (!ignore_rate) { measurement=200; nav_rate=1; time_ref=1; } ack(8);
        } else {
            assert(!size); ++rate_polls;
            uint8_t response[]={(uint8_t)measurement,(uint8_t)(measurement>>8),(uint8_t)nav_rate,0,(uint8_t)time_ref,0};
            reply(6,8,response,sizeof(response));
        }
        break;
    default: assert(!"unexpected UBX configuration message");
    }
    return HAL_OK;
}
static void nav(void)
{
    if (!powered || (uint32_t)(tick-last_nav)<measurement) return;
    last_nav=tick;
    if (!suppress_nmea) {
        if (message_rates[0][0]) sentence(no_fix_receiver ?
            "GPGGA,123519,,,,,0,00,99.99,,,,,," :
            "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
        if (message_rates[0][4]) sentence(no_fix_receiver ?
            "GPRMC,123519,V,,,,,,,041126,,,N" :
            "GPRMC,123519,A,4807.038,N,01131.000,E,22.4,84.4,041126,,,A");
    }
    uint8_t pos[28]={0},vel[36]={0},sol[52]={0};
    for (unsigned i=0;i<4;++i) { pos[i]=vel[i]=sol[i]=(uint8_t)((tick%604800000U)>>(8*i)); }
    int32_t altitude=545400;
    for (unsigned i=0;i<4;++i) pos[16+i]=(uint8_t)((uint32_t)altitude>>(8*i));
    sol[10]=3; sol[11]=0x0D; sol[47]=8;
    if (no_fix_receiver) sol[10]=sol[11]=sol[47]=0;
    if (message_rates[1][2]) reply(1,2,pos,sizeof(pos));
    if (message_rates[1][0x12]) reply(1,0x12,vel,sizeof(vel));
    if (message_rates[1][6]) reply(1,6,sol,sizeof(sol));
}
static void step(void)
{
    nav(); unsigned before=transmissions; GPS_Process(tick);
    assert(transmissions-before<=1); tick+=5;
}
static void reset(uint32_t baud,uint32_t start)
{
    tick=start; receiver_baud=baud; last_nav=tick; measurement=1000; nav_rate=time_ref=1;
    memset(message_rates,0,sizeof(message_rates)); for (unsigned i=0;i<=5;++i) message_rates[0][i]=1;
    memset(pulse_configuration,0,20); pulse_configuration[12]=47;
    unsupported_pps=unsupported_diagnostics=no_fix_receiver=hold_diagnostic_tx=0; diagnostic_polls=tx_aborts=0;
    uart.ErrorCode=0; drop_ack=0; powered=rx_connected=1; corrupt_reply=nak=nak_port=ignore_port=ignore_message=ignore_rate=fail_tx=fail_init=short_reply=0;
    sky_tracking=1; suppress_sky=nmea_bad_checksum=suppress_nmea=0; recovery_commands=recovery_command_tick=0;
    transmissions=port_sets=rate_polls=0; target=NULL; uart.Init.BaudRate=GPS_BOOT_BAUD;
    assert(GPS_Init(&uart)==HAL_OK);
}
static void until(GPS_ConfigState state,unsigned timeout)
{
    uint32_t start=tick;
    do { step(); if (GPS_GetConfig()->state==state) return; } while ((uint32_t)(tick-start)<timeout);
    assert(!"GPS configuration did not reach expected state");
}
static void check_ready(void)
{
    const GPS_ConfigStatus *c=GPS_GetConfig();
    assert(c->state==GPS_CONFIG_READY && c->error==GPS_CFG_OK && c->baud==GPS_RUN_BAUD);
    assert(c->measurement_ms==200 && c->nav_rate==1 && c->time_ref==1 && rate_polls);
    for (unsigned i=0;i<6;++i) assert(message_rates[0][i]==(i==0 || i==4));
    assert(!strcmp(GPS_SoftwareVersion(),"7.03 TEST") && !strcmp(GPS_HardwareVersion(),"00040007"));
}
static void streaming(void)
{
    uint32_t start=tick;
    while (GPS_GetConfig()->acquisition_only || !GPS_GetConfig()->pps_state) {
        step(); assert((uint32_t)(tick-start)<4000U);
    }
    check_ready();
    assert(message_rates[1][2]==1 && message_rates[1][0x12]==1 &&
           message_rates[1][6]==1 && message_rates[1][0x21]==5);
}
static void run_ms(uint32_t duration)
{
    uint32_t start=tick;
    while ((uint32_t)(tick-start)<duration) step();
}
int main(void)
{
    reset(GPS_BOOT_BAUD,0); no_fix_receiver=1;
    until(GPS_CONFIG_READY,3000); check_ready(); assert(port_sets==1);
    assert(GPS_GetConfig()->acquisition_only && !GPS_GetConfig()->pps_state);
    assert(!message_rates[1][2] && !message_rates[1][0x12] && !message_rates[1][6] && !message_rates[1][0x21]);
    /* Before UBX starts, GGA is the only source of the live satellite count. */
    suppress_nmea=1;
    sentence("GPGGA,123519,,,,,0,04,99.99,,M,,M,,"); GPS_Process(tick);
    assert(GPS_GetData()->satellites==4 && GPS_GetData()->connected && !GPS_GetData()->receiver_fix_valid);
    assert(!GPS_GetData()->position_valid && !gps_diag.received_epochs);
    nmea_bad_checksum=1;
    sentence("GPGGA,123519,,,,,0,99,99.99,,M,,M,,"); GPS_Process(tick);
    assert(GPS_GetData()->satellites==4);
    nmea_bad_checksum=0;
    sentence("GPGGA,123519,,,,,0,4x,99.99,,M,,M,,"); GPS_Process(tick);
    assert(!GPS_GetData()->satellites);
    suppress_nmea=0;
    no_fix_receiver=0; streaming();
    uint32_t last=GPS_GetData()->position_ms; unsigned updates=0;
    for (unsigned i=0;i<240;++i) {
        step();
        if (GPS_GetData()->position_ms!=last) {
            if (updates) assert((uint32_t)(GPS_GetData()->position_ms-last)==200);
            last=GPS_GetData()->position_ms; ++updates;
        }
    }
    assert(GPS_GetConfig()->pps_state==1 && message_rates[2][1]==1 && pulse_configuration[9]==1 && pulse_configuration[12]==47);
    assert(updates>=5 && GPS_GetData()->altitude_valid && GPS_GetData()->altitude_mm==545400);
    assert(!diagnostic_polls); no_fix_receiver=1;
    for (unsigned i=0;i<1000;++i) step();
    assert(diagnostic_polls>=4 && (gps_diag.receiver.seen&60)==60 && gps_diag.receiver.cno_max==49);
    assert(gps_diag.receiver.dynamic_model==4 && gps_diag.receiver.fix_mode==3 && gps_diag.receiver.pdop_limit==250);
    assert(!GPS_GetData()->position_valid && !gps_diag.receiver.used_sats && !gps_diag.receiver.fix_type);
    no_fix_receiver=0; for (unsigned i=0;i<100;++i) step();
    unsigned polls=diagnostic_polls;
    for (unsigned i=0;i<500;++i) step();
    assert(GPS_GetData()->position_valid && diagnostic_polls==polls);
    uint32_t runtime=tick; uart.ErrorCode=HAL_UART_ERROR_FE|HAL_UART_ERROR_ORE;
    GPS_OnError(&uart); step();
    assert(gps_diag.uart_by_phase[1][0]==1 && gps_diag.uart_by_phase[1][2]==1);
    assert(gps_diag.uart_first_us==(uint64_t)runtime*1000 && gps_diag.uart_last_us==gps_diag.uart_first_us);
    runtime=tick; uart.ErrorCode=HAL_UART_ERROR_NE; GPS_OnError(&uart); step();
    assert(gps_diag.uart_by_phase[1][1]==1 && gps_diag.uart_errors==2 && gps_diag.uart_last_us==(uint64_t)runtime*1000);
    uart.ErrorCode=0;
    /* A second MCU boot with the receiver still running at 38400. */
    uart.Init.BaudRate=GPS_BOOT_BAUD; assert(GPS_Init(&uart)==HAL_OK);
    until(GPS_CONFIG_READY,3000); check_ready(); assert(port_sets==1);
    powered=0; uint32_t start=tick;
    while ((uint32_t)(tick-start)<3500) step();
    assert(GPS_GetConfig()->state!=GPS_CONFIG_READY && !GPS_GetData()->connected);
    powered=1; receiver_baud=GPS_BOOT_BAUD; measurement=1000;
    until(GPS_CONFIG_READY,6000); check_ready();
    reset(GPS_BOOT_BAUD,0); rx_connected=0; until(GPS_CONFIG_ERROR,3000);
    assert(GPS_GetConfig()->error==GPS_CFG_NO_RESPONSE && measurement==1000 && !port_sets);
    rx_connected=1; until(GPS_CONFIG_READY,5000); check_ready();
    reset(GPS_RUN_BAUD,0); nak=1; until(GPS_CONFIG_ERROR,2000);
    assert(GPS_GetConfig()->error==GPS_CFG_NAK); nak=0; until(GPS_CONFIG_READY,4000); check_ready();
    reset(GPS_BOOT_BAUD,0); nak_port=1; until(GPS_CONFIG_ERROR,1000);
    assert(GPS_GetConfig()->error==GPS_CFG_NAK && !port_sets);
    reset(GPS_BOOT_BAUD,0); ignore_port=1; until(GPS_CONFIG_ERROR,5000);
    assert(GPS_GetConfig()->error==GPS_CFG_VERIFY && !port_sets);
    reset(GPS_BOOT_BAUD,0); ignore_message=1; until(GPS_CONFIG_ERROR,3000);
    assert(GPS_GetConfig()->error==GPS_CFG_VERIFY && !rate_polls);
    reset(GPS_BOOT_BAUD,0); drop_ack=1; until(GPS_CONFIG_ERROR,3000);
    assert(GPS_GetConfig()->error==GPS_CFG_VERIFY); drop_ack=0; until(GPS_CONFIG_READY,4000); check_ready();
    reset(GPS_BOOT_BAUD,0); ignore_rate=1; until(GPS_CONFIG_ERROR,3000);
    assert(GPS_GetConfig()->error==GPS_CFG_VERIFY && GPS_GetConfig()->measurement_ms==1000);
    reset(GPS_BOOT_BAUD,0); corrupt_reply=1; until(GPS_CONFIG_READY,4000); check_ready();
    reset(GPS_BOOT_BAUD,0); fail_tx=1; until(GPS_CONFIG_ERROR,100);
    assert(GPS_GetConfig()->error==GPS_CFG_UART); fail_tx=0; until(GPS_CONFIG_READY,4000); check_ready();
    reset(GPS_BOOT_BAUD,0); fail_init=1; until(GPS_CONFIG_ERROR,1000);
    assert(GPS_GetConfig()->error==GPS_CFG_UART); until(GPS_CONFIG_READY,4000); check_ready();
    reset(GPS_RUN_BAUD,UINT32_MAX-250U); short_reply=1; until(GPS_CONFIG_READY,3000); check_ready(); streaming();
    /* Unknown binary payload containing a valid-looking NMEA sentence must be skipped. */
    const char body[]="GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,999.0,M,46.9,M,,";
    char fake[140]; unsigned sum=0; for (const char *p=body;*p;++p) sum^=(unsigned char)*p;
    snprintf(fake,sizeof(fake),"$%s*%02X\r\n",body,sum);
    int32_t old_altitude=GPS_GetData()->altitude_mm;
    reply(1,0x99,(const uint8_t*)fake,strlen(fake)); GPS_Process(tick);
    assert(GPS_GetData()->altitude_mm==old_altitude);
    sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,123.4,M,46.9,M,,");
    GPS_Process(tick); assert(GPS_GetData()->altitude_mm==old_altitude);
    reset(GPS_BOOT_BAUD,0); uart.ErrorCode=HAL_UART_ERROR_FE;
    for (unsigned n=0;n<13;++n) { tick+=5; GPS_OnError(&uart); step(); }
    uart.ErrorCode=0; uint64_t first_error=gps_diag.uart_first_us,last_error=gps_diag.uart_last_us;
    until(GPS_CONFIG_READY,6000); check_ready();
    for (unsigned n=0;n<200;++n) step();
    assert(gps_diag.uart_errors==13 && gps_diag.uart_by_phase[0][0]==13 && !gps_diag.uart_by_phase[1][0]);
    powered=0; uint32_t outage=tick; while ((uint32_t)(tick-outage)<3500U) step();
    powered=1; receiver_baud=GPS_BOOT_BAUD; until(GPS_CONFIG_READY,6000);
    assert(gps_diag.uart_errors==13 && gps_diag.uart_first_us==first_error && gps_diag.uart_last_us==last_error);
    reset(GPS_BOOT_BAUD,0); unsupported_pps=1; until(GPS_CONFIG_READY,3000); streaming();
    for (unsigned n=0;n<200;++n) step();
    assert(GPS_GetConfig()->state==GPS_CONFIG_READY && GPS_GetConfig()->pps_state==2 && GPS_GetData()->position_valid);
    reset(GPS_BOOT_BAUD,0); unsupported_diagnostics=no_fix_receiver=1; until(GPS_CONFIG_READY,3000);
    for (unsigned n=0;n<1200;++n) step();
    assert(diagnostic_polls>=4 && !(gps_diag.receiver.seen&60) && !GPS_GetData()->position_valid);
    assert(GPS_GetConfig()->state==GPS_CONFIG_READY && !GPS_GetConfig()->pps_state && GPS_GetConfig()->acquisition_only);
    unsupported_diagnostics=0; fail_tx=1;
    for (unsigned n=0;n<240;++n) step();
    assert(gps_diag.tx_errors && GPS_GetConfig()->state==GPS_CONFIG_READY && !GPS_GetConfig()->pps_state);
    fail_tx=0; hold_diagnostic_tx=1;
    for (unsigned n=0;n<240;++n) step();
    assert(tx_aborts && GPS_GetConfig()->state==GPS_CONFIG_READY && !GPS_GetConfig()->pps_state);
    /* Invalid/no-checksum fixes must not enable the binary stream or PPS. */
    reset(GPS_BOOT_BAUD,0); no_fix_receiver=1; until(GPS_CONFIG_READY,3000); run_ms(2000);
    assert(GPS_GetConfig()->acquisition_only && !GPS_GetConfig()->pps_state);
    no_fix_receiver=0; nmea_bad_checksum=1; run_ms(2000);
    assert(GPS_GetConfig()->acquisition_only && !message_rates[1][2]);
    /* Embedded NMEA is binary payload, never a receiver fix. */
    reply(1,0x99,(const uint8_t*)fake,strlen(fake)); GPS_Process(tick);
    assert(GPS_GetConfig()->acquisition_only);
    nmea_bad_checksum=0; suppress_nmea=1;
    sentence("GPRMC,,A,");
    GPS_Process(tick); assert(GPS_GetConfig()->acquisition_only);
    sentence("GPGGA,123519,4807.038,N,01131.000,E,0,00,99.99,545.4,M,46.9,M,,");
    GPS_Process(tick); assert(GPS_GetConfig()->acquisition_only);
    /* RMC alone can indicate the first fix; don't require matching GGA. */
    sentence("GPRMC,123519,A,4807.038,N,01131.000,E,22.4,84.4,041126,,,A");
    GPS_Process(tick); assert(!GPS_GetData()->position_valid && GPS_GetData()->receiver_fix_valid);
    streaming(); run_ms(500);
    assert(GPS_GetData()->position_valid);
    /* GGA alone also starts streaming, without waiting for valid UTC/PPS. */
    reset(GPS_BOOT_BAUD,0); no_fix_receiver=1; until(GPS_CONFIG_READY,3000);
    suppress_nmea=1;
    sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    GPS_Process(tick); assert(!GPS_GetData()->position_valid && GPS_GetData()->receiver_fix_valid && GPS_GetData()->satellites==8);
    streaming(); run_ms(500); assert(!GPS_GetData()->position_valid);

    /* No fix while tracking signals is not a wedged receiver. */
    reset(GPS_BOOT_BAUD,0); no_fix_receiver=1; until(GPS_CONFIG_READY,3000); run_ms(130000);
    assert(!recovery_commands && !gps_diag.recovery_attempts && GPS_GetConfig()->acquisition_only);
    /* Missing/stale SVINFO is not evidence of zero signals. */
    sky_tracking=0; run_ms(5000); suppress_sky=1; run_ms(130000);
    assert(!recovery_commands && !gps_diag.recovery_attempts);
    /* A valid receiver without PPS must never be restarted. */
    reset(GPS_BOOT_BAUD,0); unsupported_pps=1; sky_tracking=0;
    until(GPS_CONFIG_READY,3000); streaming(); run_ms(130000);
    assert(GPS_GetData()->position_valid && !recovery_commands && !gps_diag.recovery_attempts);

    /* Persistent fresh zero-signal reports get exactly one hot restart per boot. */
    reset(GPS_BOOT_BAUD,0); no_fix_receiver=1; sky_tracking=0;
    until(GPS_CONFIG_READY,3000); run_ms(115000);
    assert(!recovery_commands && !gps_diag.recovery_attempts);
    uint32_t deadline=tick;
    while (!recovery_commands) { step(); assert((uint32_t)(tick-deadline)<15000U); }
    assert(recovery_commands==1 && gps_diag.recovery_attempts==1);
    assert(gps_diag.recovery_us==(uint64_t)recovery_command_tick*1000U);
    unsigned sent=transmissions; run_ms(900); assert(transmissions==sent);
    run_ms(3000); check_ready(); assert(GPS_GetConfig()->acquisition_only);
    uint32_t epochs=gps_diag.epoch_count,nmea_count=gps_diag.nmea_sentences;
    run_ms(250000); assert(recovery_commands==1 && gps_diag.recovery_attempts==1 && gps_diag.epoch_count>=epochs && gps_diag.nmea_sentences>nmea_count);
    no_fix_receiver=0; streaming(); run_ms(1000);
    assert(GPS_GetData()->position_valid && recovery_commands==1);
    puts("PASS GPS config: legacy NMEA acquisition, UBX/PPS after fix, bounded GNSS hot recovery, checksums/readback, 5Hz/38400, message rates, MCU/receiver restarts, missing RX, NAK, verification, UART faults and wrap");
}
