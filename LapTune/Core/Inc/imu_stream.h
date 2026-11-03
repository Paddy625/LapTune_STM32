#ifndef LAPTUNE_IMU_STREAM_H
#define LAPTUNE_IMU_STREAM_H
#include "acc.h"
#define IMU_STREAM_CAPACITY 256U
#define IMU_PAIR_LIMIT_US 500U
enum { IMU_ERR_SPI=1,IMU_ERR_FRESH=2,IMU_ERR_PAIR=4,IMU_ERR_CLIP=8,IMU_ERR_TIMEOUT=16 };
typedef struct {
    uint64_t t_us;
    uint32_t nr;
    int16_t raw[6]; /* ACC XYZ, GYRO XYZ; original signed ADC. */
    int32_t acc_minus_gyro_us;
    uint16_t flags,reserved;
} ImuRecord;
typedef struct {
    uint32_t acquired,dropped,spi_errors,pair_errors,clipped,missed_drdy;
    uint64_t start_us,last_drop_us;
    uint32_t high_water,last_drop_nr,last_drop_reason;
    uint64_t first_us,last_us;
    uint32_t min_dt_us,max_dt_us,max_pair_us;
} ImuDiagnostics;
extern volatile ImuDiagnostics imu_diag;
uint8_t ImuStream_Start(void);
void ImuStream_Stop(void);
void ImuStream_OnDrdy(uint8_t gyro);
void ImuStream_OnComplete(void);
void ImuStream_OnError(void);
uint8_t ImuStream_Pop(ImuRecord *record);
uint8_t ImuStream_Peek(ImuRecord *record);
uint8_t ImuStream_PeekNext(ImuRecord *record);
uint8_t ImuStream_Latest(ImuRecord *record);
void ImuStream_Consume(void);
uint16_t ImuStream_Count(void);
uint8_t ImuStream_Stalled(uint32_t timeout_us);
ImuDiagnostics ImuStream_Snapshot(void);
void ImuStream_Service(void);
void ImuStream_Scale(const ImuRecord *record,LSM6DS3_Sample *sample);
#ifdef LAPTUNE_HOST_TEST
/* Exercise the same producer/consumer queue and validity accounting on host. */
void ImuStream_TestPush(const ImuRecord *record);
#endif
#endif
