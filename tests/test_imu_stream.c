#include "imu_stream.h"
#include "timebase.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
GPIO_TypeDef gpio_a,gpio_b; SPI_HandleTypeDef hspi2;
uint8_t lsm6ds3_registers[16],lsm6ds3_config_verified;
static uint8_t registers[128]={[0x0F]=0x69,[0x10]=0x44,[0x11]=0x4C,[0x12]=0x44,[0x13]=4};
static uint64_t now; static uint8_t *dma_rx; static int fail_dma,corrupt_configuration;
uint64_t Timebase_NowUs(void) { return now; }
uint8_t LSM6DS3_ReadConfiguration(void)
{
    memcpy(lsm6ds3_registers,registers+0x0A,16); lsm6ds3_registers[3]^=corrupt_configuration; return 1;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState s)
{ assert(p==GPIOB && pin==GPIO_PIN_12); (void)s; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p,uint16_t pin)
{ assert(p==GPIOA && (pin==GPIO_PIN_1 || pin==GPIO_PIN_4)); return GPIO_PIN_RESET; }
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *s,const uint8_t *p,uint16_t n,uint32_t t)
{
    assert(s==&hspi2 && p && n<=3 && t==10);
    if (n>1) for (unsigned i=1;i<n;++i) registers[p[0]+i-1]=p[i];
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *s,uint8_t *p,uint16_t n,uint32_t t)
{ assert(s==&hspi2 && n==14 && t==10); memset(p,0,n); return HAL_OK; }
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *s) { assert(s==&hspi2); return HAL_OK; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *s,uint8_t *tx,uint8_t *rx,uint16_t n)
{ assert(s==&hspi2 && tx[0]==0x9E && n==17); dma_rx=rx; return fail_dma ? HAL_ERROR : HAL_OK; }
static void pair(unsigned gap,int clipping,int fresh)
{
    now+=9615; ImuStream_OnDrdy(0); now+=gap; ImuStream_OnDrdy(1);
    memset(dma_rx,0,17); dma_rx[1]=fresh ? 3 : 1;
    dma_rx[5]=0x18; dma_rx[6]=0xFC; /* gyro -1000 */
    dma_rx[11]=0; dma_rx[12]=8; /* accel 2048 */
    if (clipping) { dma_rx[13]=255; dma_rx[14]=127; }
    ImuStream_OnComplete();
}
int main(void)
{
    ImuRecord r; corrupt_configuration=1; assert(!ImuStream_Start()); corrupt_configuration=0;
    assert(ImuStream_Start()); pair(50,0,1); assert(!ImuStream_Pop(&r));
    pair(100,0,1); assert(ImuStream_Pop(&r) && r.nr==2 && r.t_us==now && r.acc_minus_gyro_us==-100);
    assert(r.raw[0]==2048 && r.raw[3]==-1000 && !r.flags);
    LSM6DS3_Sample s; ImuStream_Scale(&r,&s); assert(s.ax>0.999f && s.ax<1 && s.gx==-70);
    pair(501,0,1); assert(ImuStream_Pop(&r) && r.flags==IMU_ERR_PAIR);
    pair(50,1,1); assert(ImuStream_Pop(&r) && r.flags==IMU_ERR_CLIP);
    pair(50,0,0); assert(ImuStream_Pop(&r) && r.flags==IMU_ERR_FRESH);
    fail_dma=1; now+=10000; ImuStream_OnDrdy(0); ImuStream_OnDrdy(1);
    assert(ImuStream_Pop(&r) && (r.flags&IMU_ERR_SPI)); fail_dma=0;
    for (unsigned i=0;i<300;++i) pair(50,0,1);
    unsigned count=0; while (ImuStream_Pop(&r)) ++count;
    assert(count==255 && imu_diag.dropped==46); uint32_t last=r.nr;
    pair(50,0,1); assert(ImuStream_Pop(&r) && r.nr>last+1);
    now+=10000; ImuStream_OnDrdy(0); now+=100; ImuStream_OnDrdy(0); ImuStream_OnDrdy(1);
    memset(dma_rx,0,17); dma_rx[1]=3; ImuStream_OnComplete();
    assert(ImuStream_Pop(&r) && (r.flags&IMU_ERR_FRESH) && imu_diag.missed_drdy==1);
    now+=10000; ImuStream_OnDrdy(0); ImuStream_OnDrdy(1); now+=10001; ImuStream_Service();
    assert(ImuStream_Pop(&r) && (r.flags&IMU_ERR_TIMEOUT));
    assert(ImuStream_Start()); pair(0,0,1); assert(!ImuStream_Pop(&r)); pair(0,0,1);
    assert(ImuStream_Pop(&r) && r.nr>300);
    puts("PASS IMU DMA: actual edge times, signed ADC, pair/fresh/clip/transport errors, overflow gaps, restart continuity");
}
