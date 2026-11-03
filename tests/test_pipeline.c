/* The actual ISR/DMA producer, foreground consumer and FatFs writer share this test. */
#define main storage_regression_entrypoint
#include "test_sd_logger.c"
#undef main
#define now mono
#define IMU_FIXTURE_EXTERNAL_CLOCK
#include "imu_fixture.h"
#undef now
static void emit(unsigned n)
{ for (unsigned i=0;i<n;++i) pair(0,0,1); tick=(uint32_t)(mono/1000U); }
static void stall(void) { emit(121); } /* 1.163415 s of independent 104 Hz IRQ/DMA events. */
int main(void)
{
    image=malloc(SECTORS*512U); assert(image); fresh_card(0); no_card=1;
    SDLogger_Start(); assert(!SDLogger_StartAcquisition(0) && !imu_diag.acquired);
    advance(37000); SDLogger_ConsumeImu(); assert(!imu_diag.acquired && !sd_log_dropped);
    no_card=0; SDLogger_Process(tick); assert(SDLogger_Ready());
    assert(SDLogger_StartAcquisition(0)); assert(sd_diag.sd_ready_us<=imu_diag.start_us);
    emit(1); assert(!ImuStream_Count() && imu_diag.dropped==1); emit(73);
    SDLogger_ConsumeImu(); assert(!sd_diag.queue_depth && ImuStream_Count()==73);
    emit(17);
    SDLogger_ConsumeImu(); assert(sd_diag.queue_depth==16 && ImuStream_Count()==74);
    SDLogger_ConsumeImu(); assert(ImuStream_Count()==74 && !sd_log_dropped);
    on_write=stall; SDLogger_Process(tick); assert(!on_write);
    assert(ImuStream_Count()>=195 && imu_diag.dropped==1 && !ImuStream_Stalled(1000000U));
    while (ImuStream_Count() || sd_diag.queue_depth) { SDLogger_ConsumeImu(); SDLogger_Process(tick); advance(10); }
    assert(sd_log_written==211 && !sd_log_dropped && sd_diag.first_written_nr==2);
    assert(sd_diag.first_written_us>=imu_diag.start_us && imu_diag.high_water>=195);
    stop(); assert(sd_diag.clean_shutdown && sd_diag.last_synced_nr==212);
    char *text=read_log("LOG0001.TXT",211); free(text);
    SDLogger_RequestResume(); SDLogger_Process(tick); emit(300);
    assert(ImuStream_Count()==255 && imu_diag.last_drop_reason==2 && imu_diag.dropped==46);
    while (ImuStream_Count() || sd_diag.queue_depth) { SDLogger_ConsumeImu(); SDLogger_Process(tick); advance(10); }
    emit(1); SDLogger_ConsumeImu(); stop(); assert(sd_diag.last_written_nr==513 && !sd_log_dropped);
    char *json=read_file("SESSION.json");
    assert(strstr(json,"\"last_drop_nr\": 512") && strstr(json,"\"last_drop_reason\": 2"));
    assert(strstr(json,"\"last_acquired_nr\": 513") && strstr(json,"\"last_written_nr\": 513")); free(json);
    ImuStream_Stop(); assert(f_mount(NULL,"",0)==FR_OK); free(image);
    puts("PASS full pipeline: SD-first startup, no GPS/UTC dependency, queue backpressure, 1.163s stall, real DMA buffers, visible overload gaps");
}
