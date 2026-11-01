#include "session.h"
#include "boot_identity.h"
#include "sd_logger.h"
#include "gps.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifndef FIRMWARE_COMMIT
#define FIRMWARE_COMMIT "unspecified"
#endif
static FRESULT print(FIL *f,const char *format,...)
{
    char line[256]; va_list args; va_start(args,format);
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
FRESULT Session_Write(const char *dir)
{
    char temp[96],path[96],backup[96]; FIL f;
    (void)snprintf(temp,sizeof(temp),"%s/SESSION.new",dir);
    (void)snprintf(path,sizeof(path),"%s/SESSION.json",dir);
    (void)snprintf(backup,sizeof(backup),"%s/SESSION.bak",dir);
    FRESULT r=f_open(&f,temp,FA_WRITE|FA_CREATE_ALWAYS);
    if (r!=FR_OK) return r;
#define WRITE(...) do { if (r==FR_OK) r=print(&f,__VA_ARGS__); } while (0)
#define STR(k,v) do { if (r==FR_OK) r=string(&f,k,v); } while (0)
    WRITE("{\n  \"format_version\": 2,\n");
    STR("firmware_version",LAPTUNE_FIRMWARE_VERSION); STR("firmware_commit",FIRMWARE_COMMIT);
    STR("boot_id",dir); STR("mcu","STM32F103RB"); STR("imu","LSM6DS3");
    STR("gps_model","NEO-6 declared; identification via MON-VER fields");
    STR("gps_software",GPS_SoftwareVersion()); STR("gps_hardware",GPS_HardwareVersion());
    STR("time_source","TIM2 16-bit + overflow extension, nominal 1 MHz HSI PLL");
    STR("imu_timestamp","later ACC/GYRO DRDY IRQ; no bus/read/write timestamp reconstruction");
    STR("gps_row_time","last required UBX byte or assembly deadline; NOT GNSS measurement time");
    STR("utc","UTC, UART arrival interpolated, auxiliary only; no PPS or holdover guarantee");
    STR("axis_frame","sensor PCB XYZ; no vehicle rotation, gravity removal or startup zeroing");
    STR("invalid_imu","six empty vector fields; IMU_FLAGS: SPI=1,FRESH=2,PAIR=4,CLIP=8,TIMEOUT=16");
    STR("gps_states","N=new valid epoch once, R=immutable repetition, I=no valid position");
    WRITE("  \"time_sync_state\": 0, \"t_imu_unit\": \"us\", \"pps_enabled\": false,\n");
    WRITE("  \"imu_odr_hz\": 104, \"acc_range_g\": 16, \"gyro_range_dps\": 2000,\n");
    WRITE("  \"acc_g_per_lsb\": 0.000488, \"gyro_dps_per_lsb\": 0.070,\n");
    WRITE("  \"who_am_i\": %u,\n",lsm6ds3_registers[5]);
    WRITE("  \"imu_configuration_verified\": %s, \"register_base\": 10, \"registers_0a_to_19\": [",lsm6ds3_config_verified ? "true" : "false");
    for (unsigned i=0;i<16;++i) WRITE("%s%u",i ? "," : "",lsm6ds3_registers[i]);
    WRITE("],\n");
    WRITE("  \"filters\": \"unchanged sensor defaults plus CTRL1/2/3/4 above; group delay NOT bench validated\",\n");
    WRITE("  \"pair_limit_us\": %u, \"timestamp_accuracy_validated\": false,\n",IMU_PAIR_LIMIT_US);
    WRITE("  \"gps_timeout_us\": 800000, \"gps_assembly_deadline_us\": 80000, \"gps_nominal_hz\": 5,\n");
    const GPS_ConfigStatus *c=GPS_GetConfig();
    WRITE("  \"gps_uart_baud\": %lu, \"gps_configuration_state\": %u, \"gps_measurement_ms\": %u,\n",(unsigned long)c->baud,(unsigned)c->state,c->measurement_ms);
    WRITE("  \"gps_min_period_ms\": %lu, \"gps_max_period_ms\": %lu, \"gps_history_misses\": %lu,\n",(unsigned long)gps_diag.min_period_ms,(unsigned long)gps_diag.max_period_ms,(unsigned long)gps_diag.history_misses);
    WRITE("  \"raw_ring_capacity\": 255, \"sd_queue_capacity\": 16, \"rotation_ms\": 180000,\n");
    WRITE("  \"imu_acquired\": %lu, \"imu_ring_dropped\": %lu, \"imu_spi_errors\": %lu,\n",(unsigned long)imu_diag.acquired,(unsigned long)imu_diag.dropped,(unsigned long)imu_diag.spi_errors);
    WRITE("  \"imu_pair_errors\": %lu, \"imu_clipped\": %lu, \"max_pair_us\": %lu,\n",(unsigned long)imu_diag.pair_errors,(unsigned long)imu_diag.clipped,(unsigned long)imu_diag.max_pair_us);
    WRITE("  \"imu_missed_drdy\": %lu,\n",(unsigned long)imu_diag.missed_drdy);
    WRITE("  \"min_dt_us\": %lu, \"max_dt_us\": %lu,\n",(unsigned long)imu_diag.min_dt_us,(unsigned long)imu_diag.max_dt_us);
    WRITE("  \"gps_checksum_errors\": %lu, \"gps_uart_errors\": %lu, \"gps_rx_overflows\": %lu,\n",(unsigned long)gps_diag.checksum_errors,(unsigned long)gps_diag.uart_errors,(unsigned long)gps_diag.rx_overflows);
    WRITE("  \"gps_incomplete_epochs\": %lu, \"gps_late_fragments\": %lu, \"gps_bad_periods\": %lu,\n",(unsigned long)gps_diag.incomplete,(unsigned long)gps_diag.late,(unsigned long)gps_diag.bad_period);
    WRITE("  \"sd_queue_dropped\": %lu, \"sd_written\": %lu, \"sd_max_pause_us\": %lu,\n",(unsigned long)sd_log_dropped,(unsigned long)sd_log_written,(unsigned long)sd_max_pause_us);
    WRITE("  \"antenna_imu_lever_arm_m\": null, \"mounting_calibration\": \"desktop application\"\n}\n");
    if (r==FR_OK) r=f_sync(&f);
    FRESULT close=f_close(&f); if (r==FR_OK) r=close;
    if (r!=FR_OK) return r;
    /* Keep a recoverable previous snapshot, never a second importer-visible TXT. */
    (void)f_unlink(backup);
    FRESULT moved=f_rename(path,backup);
    if (moved!=FR_OK && moved!=FR_NO_FILE) return moved;
    return f_rename(temp,path);
#undef WRITE
#undef STR
}
