#include "imu_stream.h"
#include "timebase.h"
#include <string.h>
#include <limits.h>
_Static_assert(sizeof(ImuRecord)==32,"IMU queue RAM budget");
static ImuRecord queue[IMU_STREAM_CAPACITY],flight;
static volatile uint16_t head,tail;
static volatile uint8_t active,busy,ready,first;
static volatile uint64_t acc_time,gyro_time;
static uint8_t tx[17],rx[17];
static volatile uint64_t started;
static uint16_t pending_flags;
volatile ImuDiagnostics imu_diag;
#ifndef LAPTUNE_HOST_TEST
static uint32_t lock(void) { uint32_t m=__get_PRIMASK(); __disable_irq(); return m; }
static void unlock(uint32_t m) { __set_PRIMASK(m); }
#else
static uint32_t lock(void) { return 0; }
static void unlock(uint32_t m) { (void)m; }
#endif
static void publish(const ImuRecord *record)
{
    uint16_t next=(head+1U)%IMU_STREAM_CAPACITY;
    if (next==tail) { ++imu_diag.dropped; return; }
    queue[head]=*record;
#ifndef LAPTUNE_HOST_TEST
    __DMB();
#endif
    head=next;
}
static void begin(void)
{
    uint32_t m=lock();
    if (!active || busy || ready!=3) { unlock(m); return; }
    busy=1; ready=0; memset(&flight,0,sizeof(flight));
    flight.t_us=acc_time>gyro_time ? acc_time : gyro_time;
    flight.nr=++imu_diag.acquired; flight.flags=pending_flags; pending_flags=0;
    int64_t skew=(int64_t)acc_time-(int64_t)gyro_time;
    flight.acc_minus_gyro_us=skew>INT32_MAX ? INT32_MAX : (skew<INT32_MIN ? INT32_MIN : (int32_t)skew);
    uint64_t difference=skew<0 ? (uint64_t)-skew : (uint64_t)skew;
    if (difference>imu_diag.max_pair_us) imu_diag.max_pair_us=(uint32_t)difference;
    if (difference>IMU_PAIR_LIMIT_US) { flight.flags|=IMU_ERR_PAIR; ++imu_diag.pair_errors; }
    if (!imu_diag.first_us) imu_diag.first_us=flight.t_us;
    if (imu_diag.last_us) {
        uint32_t dt=(uint32_t)(flight.t_us-imu_diag.last_us);
        if (!imu_diag.min_dt_us || dt<imu_diag.min_dt_us) imu_diag.min_dt_us=dt;
        if (dt>imu_diag.max_dt_us) imu_diag.max_dt_us=dt;
    }
    imu_diag.last_us=flight.t_us; started=Timebase_NowUs(); unlock(m);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_RESET);
    if (HAL_SPI_TransmitReceive_DMA(&hspi2,tx,rx,sizeof(tx))!=HAL_OK) ImuStream_OnError();
}
uint8_t ImuStream_Start(void)
{
    /* Outputs are drained before DRDY routing; each subsequent rising edge is new.
       FIFO is deliberately bypassed: independent SPI2 DMA services each pair. */
    ImuStream_Stop();
    uint8_t disable[]={0x0A,0},routes[]={0x0D,1,2},discard[14];
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_RESET);
    HAL_StatusTypeDef r=HAL_SPI_Transmit(&hspi2,disable,2,10);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_SET);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_RESET);
    if (r==HAL_OK) r=HAL_SPI_Transmit(&hspi2,routes,sizeof(routes),10);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_SET);
    uint8_t address=0xA0;
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_RESET);
    if (r==HAL_OK) r=HAL_SPI_Transmit(&hspi2,&address,1,10);
    if (r==HAL_OK) r=HAL_SPI_Receive(&hspi2,discard,sizeof(discard),10);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_SET);
    if (r==HAL_OK && (!LSM6DS3_ReadConfiguration() ||
        lsm6ds3_registers[0]!=0 || lsm6ds3_registers[3]!=1 || lsm6ds3_registers[4]!=2 ||
        lsm6ds3_registers[5]!=0x69 || lsm6ds3_registers[6]!=0x44 || lsm6ds3_registers[7]!=0x4C ||
        lsm6ds3_registers[8]!=0x44 || lsm6ds3_registers[9]!=4)) r=HAL_ERROR;
    lsm6ds3_config_verified=r==HAL_OK;
    memset(tx,0xFF,sizeof(tx)); tx[0]=0x9E;
    if (r!=HAL_OK) { active=0; return 0; }
    ready=busy=0; pending_flags=0; first=1; active=1;
    /* Discard the first pair after arming; an already-high pin has no IRQ time. */
    if (HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_1)==GPIO_PIN_SET) ImuStream_OnDrdy(0);
    if (HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_4)==GPIO_PIN_SET) ImuStream_OnDrdy(1);
    return 1;
}
void ImuStream_Stop(void)
{
    uint32_t m=lock(); active=0; ready=0; uint8_t was_busy=busy; busy=0; unlock(m);
    if (was_busy) (void)HAL_SPI_Abort(&hspi2);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_SET);
}
void ImuStream_OnDrdy(uint8_t gyro)
{
    if (!active) return;
    uint8_t bit=gyro ? 2 : 1;
    if (ready&bit) {
        ++imu_diag.missed_drdy; ++imu_diag.acquired; ++imu_diag.dropped;
        pending_flags|=IMU_ERR_FRESH;
    }
    if (gyro) gyro_time=Timebase_NowUs(); else acc_time=Timebase_NowUs();
    ready|=bit; begin();
}
static int16_t signed16(const uint8_t *p)
{ uint16_t v=p[0]|((uint16_t)p[1]<<8); return v<32768 ? (int16_t)v : (int16_t)((int32_t)v-65536); }
void ImuStream_OnComplete(void)
{
    if (!busy) return;
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_SET);
    if ((rx[1]&3)!=3) flight.flags|=IMU_ERR_FRESH;
    for (unsigned i=0;i<3;++i) { flight.raw[i]=signed16(rx+11+2*i); flight.raw[i+3]=signed16(rx+5+2*i); }
    for (unsigned i=0;i<6;++i) if (flight.raw[i]==INT16_MIN || flight.raw[i]==INT16_MAX) flight.flags|=IMU_ERR_CLIP;
    if (flight.flags&IMU_ERR_CLIP) ++imu_diag.clipped;
    if (first) { first=0; ++imu_diag.dropped; } else publish(&flight);
    busy=0; begin();
}
void ImuStream_OnError(void)
{
    if (!busy) return;
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_SET);
    flight.flags|=IMU_ERR_SPI; ++imu_diag.spi_errors;
    if (first) { first=0; ++imu_diag.dropped; } else publish(&flight);
    busy=0;
}
void ImuStream_Service(void)
{
    uint32_t m=lock();
    if (!busy || Timebase_NowUs()-started<=10000U) { unlock(m); return; }
    /* Claim the failed transfer before enabling interrupts again. Never wait for
       HAL timeouts with PRIMASK set; callbacks see busy=0 while abort runs. */
    ImuRecord failed=flight; uint8_t discard=first;
    active=busy=ready=first=0; unlock(m);
    (void)HAL_SPI_Abort(&hspi2);
    HAL_GPIO_WritePin(LSM6DS3_CS_PORT,LSM6DS3_CS_PIN,GPIO_PIN_SET);
    failed.flags|=IMU_ERR_SPI|IMU_ERR_TIMEOUT; ++imu_diag.spi_errors;
    if (discard) ++imu_diag.dropped; else publish(&failed);
    /* Foreground no-data recovery reinitializes and verifies the sensor. */
}
uint8_t ImuStream_Pop(ImuRecord *r)
{
    if (tail==head) return 0;
#ifndef LAPTUNE_HOST_TEST
    __DMB();
#endif
    *r=queue[tail];
#ifndef LAPTUNE_HOST_TEST
    __DMB();
#endif
    tail=(tail+1U)%IMU_STREAM_CAPACITY; return 1;
}
void ImuStream_Scale(const ImuRecord *r,LSM6DS3_Sample *s)
{
    *s=(LSM6DS3_Sample){r->raw[0]*LSM6DS3_ACCEL_G_LSB,r->raw[1]*LSM6DS3_ACCEL_G_LSB,r->raw[2]*LSM6DS3_ACCEL_G_LSB,
        r->raw[3]*LSM6DS3_GYRO_DPS_LSB,r->raw[4]*LSM6DS3_GYRO_DPS_LSB,r->raw[5]*LSM6DS3_GYRO_DPS_LSB};
}
#ifdef LAPTUNE_HOST_TEST
void ImuStream_TestPush(const ImuRecord *r) { imu_diag.acquired=r->nr; publish(r); }
#endif
