#include "sd_spi.h"
#include "main.h"
#include "../FatFs/diskio.h"
#include <string.h>

static SPI_HandleTypeDef *bus;
static DSTATUS status = STA_NOINIT;
static uint8_t block_addressed, io_failed, initializing, dummy[32];
static uint32_t sectors, fast_prescaler, serviced;
volatile SD_SPI_Diagnostics sd_spi_diag;

const char *SD_SPI_ErrorName(void)
{
    static const char *const names[]={"OK","BUS","HAL","CLOCK","BUSY","CMD0","CMD8",
        "ACMD41","OCR","CMD16","CMD59","CSD","CAPACITY","READ","TOKEN","CRC",
        "WRITE","REJECT","STATUS"};
    return (unsigned)sd_spi_diag.error<sizeof(names)/sizeof(names[0]) ? names[sd_spi_diag.error] : "UNKNOWN";
}
static void reset_diagnostics(void)
{
    sd_spi_diag=(SD_SPI_Diagnostics){.command=0xFF,.response=0xFF,.token=0xFF,.cmd8_response=0xFF};
}
static void error(SD_SPI_Error reason)
{
    if (sd_spi_diag.error==SD_SPI_OK) sd_spi_diag.error=reason;
}
static void hal_error(HAL_StatusTypeDef result)
{
    if (result!=HAL_OK) {
        io_failed=1;
        if (sd_spi_diag.error==SD_SPI_OK) sd_spi_diag.hal_status=result;
        error(SD_SPI_HAL);
    }
}

__attribute__((weak)) void SD_SPI_Service(void) {}
void SD_SPI_Bind(SPI_HandleTypeDef *spi)
{
    bus = spi; status = STA_NOINIT; sectors = 0;
    initializing=io_failed=0; serviced=HAL_GetTick(); reset_diagnostics();
    fast_prescaler = spi ? spi->Init.BaudRatePrescaler : SPI_BAUDRATEPRESCALER_8;
    memset(dummy, 0xFF, sizeof(dummy));
}
static void service(void)
{
    if ((uint32_t)(HAL_GetTick()-serviced) >= 5U) {
        serviced = HAL_GetTick(); SD_SPI_Service();
    }
}
static uint8_t exchange(uint8_t tx)
{
    uint8_t rx = 0xFF;
    hal_error(HAL_SPI_TransmitReceive(bus, &tx, &rx, 1, 20));
    return rx;
}
static uint8_t speed(uint32_t prescaler)
{
    if (bus->Init.BaudRatePrescaler == prescaler) return 1;
    bus->Init.BaudRatePrescaler = prescaler;
    HAL_StatusTypeDef r=HAL_SPI_Init(bus);
    if (r!=HAL_OK) {
        if (sd_spi_diag.error==SD_SPI_OK) sd_spi_diag.hal_status=r;
        error(SD_SPI_CLOCK); io_failed=1;
    }
    return r==HAL_OK;
}
static void deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    (void)exchange(0xFF); /* Extra clocks let the card release MISO. */
}
static void release(void)
{
    deselect();
    (void)speed(fast_prescaler);
}
static uint8_t ready(uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    do {
        if (exchange(0xFF) == 0xFF) return !io_failed;
        service();
    } while (!io_failed && (uint32_t)(HAL_GetTick()-start) < timeout);
    error(SD_SPI_BUSY); return 0;
}
static uint8_t select_card(uint8_t cmd)
{
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
    /* End each command transaction, including CMD55/ACMD41. */
    deselect();
    if (!speed(initializing ? SPI_BAUDRATEPRESCALER_256 : SD_SPI_DATA_PRESCALER)) return 0;
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
    /* CMD0 must also work before the card has entered SPI mode. */
    return cmd==0 || ready(500);
}
static uint8_t crc7(const uint8_t *p, unsigned len)
{
    uint8_t crc = 0;
    for (unsigned i=0; i<len; ++i) for (unsigned bit=0; bit<8; ++bit) {
        crc <<= 1;
        if (((p[i] << bit) ^ crc) & 0x80U) crc ^= 0x09U;
    }
    return (uint8_t)((crc << 1) | 1U);
}
static uint16_t crc16(const uint8_t *p, unsigned len)
{
    uint16_t crc = 0;
    while (len--) {
        crc ^= (uint16_t)*p++ << 8;
        for (unsigned bit=0; bit<8; ++bit)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0));
    }
    return crc;
}
static uint8_t command(uint8_t cmd, uint32_t arg)
{
    uint8_t packet[] = {0x40U | cmd, arg>>24, arg>>16, arg>>8, arg, 0};
    packet[5] = crc7(packet,5);
    if (sd_spi_diag.error==SD_SPI_OK) {
        sd_spi_diag.command=cmd; sd_spi_diag.response=0xFF;
    }
    if (!select_card(cmd)) return 0xFF;
    hal_error(HAL_SPI_Transmit(bus,packet,sizeof(packet),20));
    if (io_failed) return 0xFF;
    for (unsigned i=0; i<10; ++i) {
        uint8_t r=exchange(0xFF);
        if (!(r & 0x80U)) {
            if (sd_spi_diag.error==SD_SPI_OK) sd_spi_diag.response=r;
            return r;
        }
    }
    return 0xFF;
}
static uint8_t receive_block(uint8_t *data, uint16_t length)
{
    uint32_t start=HAL_GetTick(); uint8_t token;
    do {
        token=exchange(0xFF);
        if (token!=0xFF) break;
        service();
    } while (!io_failed && (uint32_t)(HAL_GetTick()-start)<200U);
    if (sd_spi_diag.error==SD_SPI_OK) sd_spi_diag.token=token;
    if (token!=0xFE || io_failed) { error(SD_SPI_TOKEN); return 0; }
    for (uint16_t n=0; n<length;) {
        uint16_t part=(uint16_t)(length-n);
        if (part>sizeof(dummy)) part=sizeof(dummy);
        hal_error(HAL_SPI_TransmitReceive(bus,dummy,data+n,part,20));
        if (io_failed) return 0;
        n+=part;
    }
    uint16_t received=(uint16_t)exchange(0xFF)<<8;
    received |= exchange(0xFF);
    if (io_failed || received!=crc16(data,length)) { error(SD_SPI_CRC); return 0; }
    return 1;
}
static uint8_t card_status(void)
{
    uint8_t r=command(13,0), detail=exchange(0xFF);
    if (sd_spi_diag.error==SD_SPI_OK) sd_spi_diag.token=detail;
    if (r || detail || io_failed) { error(SD_SPI_STATUS); return 0; }
    return 1;
}
static uint8_t reset_to_idle(void)
{
    uint32_t start=HAL_GetTick(); uint8_t r;
    do {
        r=command(0,0);
        if (r==1 || io_failed) break;
        SD_SPI_Service(); HAL_Delay(10);
    } while ((uint32_t)(HAL_GetTick()-start)<1000U);
    if (r!=1 || io_failed) { error(SD_SPI_CMD0); return 0; }
    return 1;
}
DSTATUS disk_initialize(BYTE drive)
{
    if (drive) return STA_NOINIT;
    reset_diagnostics();
    if (!bus) { error(SD_SPI_NO_BUS); return STA_NOINIT; }
    status=STA_NOINIT; io_failed=0; block_addressed=0; sectors=0; initializing=1;
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port,TFT_CS_Pin,GPIO_PIN_SET);
    HAL_GPIO_WritePin(SD_CS_GPIO_Port,SD_CS_Pin,GPIO_PIN_SET);
    if (!speed(SPI_BAUDRATEPRESCALER_256)) goto done;
    HAL_Delay(10); SD_SPI_Service();
    for (unsigned i=0; i<10; ++i) (void)exchange(0xFF);
    uint8_t r=0xFF;
    for (unsigned attempt=0;attempt<3;++attempt) {
        if (!reset_to_idle()) goto done;
        r=command(8,0x1AA); sd_spi_diag.cmd8_response=r;
        if (io_failed) goto done;
        /* 00/04 after CMD0 are atypical: retry the complete reset/probe first. */
        if ((r==0 || r==4) && attempt<2) {
            ++sd_spi_diag.cmd8_retries; SD_SPI_Service(); HAL_Delay(10);
        } else break;
    }
    uint8_t v2=(r==1);
    if (v2) {
        uint8_t r7[4]; for (unsigned i=0;i<4;++i) r7[i]=exchange(0xFF);
        if (r7[0] || r7[1] || r7[2]!=1 || r7[3]!=0xAA) { error(SD_SPI_CMD8); goto done; }
    } else if (r==0 || r==4 || r==5) {
        /* 05 is the standard SD v1 response. 00/04 only permit a legacy probe:
           ACMD41, OCR and CSD below must independently confirm byte-addressed SD. */
        sd_spi_diag.legacy_probe=(r!=5);
    } else { error(SD_SPI_CMD8); goto done; } /* Never fall back on CRC errors/timeouts. */
    uint32_t start=HAL_GetTick();
    do {
        r=command(55,0);
        if (r>1) { error(SD_SPI_ACMD41); goto done; }
        r=command(41,v2 ? 0x40000000U : 0);
        if (!r) break;
        if (r!=1) { error(SD_SPI_ACMD41); goto done; }
        SD_SPI_Service(); HAL_Delay(1);
    } while ((uint32_t)(HAL_GetTick()-start)<1500U && !io_failed);
    if (r) { error(SD_SPI_ACMD41); goto done; }
    if (command(58,0)) { error(SD_SPI_OCR); goto done; }
    uint8_t ocr[4]; for (unsigned i=0;i<4;++i) ocr[i]=exchange(0xFF);
    if (!(ocr[0] & 0x80U) || !(ocr[1] & 0x30U)) { error(SD_SPI_OCR); goto done; }
    if (!v2 && (ocr[0]&0x40U)) { error(SD_SPI_OCR); goto done; }
    block_addressed=v2 && (ocr[0]&0x40U);
    if (!block_addressed && command(16,512)) { error(SD_SPI_CMD16); goto done; }
    r=command(59,1); /* Older cards may reject CMD59; still verify all read CRCs. */
    if (r!=0 && r!=4) { error(SD_SPI_CMD59); goto done; }
    sd_spi_diag.crc_enabled=r==0;
    uint8_t csd[16];
    if (command(9,0) || !receive_block(csd,sizeof(csd))) { error(SD_SPI_CSD); goto done; }
    if ((csd[0]>>6)!=(block_addressed ? 1U : 0U)) { error(SD_SPI_CSD); goto done; }
    uint64_t count=0;
    if ((csd[0]>>6)==1) {
        uint32_t size=((uint32_t)(csd[7]&63U)<<16)|((uint32_t)csd[8]<<8)|csd[9];
        count=((uint64_t)size+1U)*1024U;
    } else if ((csd[0]>>6)==0 && (csd[5]&15U)<=11U) {
        uint32_t size=((uint32_t)(csd[6]&3U)<<10)|((uint32_t)csd[7]<<2)|(csd[8]>>6);
        uint8_t mult=((csd[9]&3U)<<1)|(csd[10]>>7);
        count=((uint64_t)size+1U)*(1UL<<(mult+2U))*(1UL<<(csd[5]&15U))/512U;
    }
    if (!count || count>UINT32_MAX || io_failed) { error(SD_SPI_CAPACITY); goto done; }
    sectors=(uint32_t)count; status=0;
done:
    release(); initializing=0;
    if (io_failed || sd_spi_diag.error!=SD_SPI_OK) status=STA_NOINIT;
    SD_SPI_Service(); return status;
}
DSTATUS disk_status(BYTE drive) { return drive ? STA_NOINIT : status; }
DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count)
{
    if (drive) return RES_PARERR;
    if (status || !bus) return RES_NOTRDY;
    if (!count || !data || sector>=sectors || count>sectors-sector) return RES_PARERR;
    io_failed=0;
    for (UINT i=0;i<count;++i) {
        if (!block_addressed && sector>UINT32_MAX/512U) return RES_PARERR;
        uint8_t ok=!command(17,block_addressed ? sector : sector*512U) && receive_block(data,512);
        if (!ok) error(SD_SPI_READ);
        release(); SD_SPI_Service();
        if (!ok || io_failed) { status=STA_NOINIT; return RES_ERROR; }
        data+=512; ++sector;
    }
    return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *data,LBA_t sector,UINT count)
{
    if (drive) return RES_PARERR;
    if (status || !bus) return RES_NOTRDY;
    if (!count || !data || sector>=sectors || count>sectors-sector) return RES_PARERR;
    io_failed=0;
    for (UINT i=0;i<count;++i) {
        if (!block_addressed && sector>UINT32_MAX/512U) return RES_PARERR;
        uint8_t ok=!command(24,block_addressed ? sector : sector*512U);
        if (!ok) error(SD_SPI_WRITE);
        if (ok) {
            (void)exchange(0xFF); (void)exchange(0xFE);
            hal_error(HAL_SPI_Transmit(bus,data,512,20));
            uint16_t crc=crc16(data,512); (void)exchange(crc>>8); (void)exchange(crc);
            uint8_t response=0xFF;
            for (unsigned n=0;n<32 && response==0xFF && !io_failed;++n) response=exchange(0xFF);
            if (sd_spi_diag.error==SD_SPI_OK) sd_spi_diag.token=response;
            ok=(response&0x1FU)==5;
            if (!ok) error(SD_SPI_REJECT);
            if (ok) ok=ready(500) && card_status();
        }
        release(); SD_SPI_Service();
        if (!ok || io_failed) { status=STA_NOINIT; return RES_ERROR; }
        data+=512; ++sector;
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE cmd,void *out)
{
    if (drive || !bus) return RES_PARERR;
    if (status) return RES_NOTRDY;
    if (cmd==GET_SECTOR_COUNT && out) { *(LBA_t*)out=sectors; return RES_OK; }
    if (cmd==GET_SECTOR_SIZE && out) { *(WORD*)out=512; return RES_OK; }
    if (cmd==GET_BLOCK_SIZE && out) { *(DWORD*)out=1; return RES_OK; }
    if (cmd==CTRL_SYNC) {
        io_failed=0; uint8_t ok=card_status(); release(); SD_SPI_Service();
        if (!ok || io_failed) { status=STA_NOINIT; return RES_ERROR; }
        return RES_OK;
    }
    return RES_PARERR;
}
