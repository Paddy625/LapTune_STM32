#include "sd_spi.h"
#include "main.h"
#include "../FatFs/diskio.h"
#include <string.h>

static SPI_HandleTypeDef *bus;
static DSTATUS status = STA_NOINIT;
static uint8_t block_addressed, io_failed, dummy[32];
static uint32_t sectors, fast_prescaler, serviced;

__attribute__((weak)) void SD_SPI_Service(void) {}
void SD_SPI_Bind(SPI_HandleTypeDef *spi)
{
    bus = spi; status = STA_NOINIT; sectors = 0;
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
    if (HAL_SPI_TransmitReceive(bus, &tx, &rx, 1, 20) != HAL_OK) io_failed = 1;
    return rx;
}
static uint8_t speed(uint32_t prescaler)
{
    if (bus->Init.BaudRatePrescaler == prescaler) return 1;
    bus->Init.BaudRatePrescaler = prescaler;
    return HAL_SPI_Init(bus) == HAL_OK;
}
static void release(void)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    (void)exchange(0xFF); /* Extra clocks let the card release MISO. */
}
static uint8_t ready(uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    do {
        if (exchange(0xFF) == 0xFF) return !io_failed;
        service();
    } while (!io_failed && (uint32_t)(HAL_GetTick()-start) < timeout);
    return 0;
}
static uint8_t select_card(void)
{
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
    return ready(500);
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
    if (!ready(500)) return 0xFF;
    if (HAL_SPI_Transmit(bus,packet,sizeof(packet),20) != HAL_OK) { io_failed=1; return 0xFF; }
    for (unsigned i=0; i<10; ++i) {
        uint8_t r=exchange(0xFF);
        if (!(r & 0x80U)) return r;
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
    if (token!=0xFE || io_failed) return 0;
    for (uint16_t n=0; n<length;) {
        uint16_t part=(uint16_t)(length-n);
        if (part>sizeof(dummy)) part=sizeof(dummy);
        if (HAL_SPI_TransmitReceive(bus,dummy,data+n,part,20)!=HAL_OK) { io_failed=1; return 0; }
        n+=part;
    }
    uint16_t received=(uint16_t)exchange(0xFF)<<8;
    received |= exchange(0xFF);
    return !io_failed && received==crc16(data,length);
}
static uint8_t card_status(void)
{
    return command(13,0)==0 && exchange(0xFF)==0 && !io_failed;
}
DSTATUS disk_initialize(BYTE drive)
{
    if (drive || !bus) return STA_NOINIT;
    status=STA_NOINIT; io_failed=0; block_addressed=0;
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port,TFT_CS_Pin,GPIO_PIN_SET);
    HAL_GPIO_WritePin(SD_CS_GPIO_Port,SD_CS_Pin,GPIO_PIN_SET);
    if (!speed(SPI_BAUDRATEPRESCALER_256)) goto done;
    for (unsigned i=0; i<10; ++i) (void)exchange(0xFF);
    if (!select_card() || command(0,0)!=1) goto done;
    uint8_t r=command(8,0x1AA), v2=(r==1);
    if (v2) {
        uint8_t r7[4]; for (unsigned i=0;i<4;++i) r7[i]=exchange(0xFF);
        if (r7[2]!=1 || r7[3]!=0xAA) goto done;
    } else if (r!=5) goto done; /* SD v1 may reject CMD8; MMC is not supported. */
    uint32_t start=HAL_GetTick();
    do {
        r=command(55,0);
        if (r>1) goto done;
        r=command(41,v2 ? 0x40000000U : 0);
        if (!r) break;
        if (r!=1) goto done;
        SD_SPI_Service(); HAL_Delay(1);
    } while ((uint32_t)(HAL_GetTick()-start)<1500U && !io_failed);
    if (r || command(58,0)) goto done;
    uint8_t ocr[4]; for (unsigned i=0;i<4;++i) ocr[i]=exchange(0xFF);
    if (!(ocr[0] & 0x80U) || !(ocr[1] & 0x30U)) goto done;
    block_addressed=v2 && (ocr[0]&0x40U);
    if (!block_addressed && command(16,512)) goto done;
    if (command(59,1)) goto done; /* Enable card-side command/data CRC checks. */
    uint8_t csd[16];
    if (command(9,0) || !receive_block(csd,sizeof(csd))) goto done;
    uint64_t count=0;
    if ((csd[0]>>6)==1) {
        uint32_t size=((uint32_t)(csd[7]&63U)<<16)|((uint32_t)csd[8]<<8)|csd[9];
        count=((uint64_t)size+1U)*1024U;
    } else if ((csd[0]>>6)==0 && (csd[5]&15U)<=11U) {
        uint32_t size=((uint32_t)(csd[6]&3U)<<10)|((uint32_t)csd[7]<<2)|(csd[8]>>6);
        uint8_t mult=((csd[9]&3U)<<1)|(csd[10]>>7);
        count=((uint64_t)size+1U)*(1UL<<(mult+2U))*(1UL<<(csd[5]&15U))/512U;
    }
    if (!count || count>UINT32_MAX || io_failed) goto done;
    sectors=(uint32_t)count; status=0;
done:
    release();
    if (!speed(fast_prescaler) || io_failed) status=STA_NOINIT;
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
        uint8_t ok=select_card() && !command(17,block_addressed ? sector : sector*512U) && receive_block(data,512);
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
        uint8_t ok=select_card() && !command(24,block_addressed ? sector : sector*512U);
        if (ok) {
            (void)exchange(0xFF); (void)exchange(0xFE);
            if (HAL_SPI_Transmit(bus,data,512,20)!=HAL_OK) io_failed=1;
            uint16_t crc=crc16(data,512); (void)exchange(crc>>8); (void)exchange(crc);
            ok=(exchange(0xFF)&0x1FU)==5 && ready(500) && card_status();
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
        io_failed=0; uint8_t ok=select_card() && card_status(); release(); SD_SPI_Service();
        if (!ok || io_failed) { status=STA_NOINIT; return RES_ERROR; }
        return RES_OK;
    }
    return RES_PARERR;
}
