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
static int drop_ack,unsupported_pps;
static uint8_t pulse_configuration[20];
static unsigned transmissions,port_sets,rate_polls;
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u) { assert(u==&uart); return HAL_OK; }
static void feed(const uint8_t *bytes,unsigned length)
{
    if (!powered || !target || uart.Init.BaudRate!=receiver_baud) return;
    for (unsigned i=0;i<length;++i) { *target=bytes[i]; GPS_OnRxComplete(&uart); }
}
static void reply(uint8_t cls,uint8_t id,const uint8_t *payload,unsigned len)
{
    uint8_t frame[160]={0xB5,0x62,cls,id,(uint8_t)len,(uint8_t)(len>>8)};
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
    assert(handle==&uart && len>=8 && p[0]==0xB5 && p[1]==0x62 && (p[2]==6 || p[2]==0x0A));
    GPS_OnTxComplete(handle);
    ++transmissions;
    if (fail_tx) return HAL_TIMEOUT;
    unsigned size=(unsigned)p[4]|((unsigned)p[5]<<8); assert(size+8==len);
    uint8_t a=0,b=0;
    for (unsigned i=2;i<len-2U;++i) { a+=p[i]; b+=a; }
    assert(p[len-2]==a && p[len-1]==b);
    if (!powered || !rx_connected || receiver_baud!=uart.Init.BaudRate) return HAL_OK;
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
    uint8_t pos[28]={0},vel[36]={0},sol[52]={0};
    for (unsigned i=0;i<4;++i) { pos[i]=vel[i]=sol[i]=(uint8_t)((tick%604800000U)>>(8*i)); }
    int32_t altitude=545400;
    for (unsigned i=0;i<4;++i) pos[16+i]=(uint8_t)((uint32_t)altitude>>(8*i));
    sol[10]=3; sol[11]=0x0D; sol[47]=8;
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
    memset(pulse_configuration,0,20); pulse_configuration[12]=47; unsupported_pps=0;
    uart.ErrorCode=0; drop_ack=0; powered=rx_connected=1; corrupt_reply=nak=nak_port=ignore_port=ignore_message=ignore_rate=fail_tx=fail_init=short_reply=0;
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
    for (unsigned i=0;i<6;++i) assert(!message_rates[0][i]);
    assert(message_rates[1][2]==1 && message_rates[1][0x12]==1 && message_rates[1][6]==1 && message_rates[1][0x21]==5);
    assert(!strcmp(GPS_SoftwareVersion(),"7.03 TEST") && !strcmp(GPS_HardwareVersion(),"00040007"));
}
int main(void)
{
    reset(GPS_BOOT_BAUD,0); until(GPS_CONFIG_READY,3000); check_ready(); assert(port_sets==1);
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
    reset(GPS_RUN_BAUD,UINT32_MAX-250U); short_reply=1; until(GPS_CONFIG_READY,3000); check_ready();
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
    reset(GPS_BOOT_BAUD,0); unsupported_pps=1; until(GPS_CONFIG_READY,3000);
    for (unsigned n=0;n<200;++n) step();
    assert(GPS_GetConfig()->state==GPS_CONFIG_READY && GPS_GetConfig()->pps_state==2 && GPS_GetData()->position_valid);
    puts("PASS GPS config: UBX checksums/readback, 5Hz/38400, message rates, MCU/receiver restarts, missing RX, NAK, verification, UART faults and wrap");
}
