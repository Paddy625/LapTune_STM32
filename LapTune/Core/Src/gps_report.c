#include "gps_report.h"
#include "gps.h"
#include "sd_logger.h"
#include "boot_identity.h"
#include "../FatFs/ff.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static FRESULT output(FIL *f,const char *format,...)
{
    char text[112]; va_list args; va_start(args,format);
    int n=vsnprintf(text,sizeof(text),format,args); va_end(args);
    if (n<0 || (unsigned)n>=sizeof(text)) return FR_INT_ERR;
    UINT wrote=0; FRESULT r=f_write(f,text,(UINT)n,&wrote);
    return r==FR_OK && wrote!=(UINT)n ? FR_DENIED : r;
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p)|((uint32_t)u16(p+2)<<16); }
static FRESULT decode(FIL *f,const GPS_QueryResult *r)
{
    const uint8_t *p=r->payload;
    if (r->status!=GPS_QUERY_OK) return output(f,"decoded=UNKNOWN (see status and raw response)\r\n");
    if (r->rx_class==10 && r->rx_id==4) {
        char text[41];
        for (unsigned i=0;i<40;++i) text[i]=p[i]>=32 && p[i]<=126 ? (char)p[i] : ' ';
        text[40]=0; return output(f,"software[30],hardware[10]=%s\r\n",text);
    }
    if (r->rx_class==6) switch (r->rx_id) {
    case 0:
        if (p[0]!=1 && p[0]!=2) return output(f,"port=%u in=0x%04X out=0x%04X; port-specific fields in raw frame\r\n",p[0],u16(p+12),u16(p+14));
        return output(f,"port=%u baud=%lu mode=0x%08lX in=0x%04X out=0x%04X\r\n",
        p[0],(unsigned long)u32(p+8),(unsigned long)u32(p+4),u16(p+12),u16(p+14));
    case 8: return output(f,"measurement_ms=%u nav_cycles=%u time_ref=%u (0=UTC,1=GPS)\r\n",u16(p),u16(p+2),u16(p+4));
    case 1: return output(f,"message=%02X/%02X uart1_rate=%u response_bytes=%u\r\n",p[0],p[1],p[r->length==8 ? 3 : 2],r->length);
    case 0x23: return output(f,"version=%u minSV=%u maxSV=%u minCNO=%u initial3D=%u week_rollover=%u PPP=%u AOP=%u\r\n",
        u16(p),p[10],p[11],p[12],p[14],u16(p+18),p[26],p[27]);
    case 0x17: return output(f,"filter=0x%02X NMEA_version=0x%02X numSV=%u flags=0x%02X\r\n",p[0],p[1],p[2],p[3]);
    case 0x16: return output(f,"SBAS_mode=0x%02X usage=0x%02X maxSBAS=%u scanmode2=0x%02X scanmode1=0x%08lX\r\n",p[0],p[1],p[2],p[3],(unsigned long)u32(p+4));
    case 0x39: return output(f,"ITFM_config=0x%08lX config2=0x%08lX\r\n",(unsigned long)u32(p),(unsigned long)u32(p+4));
    case 0x0E: return output(f,"FXN_flags=0x%08lX reacquire_ms=%lu acquire_ms=%lu\r\n",(unsigned long)u32(p),(unsigned long)u32(p+4),(unsigned long)u32(p+8));
    case 0x31: return output(f,"pulse_index=%u freqPeriod=%lu freqPeriodLock=%lu flags=0x%08lX\r\n",p[0],(unsigned long)u32(p+8),(unsigned long)u32(p+12),(unsigned long)u32(p+28));
    case 0x24: return output(f,"dynamic_model=%u fix_mode=%u min_elev_deg=%d pDOP_x10=%u tDOP_x10=%u\r\n",
        p[2],p[3],(int)(int8_t)p[12],u16(p+14),u16(p+16));
    case 0x11: return output(f,"low_power_mode=%u (0=continuous,1=power-save,4=eco if supported)\r\n",p[1]);
    case 7: return output(f,"interval_us=%lu pulse_us=%lu status=%d time_ref=%u flags=0x%02X\r\n",
        (unsigned long)u32(p),(unsigned long)u32(p+4),(int)(int8_t)p[8],p[9],p[10]);
    case 0x13: return output(f,"antenna_flags=0x%04X antenna_pin_config=0x%04X\r\n",u16(p),u16(p+2));
    }
    if (r->rx_class==1 && r->rx_id==3) return output(f,
        "iTOW_ms=%lu fix_type=%u flags=0x%02X fix_status=0x%02X ttff_ms=%lu uptime_ms=%lu\r\n",
        (unsigned long)u32(p),p[4],p[5],p[6],(unsigned long)u32(p+8),(unsigned long)u32(p+12));
    if (r->rx_class==1 && r->rx_id==0x30) {
        FRESULT error=output(f,"iTOW_ms=%lu channels=%u\r\n",(unsigned long)u32(p),p[4]);
        for (unsigned i=0;i<p[4] && error==FR_OK;++i) {
            const uint8_t *s=p+8+12*i;
            error=output(f,"channel=%u svid=%u flags=0x%02X quality=%u CNO_dBHz=%u elevation_deg=%d\r\n",
                s[0],s[1],s[2],s[3],s[4],(int)(int8_t)s[5]);
        }
        return error;
    }
    if (r->rx_class==10 && r->rx_id==9) return output(f,
        "noise=%u agc=%u antenna_status=%u antenna_power=%u flags=0x%02X jam=%u\r\n",
        u16(p+16),u16(p+18),p[20],p[21],p[22],p[r->length>=68 ? 53 : 45]);
    return output(f,"decoded=see complete raw payload (no short text decoder for this message)\r\n");
}
static FRESULT report(FIL *f,const GPS_QueryResult *r)
{
    static const char *const names[]={"OK","TIMEOUT","NAK","MALFORMED","TRUNCATED","TX_ERROR","SKIPPED","VERIFY_FAILED"};
    uint8_t received=r->rx_present;
    FRESULT error=output(f,"BEGIN %u/%u t_ms=%lu query=%s attempts=%u status=%s\r\n",
        r->index,r->total,(unsigned long)r->t_ms,r->name,r->attempts,names[r->status]);
    static const char *const mask_names[]={"NO_BASELINE","READY","ALREADY_0003","UNEXPECTED_BASELINE",
        "SET_SENT_UNVERIFIED","ACKED_UNVERIFIED","SET_REJECTED","VERIFIED","VERIFY_FAILED"};
    if (error==FR_OK) error=output(f,"cycle=%u mask_change=%s\r\n",r->cycle,mask_names[r->mask_state]);
    if (error==FR_OK && r->tx_length) {
        error=output(f,"tx=");
        for (unsigned i=0;i<r->tx_length && error==FR_OK;++i) error=output(f,"%02X ",r->tx_frame[i]);
        if (error==FR_OK) error=output(f,"\r\n");
    }
    if (error==FR_OK && received) error=output(f,"rx=%02X/%02X payload_length=%u stored=%u\r\nraw=B5 62 %02X %02X %02X %02X ",
        r->rx_class,r->rx_id,r->length,r->stored,r->rx_class,r->rx_id,r->length&255,r->length>>8);
    for (unsigned i=0;i<r->stored && error==FR_OK;i+=24) {
        char hex[73]; unsigned used=0;
        for (unsigned j=i;j<r->stored && j<i+24;++j) {
            static const char digits[]="0123456789ABCDEF";
            hex[used++]=digits[r->payload[j]>>4]; hex[used++]=digits[r->payload[j]&15]; hex[used++]=' ';
        }
        hex[used]=0; error=output(f,"%s",hex);
    }
    if (error==FR_OK && received) {
        if (r->stored<r->length) error=output(f,"[PAYLOAD TRUNCATED] ");
        if (error==FR_OK) error=output(f,"%02X %02X\r\n",r->ck_a,r->ck_b);
    }
    if (error==FR_OK) error=decode(f,r);
    if (error==FR_OK && r->index==GPS_ConfigQueryCount()) error=output(f,"CONFIG_READ_COMPLETE; inspect statuses; 20 receiver-status rounds follow.\r\n");
    if (error==FR_OK) error=output(f,"END %u\r\n%s",r->index,r->index==r->total ? "COMPLETE: all polls attempted; inspect each status, TIMEOUT is not a zero setting.\r\n" : "");
    return error;
}
void GPSReport_Process(uint32_t now)
{
    static uint32_t last_failure; static uint8_t retry;
    const GPS_QueryResult *r=GPS_QueryPending();
    if (!r || !SDLogger_Ready() || ImuStream_Count()>100U || (retry && (uint32_t)(now-last_failure)<2000U)) return;
    /* The UART parser freezes r and its payload until this file is closed.
       SD service hooks can drain GPS/IMU, but never re-enter FatFs here. */
    char path[96]; FIL f;
    (void)snprintf(path,sizeof(path),"%s/GPS_CFG.TXT",sd_log_session_dir);
    FRESULT error=f_open(&f,path,FA_WRITE|FA_OPEN_ALWAYS);
    if (error==FR_OK) {
        error=f_lseek(&f,f_size(&f));
        if (error==FR_OK && !f_size(&f)) error=output(&f,"LAPTUNE %s GPS CONFIG REPORT; UART1 out mask only; no reset/restore/save.\r\n",LAPTUNE_FIRMWARE_VERSION);
        if (error==FR_OK) error=report(&f,r);
        FRESULT close=f_close(&f); if (error==FR_OK) error=close; /* FatFs close synchronizes. */
    }
    if (error==FR_OK) { retry=0; GPS_QuerySaved(); }
    else { retry=1; last_failure=now; } /* Retry same result, never discard on a storage failure. */
}
