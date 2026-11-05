#include "session.h"
#include "boot_identity.h"
#include "sd_logger.h"
#include "gps.h"
#include "gps_time.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifndef FIRMWARE_COMMIT
#include "build_info.h"
#endif
#ifndef FIRMWARE_DIRTY
#define FIRMWARE_DIRTY 0
#endif
#include "memory_diag.h"
#include "timebase.h"
static FRESULT print(FIL *f,const char *format,...)
{
    char line[224]; va_list args; va_start(args,format);
    int n=vsnprintf(line,sizeof(line),format,args); va_end(args);
    if (n<0 || (unsigned)n>=sizeof(line)) return FR_INT_ERR;
    UINT wrote; FRESULT r=f_write(f,line,(UINT)n,&wrote);
    return r==FR_OK && wrote!=(UINT)n ? FR_DENIED : r;
}
static FRESULT string(FIL *f,const char *key,const char *value)
{
    FRESULT r=print(f,"  \"%s\": \"",key);
    for (const unsigned char *p=(const unsigned char*)value;r==FR_OK && *p;++p) {
        if (*p=='"' || *p=='\\') r=print(f,"\\%c",*p);
        else if (*p<32 || *p>126) r=print(f,"\\u%04X",*p);
        else r=print(f,"%c",*p);
    }
    if (r==FR_OK) r=print(f,"\",\n");
    return r;
}
static FRESULT number64(FIL *f,const char *key,uint64_t n)
{
    char s[21],reverse[20]; unsigned len=0;
    do { reverse[len++]=(char)('0'+n%10U); n/=10U; } while (n);
    for (unsigned i=0;i<len;++i) s[i]=reverse[len-i-1];
    s[len]=0; return print(f,"  \"%s\": %s,\n",key,s);
}
FRESULT Session_Write(const char *dir)
{
    /* Freeze all counters before storage hooks can process more UART packets. */
    ImuDiagnostics imu=ImuStream_Snapshot(); GPS_Diagnostics gps; GPS_DiagnosticsSnapshot(&gps);
    SDLogger_Diagnostics sd=sd_diag; PPS_Diagnostics pps=Timebase_PPSSnapshot();
    GPS_TimeDiagnostics sync=GPS_Time_Diagnostics();
    uint64_t snapshot=Timebase_NowUs(); GPS_ConfigStatus config=*GPS_GetConfig();
    char path[96],backup[96]; FIL f;
    (void)snprintf(path,sizeof(path),"%s/SESSION.new",dir);
    (void)snprintf(backup,sizeof(backup),"%s/SESSION.bak",dir);
    FRESULT r=f_open(&f,path,FA_WRITE|FA_CREATE_ALWAYS);
    if (r!=FR_OK) return r;
#define WRITE(...) do { if (r==FR_OK) r=print(&f,__VA_ARGS__); } while (0)
#define STR(k,v) do { if (r==FR_OK) r=string(&f,k,v); } while (0)
    WRITE("{\n  \"format_version\": 4,\n");
    WRITE("  \"firmware_dirty\": %s,\n",FIRMWARE_DIRTY ? "true" : "false");
    STR("firmware_version",LAPTUNE_FIRMWARE_VERSION); STR("firmware_commit",FIRMWARE_COMMIT);
    STR("boot_id",dir); STR("mcu","STM32F103RB"); STR("imu","LSM6DS3");
    STR("gps_model","NEO-6 declared; identification via MON-VER fields");
    STR("gps_software",GPS_SoftwareVersion()); STR("gps_hardware",GPS_HardwareVersion());
    STR("time_source","TIM2 16-bit + overflow extension, nominal 1 MHz HSI PLL");
    STR("imu_timestamp","later ACC/GYRO DRDY IRQ; no bus/read/write timestamp reconstruction");
    STR("gps_row_time","NMEA GGA/RMC paired by UTC-of-day; assigned by UART RX/publication, not measurement time");
    STR("utc","Approximate UTC extrapolated from RMC reception, as in 1fd016f; no PPS synchronization");
    STR("axis_frame","sensor PCB XYZ; no vehicle rotation, gravity removal or startup zeroing");
    STR("invalid_imu","six empty vector fields; IMU_FLAGS: SPI=1,FRESH=2,PAIR=4,CLIP=8,TIMEOUT=16");
    STR("gps_profile","1fd016f: default 1Hz/9600/NMEA; no UBX NAV or PPS configuration");
    STR("gps_epoch_identity","local sequence for matched NMEA UTC-of-day; GPS_ITOW_MS and GPS_T_MEAS_US empty, TIME_SYNC_STATE=0");
    STR("gps_states","N=new valid epoch once, R=immutable repetition, I=no valid position");
    WRITE("  \"time_sync_state\": %u, \"t_imu_unit\": \"us\", \"pps_enabled\": false,\n",sync.state);
    WRITE("  \"imu_odr_hz\": 104, \"acc_range_g\": 16, \"gyro_range_dps\": 2000,\n");
    WRITE("  \"acc_g_per_lsb\": 0.000488, \"gyro_dps_per_lsb\": 0.070,\n");
    WRITE("  \"who_am_i\": %u,\n",lsm6ds3_registers[5]);
    WRITE("  \"imu_configuration_verified\": %s, \"register_base\": 10, \"registers_0a_to_19\": [",lsm6ds3_config_verified ? "true" : "false");
    for (unsigned i=0;i<16;++i) WRITE("%s%u",i ? "," : "",lsm6ds3_registers[i]);
    WRITE("],\n");
    WRITE("  \"filters\": \"unchanged sensor defaults plus CTRL1/2/3/4 above; group delay NOT bench validated\",\n");
    WRITE("  \"pair_limit_us\": %u, \"timestamp_accuracy_validated\": false,\n",IMU_PAIR_LIMIT_US);
    WRITE("  \"gps_timeout_us\": 3000000, \"gps_assembly_deadline_us\": 800000, \"gps_nominal_hz\": 1,\n");
    const GPS_ConfigStatus *c=&config;
    WRITE("  \"gps_uart_baud\": %lu, \"gps_configuration_state\": %u, \"gps_measurement_ms\": %u,\n",(unsigned long)c->baud,(unsigned)c->state,c->measurement_ms);
    WRITE("  \"gps_acquisition_only\": %u, \"gps_nmea_sentences\": %lu, \"gps_nmea_fix_sentences\": %lu,\n",c->acquisition_only,(unsigned long)gps.nmea_sentences,(unsigned long)gps.nmea_fix_sentences);
    if (r==FR_OK) r=number64(&f,"gps_nmea_last_us",gps.nmea_last_us);
    if (r==FR_OK) r=number64(&f,"gps_nmea_last_fix_us",gps.nmea_last_fix_us);
    WRITE("  \"gps_recovery_attempts\": %lu,\n",(unsigned long)gps.recovery_attempts);
    if (r==FR_OK) r=number64(&f,"gps_recovery_us",gps.recovery_us);
    STR("gps_recovery_reason","2.1.6 factory rollback once; passive NMEA afterwards; NO FIX never triggers a reset");
    WRITE("  \"gps_restore_state\": %u, \"gps_restore_error\": %u, \"gps_restore_ack\": %u,\n",c->restore_state,c->restore_error,c->restore_ack);
    STR("gps_restore_states","0=not requested this boot,1=pending,2=defaults TX,3=cold TX,4=factory 9600/1Hz/NMEA read back,5=failed; 4 is not RF/fix verification");
    WRITE("  \"gps_min_period_ms\": %lu, \"gps_max_period_ms\": %lu, \"gps_history_misses\": %lu,\n",(unsigned long)gps.min_period_ms,(unsigned long)gps.max_period_ms,(unsigned long)gps.history_misses);
    const GPS_ReceiverStatus *rx=&gps.receiver;
    WRITE("  \"gps_receiver_seen\": %u, \"gps_sol_fix_type\": %u, \"gps_sol_flags\": %u,\n",rx->seen,rx->fix_type,rx->sol_flags);
    WRITE("  \"gps_sol_week\": %d, \"gps_sol_used_sats\": %u, \"gps_utc_valid_flags\": %u,\n",rx->week,rx->used_sats,rx->utc_flags);
    WRITE("  \"gps_svinfo_channels\": %u, \"gps_sats_with_cno\": %u, \"gps_sats_code_locked\": %u, \"gps_max_cno_dbhz\": %u,\n",rx->sky_channels,rx->cno_nonzero,rx->code_locked,rx->cno_max);
    WRITE("  \"gps_antenna_status\": %u, \"gps_antenna_power\": %u, \"gps_noise_per_ms\": %u, \"gps_agc_count\": %u,\n",rx->antenna_status,rx->antenna_power,rx->noise_per_ms,rx->agc_count);
    WRITE("  \"gps_jamming_indicator\": %u, \"gps_hw_flags\": %u,\n",rx->jamming,rx->hw_flags);
    WRITE("  \"gps_dynamic_model\": %u, \"gps_fix_mode\": %u, \"gps_power_mode\": %u,\n",rx->dynamic_model,rx->fix_mode,rx->power_mode);
    WRITE("  \"gps_min_elevation_deg\": %d, \"gps_pdop_limit_x10\": %u,\n",rx->min_elevation,rx->pdop_limit);
    STR("gps_receiver_seen_bits","SOL=1,UTC=2,SVINFO=4,MON_HW=8,NAV5=16,RXM=32; fields without bit are unknown");
    WRITE("  \"raw_ring_capacity\": 255, \"sd_queue_capacity\": 16, \"rotation_ms\": 180000,\n");
    WRITE("  \"imu_acquired\": %lu, \"imu_ring_dropped\": %lu, \"imu_spi_errors\": %lu,\n",(unsigned long)imu.acquired,(unsigned long)imu.dropped,(unsigned long)imu.spi_errors);
    WRITE("  \"imu_pair_errors\": %lu, \"imu_clipped\": %lu, \"max_pair_us\": %lu,\n",(unsigned long)imu.pair_errors,(unsigned long)imu.clipped,(unsigned long)imu.max_pair_us);
    WRITE("  \"imu_missed_drdy\": %lu,\n",(unsigned long)imu.missed_drdy);
    WRITE("  \"min_dt_us\": %lu, \"max_dt_us\": %lu,\n",(unsigned long)imu.min_dt_us,(unsigned long)imu.max_dt_us);
    WRITE("  \"gps_checksum_errors\": %lu, \"gps_uart_errors\": %lu, \"gps_rx_overflows\": %lu,\n",(unsigned long)gps.checksum_errors,(unsigned long)gps.uart_errors,(unsigned long)gps.rx_overflows);
    WRITE("  \"gps_incomplete_epochs\": %lu, \"gps_late_fragments\": %lu, \"gps_bad_periods\": %lu,\n",(unsigned long)gps.incomplete,(unsigned long)gps.late,(unsigned long)gps.bad_period);
    WRITE("  \"sd_queue_dropped\": %lu, \"sd_written\": %lu, \"sd_max_pause_us\": %lu,\n",(unsigned long)sd_log_dropped,(unsigned long)sd_log_written,(unsigned long)sd_max_pause_us);
#define U64(k,v) do { if (r==FR_OK) r=number64(&f,k,v); } while (0)
    U64("gps_svinfo_rx_us",rx->sky_rx_us); U64("gps_mon_hw_rx_us",rx->hw_rx_us);
    U64("snapshot_t_us",snapshot); U64("sd_ready_us",sd.sd_ready_us); U64("imu_start_us",imu.start_us);
    U64("first_written_us",sd.first_written_us); U64("first_written_sample_us",sd.first_written_sample_us);
    WRITE("  \"first_written_nr\": %lu, \"last_acquired_nr\": %lu, \"last_written_nr\": %lu,\n",(unsigned long)sd.first_written_nr,(unsigned long)imu.acquired,(unsigned long)sd.last_written_nr);
    U64("last_synced_us",sd.last_synced_us); U64("last_synced_sample_us",sd.last_synced_sample_us);
    WRITE("  \"last_synced_nr\": %lu, \"sd_synced_records\": %lu, \"clean_shutdown\": %s,\n",(unsigned long)sd.last_synced_nr,(unsigned long)sd.synced_records,sd.clean_shutdown ? "true" : "false");
    uint8_t raw_later=imu.last_drop_us>=sd.last_drop_us;
    WRITE("  \"last_drop_nr\": %lu, \"last_drop_reason\": %lu,\n",(unsigned long)(raw_later ? imu.last_drop_nr : sd.last_drop_nr),(unsigned long)(raw_later ? imu.last_drop_reason : sd.last_drop_reason));
    U64("last_drop_us",raw_later ? imu.last_drop_us : sd.last_drop_us);
    STR("drop_reasons","0=none,1=arming_discard,2=raw_ring_full,3=missed_DRDY,5=explicit_pause,7=uncertain_write_batch,8=storage_unavailable");
    STR("sd_written_semantics","records accepted by f_write; sd_synced_records counts successful data f_sync/f_close; no power-fail guarantee");
    WRITE("  \"sd_mount_attempts\": %lu, \"sd_failures\": %lu, \"sd_first_error_code\": %lu, \"sd_last_error_code\": %lu,\n",(unsigned long)sd.mount_attempts,(unsigned long)sd.init_failures,(unsigned long)sd.first_error_code,(unsigned long)sd.last_error_code);
    U64("sd_first_error_us",sd.first_error_us); U64("sd_last_error_us",sd.last_error_us);
    STR("queue_capacity_unit","individual records, not batches or bytes");
    WRITE("  \"raw_record_bytes\": %u, \"sd_record_bytes\": %u, \"raw_ring_high_water\": %lu, \"sd_queue_high_water\": %lu, \"sd_queue_depth\": %lu,\n",(unsigned)sizeof(ImuRecord),SDLogger_RecordBytes(),(unsigned long)imu.high_water,(unsigned long)sd.queue_high_water,(unsigned long)sd.queue_depth);
    WRITE("  \"static_ram_bytes\": %lu, \"stack_reserved_bytes\": %lu, \"stack_free_min_bytes\": %lu, \"heap_used_bytes\": %lu,\n",(unsigned long)MemoryDiag_StaticBytes(),(unsigned long)MemoryDiag_StackReserved(),(unsigned long)MemoryDiag_StackFreeMin(),(unsigned long)MemoryDiag_HeapUsed());
    WRITE("  \"timer_pclk1_hz\": %lu, \"timer_input_hz\": %lu, \"timer_prescaler\": %lu, \"timer_tick_hz\": %lu, \"timer_arr\": %lu,\n",(unsigned long)timebase_config.pclk1_hz,(unsigned long)timebase_config.timer_hz,(unsigned long)timebase_config.prescaler,(unsigned long)timebase_config.tick_hz,(unsigned long)timebase_config.reload);
    WRITE("  \"pps_capture_enabled\": false, \"pps_reference_validated\": false, \"pps_pulses\": %lu, \"pps_periods\": %lu,\n",(unsigned long)pps.pulses,(unsigned long)pps.periods);
    U64("pps_last_capture_us",pps.last_capture_us);
    WRITE("  \"pps_last_period_ticks\": %lu, \"pps_min_period_ticks\": %lu, \"pps_max_period_ticks\": %lu,\n",(unsigned long)pps.last_period_us,(unsigned long)pps.min_period_us,(unsigned long)pps.max_period_us);
    WRITE("  \"pps_mean_period_ticks\": %lu, \"pps_capture_overruns\": %lu, \"pps_rejected_periods\": %lu,\n",(unsigned long)(pps.periods ? pps.sum_period_us/pps.periods : 0),(unsigned long)pps.overruns,(unsigned long)pps.rejected_periods);
    STR("pps_semantics","disabled for GPS rollback; PA0 can remain wired; IMU timer continues");
    WRITE("  \"pps_configuration_state\": %u, \"pps_configuration_error\": %u,\n",config.pps_state,config.pps_error);
    WRITE("  \"pps_timtp_packets\": %lu, \"pps_matched\": %lu, \"pps_rejected\": %lu,\n",(unsigned long)sync.timtp_packets,(unsigned long)sync.matched,(unsigned long)sync.rejected);
    WRITE("  \"pps_locks\": %lu, \"pps_losses\": %lu, \"pps_period_change_ticks\": %lu,\n",(unsigned long)sync.locks,(unsigned long)sync.losses,(unsigned long)sync.period_change_ticks);
    WRITE("  \"pps_model_period_ticks\": %lu,\n",(unsigned long)sync.period_ticks);
    WRITE("  \"log_holdback_us\": %u, \"pps_nav_latency_limit_us\": 500000,\n",SD_LOG_HOLDBACK_US);
    U64("gps_uart_first_error_us",gps.uart_first_us); U64("gps_uart_last_error_us",gps.uart_last_us);
    U64("gps_uart_init_first_error_us",gps.uart_phase_first_us[0]); U64("gps_uart_init_last_error_us",gps.uart_phase_last_us[0]);
    U64("gps_uart_run_first_error_us",gps.uart_phase_first_us[1]); U64("gps_uart_run_last_error_us",gps.uart_phase_last_us[1]);
    U64("gps_checksum_first_error_us",gps.checksum_first_us); U64("gps_checksum_last_error_us",gps.checksum_last_us);
    const char *phases[]={"init","run"},*errors[]={"framing","noise","overrun","parity","other"};
    for (unsigned phase=0;phase<2;++phase) for (unsigned error=0;error<5;++error)
        WRITE("  \"gps_uart_%s_%s\": %lu,\n",phases[phase],errors[error],(unsigned long)gps.uart_by_phase[phase][error]);
    WRITE("  \"gps_checksum_init\": %lu, \"gps_checksum_run\": %lu, \"gps_tx_errors\": %lu,\n",(unsigned long)gps.checksum_by_phase[0],(unsigned long)gps.checksum_by_phase[1],(unsigned long)gps.tx_errors);
    WRITE("  \"gps_received_epochs\": %lu, \"gps_published_epochs\": %lu, \"gps_valid_epochs\": %lu, \"gps_invalid_fixes\": %lu,\n",(unsigned long)gps.received_epochs,(unsigned long)gps.epoch_count,(unsigned long)gps.valid_epochs,(unsigned long)gps.invalid_fixes);
    STR("gps_configuration_states","0=factory restore pending,1=defaults readback OK,2=restore error,3=passive NMEA without readback this boot; no state implies fix");
#undef U64
    WRITE("  \"antenna_imu_lever_arm_m\": null, \"mounting_calibration\": \"desktop application\"\n}\n");
    if (r==FR_OK) r=f_sync(&f);
    FRESULT close=f_close(&f); if (r==FR_OK) r=close;
    if (r!=FR_OK) return r;
    /* Keep a recoverable previous snapshot, never a second importer-visible TXT. */
    (void)f_unlink(backup);
    (void)snprintf(path,sizeof(path),"%s/SESSION.json",dir);
    FRESULT moved=f_rename(path,backup);
    if (moved!=FR_OK && moved!=FR_NO_FILE) return moved;
    (void)snprintf(backup,sizeof(backup),"%s/SESSION.new",dir);
    return f_rename(backup,path);
#undef WRITE
#undef STR
}
