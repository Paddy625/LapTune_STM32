#include "imu_stream.h"
#include "timebase.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "imu_fixture.h"
int main(void)
{
    ImuRecord r; corrupt_configuration=1; assert(!ImuStream_Start()); corrupt_configuration=0;
    registers[0x10]=0x44; registers[0x11]=0x4C; /* Old +/-16g / +/-2000 dps profile must fail readback. */
    assert(!ImuStream_Start()); registers[0x10]=0x4C; registers[0x11]=0x44;
    assert(ImuStream_Start()); pair(50,0,1); assert(!ImuStream_Pop(&r));
    pair(100,0,1); assert(ImuStream_Pop(&r) && r.nr==2 && r.t_us==now && r.acc_minus_gyro_us==-100);
    assert(r.raw[0]==2048 && r.raw[3]==-1000 && !r.flags);
    LSM6DS3_Sample s; ImuStream_Scale(&r,&s); assert(s.ax>0.4997f && s.ax<0.4998f && s.gx==-17.5f);
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
