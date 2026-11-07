/* The actual ISR/DMA producer, foreground consumer and FatFs writer share this test. */
#define main storage_regression_entrypoint
#include "test_sd_logger.c"
#undef main
#define now mono
#define IMU_FIXTURE_EXTERNAL_CLOCK
#include "imu_fixture.h"
#undef now
static ImuRecord displayed;
static unsigned io_services;
void Logger_ServiceIO(void) { ++io_services; }
void Logger_OnImuRecord(const ImuRecord *r) { displayed=*r; }
static void emit(unsigned n)
{ for (unsigned i=0;i<n;++i) pair(0,0,1); tick=(uint32_t)(mono/1000U); }
static void stall(void) { emit(121); } /* 1.163415 s of independent 104 Hz IRQ/DMA events. */
int main(void)
{
    image=malloc(SECTORS*512U); assert(image); fresh_card(0);
    SDLogger_Start(); assert(SDLogger_Ready());
    assert(Logger_StartAcquisition()); assert(sd_diag.sd_ready_us<=imu_diag.start_us);
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
    ImuStream_Stop();
    /* Boot with no SD: keep the UI hook fresh beyond the complete raw-ring
       capacity, explicitly count unrecorded samples, then accept a card live. */
    fresh_card(0); no_card=1; SDLogger_Start();
    assert(!SDLogger_Ready() && Logger_StartAcquisition()); emit(1);
    for (unsigned n=0;n<4000;++n) {
        emit(1); SDLogger_ConsumeImu(); SDLogger_Process(tick);
        assert(displayed.nr==imu_diag.acquired && !ImuStream_Count());
    }
    assert(imu_diag.acquired==4001 && imu_diag.dropped==1 && sd_log_dropped==4000);
    assert(sd_diag.last_drop_nr==4001 && sd_diag.last_drop_reason==8);
    assert(!sd_log_written && !sd_diag.sd_ready_us);
    no_card=0; on_init=stall; advance(SD_LOG_RETRY_INTERVAL_MS); SDLogger_Process(tick);
    assert(SDLogger_Ready() && !on_init && sd_diag.sd_ready_us>imu_diag.start_us);
    emit(90);
    while (ImuStream_Count() || sd_diag.queue_depth) { SDLogger_ConsumeImu(); SDLogger_Process(tick); advance(10); }
    assert(sd_log_written==211 && sd_diag.first_written_nr==4002 && sd_diag.last_written_nr==4212);
    assert(sd_log_dropped==4000 && imu_diag.dropped==1);
    stop(); free(read_log("LOG0001.TXT",211));
    json=read_file("SESSION.json");
    assert(strstr(json,"\"last_drop_reason\": 8") && strstr(json,"8=storage_unavailable")); free(json);
    /* Card loss after recording also leaves the display and acquisition live. */
    SDLogger_RequestResume(); SDLogger_Process(tick); no_card=1;
    advance(SD_LOG_SNAPSHOT_INTERVAL_MS+1); SDLogger_Process(tick); assert(sd_log_state==LOG_CARD_ERROR);
    for (unsigned n=0;n<400;++n) { emit(1); SDLogger_ConsumeImu(); SDLogger_Process(tick); assert(displayed.nr==imu_diag.acquired); }
    assert(!ImuStream_Count() && sd_log_dropped==4400 && imu_diag.dropped==1);
    assert(!SDLogger_Backlogged()); /* Missing card must not gate live diagnostics. */
    ImuStream_Stop();
    fresh_card(0); SDLogger_Start(); assert(Logger_StartAcquisition()); emit(1);
    /* Run the real foreground scheduler through repeated SD stalls. Each one
       generates 121 IRQ/DMA samples; slow display work is admitted only after
       the backlog has recovered. No artificial renumbering or buffer growth. */
    /* An overdue JSON snapshot waits while raw samples are under pressure. */
    advance(SD_LOG_SNAPSHOT_INTERVAL_MS+1); emit(210);
    char *before_snapshot=read_file("SESSION.json");
    Logger_ServiceStorage(); assert(SDLogger_Backlogged());
    json=read_file("SESSION.json"); assert(!strcmp(json,before_snapshot)); free(json);
    for (unsigned round=0;round<100 && SDLogger_Backlogged();++round) Logger_ServiceStorage();
    assert(!SDLogger_Backlogged()); Logger_ServiceStorage();
    json=read_file("SESSION.json"); assert(strcmp(json,before_snapshot)); free(json); free(before_snapshot);
    advance(800);
    while (ImuStream_Count() || sd_diag.queue_depth) Logger_ServiceStorage();
    unsigned ui_updates=0;
    for (unsigned cycle=0;cycle<20;++cycle) {
        emit(90); on_write=stall;
        unsigned before=io_services;
        Logger_ServiceStorage();
        assert(!on_write && io_services>before && io_services-before<=8);
        for (unsigned round=0;round<100 && SDLogger_Backlogged();++round) {
            before=io_services; Logger_ServiceStorage();
            assert(io_services-before<=8);
        }
        assert(!SDLogger_Backlogged()); ++ui_updates;
        assert(imu_diag.dropped==1 && !sd_log_dropped);
        /* Drain the 700ms history holdback before starting the next stall. */
        advance(800);
        while (ImuStream_Count() || sd_diag.queue_depth) Logger_ServiceStorage();
    }
    assert(ui_updates==20 && sd_log_written==4430 && sd_diag.max_oldest_age_us>1000000);
    stop(); free(read_log("LOG0001.TXT",4430));
    assert(sd_diag.last_written_nr==4431 && sd_diag.max_batch_records>=4);
    ImuStream_Stop(); assert(f_mount(NULL,"",0)==FR_OK); free(image);
    puts("PASS full pipeline: SD-first when available, live IMU without SD and after reinsertion, no GPS/UTC dependency, queue backpressure, 1.163s stall, real DMA buffers, visible overload gaps");
}
