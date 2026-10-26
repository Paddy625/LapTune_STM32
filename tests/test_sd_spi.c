#include "sd_spi.h"
#include "main.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
GPIO_TypeDef gpio_a,gpio_b;
static SPI_HandleTypeDef spi;
static uint8_t selected,tft_selected,initialized,legacy,absent,bad_crc,bad_write,forever_busy;
static uint8_t replies[1024],pending[512],storage[1024][512],write_stage,crc_hi;
static unsigned head,tail,services,commands,writes;
static uint32_t tick,write_sector;
uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t n) { tick+=n; }
void SD_SPI_Service(void) { services++; assert(!tft_selected); }
static void push(uint8_t b) { assert(head<sizeof(replies)); replies[head++]=b; }
static uint16_t checksum(const uint8_t *bytes,unsigned len)
{
    uint16_t c=0;
    for (unsigned i=0;i<len;++i) {
        c^=(uint16_t)bytes[i]<<8;
        for (unsigned j=0;j<8;++j) c=(c&0x8000U) ? (uint16_t)((c<<1)^0x1021U) : (uint16_t)(c<<1);
    }
    return c;
}
static void block(const uint8_t *bytes,unsigned len)
{
    push(0xFF); push(0xFE);
    for (unsigned i=0;i<len;++i) push(bytes[i]);
    uint16_t c=checksum(bytes,len); push(c>>8); push((uint8_t)c ^ bad_crc);
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState state)
{
    if (port==TFT_CS_GPIO_Port && pin==TFT_CS_Pin) tft_selected=state==GPIO_PIN_RESET;
    if (port==SD_CS_GPIO_Port && pin==SD_CS_Pin) {
        selected=state==GPIO_PIN_RESET;
        if (!selected) { head=tail=0; write_stage=0; }
    }
    assert(!(selected && tft_selected));
}
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *handle)
{
    assert(handle==&spi && !selected); return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle,const uint8_t *data,uint16_t len,uint32_t timeout)
{
    (void)timeout; assert(handle==&spi && selected && !tft_selected); tick++;
    if (write_stage==2) {
        assert(len==512); memcpy(pending,data,512); write_stage=3; return HAL_OK;
    }
    assert(len==6 && head==tail); head=tail=0; commands++;
    uint8_t cmd=data[0]&63U;
    uint32_t arg=((uint32_t)data[1]<<24)|((uint32_t)data[2]<<16)|((uint32_t)data[3]<<8)|data[4];
    if (absent) return HAL_OK;
    switch(cmd) {
    case 0: assert(data[5]==0x95); push(1); break;
    case 8: assert(data[5]==0x87); push(legacy?5:1); if (!legacy) { push(0);push(0);push(1);push(0xAA); } break;
    case 55: push(initialized?0:1); break;
    case 41: initialized=1; push(0); break;
    case 58: push(0);push(legacy?0x80:0xC0);push(0xFF);push(0x80);push(0);break;
    case 16: assert(legacy && arg==512);push(0);break;
    case 59: assert(arg==1);push(0);break;
    case 9: {
        uint8_t csd[16]={0};
        if (legacy) { csd[5]=9;csd[7]=63;csd[8]=0xC0; }
        else csd[0]=0x40;
        push(0);block(csd,16);break;
    }
    case 17: {
        uint32_t at=legacy?arg/512U:arg; assert(at<1024 && (!legacy || arg%512==0));
        push(0);block(storage[at],512);break;
    }
    case 24:
        write_sector=legacy?arg/512U:arg;
        assert(write_sector<1024 && (!legacy || arg%512==0));
        push(0);write_stage=1;break;
    case 13: push(0);push(0);break;
    default: assert(!"unexpected SD command");
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *handle,const uint8_t *tx,uint8_t *rx,uint16_t len,uint32_t timeout)
{
    (void)timeout; assert(handle==&spi && !tft_selected); tick++;
    for (unsigned i=0;i<len;++i) {
        if (!selected || absent) rx[i]=0xFF;
        else if (head!=tail) rx[i]=replies[tail++];
        else if (write_stage==1 && tx[i]==0xFE) { write_stage=2; rx[i]=0xFF; }
        else if (write_stage==3) { crc_hi=tx[i];write_stage=4;rx[i]=0xFF; }
        else if (write_stage==4) {
            assert((((uint16_t)crc_hi<<8)|tx[i])==checksum(pending,512));
            memcpy(storage[write_sector],pending,512); writes++;
            write_stage=0; head=tail=0;push(bad_write?0x0D:5);push(0);push(0);push(0xFF);rx[i]=0xFF;
        } else if (forever_busy && writes) rx[i]=0;
        else rx[i]=0xFF;
    }
    return HAL_OK;
}
static void init(uint8_t old)
{
    head=tail=initialized=selected=write_stage=absent=bad_crc=bad_write=forever_busy=0;
    legacy=old;services=commands=writes=tick=0;
    spi.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_8;
    SD_SPI_Bind(&spi); assert(disk_initialize(0)==0);
    assert(!selected && !tft_selected && spi.Init.BaudRatePrescaler==SPI_BAUDRATEPRESCALER_8);
    LBA_t sectors=0;assert(disk_ioctl(0,GET_SECTOR_COUNT,&sectors)==RES_OK && sectors==1024);
}
int main(void)
{
    for (unsigned old=0;old<2;++old) {
        init(old);
        uint8_t data[1024],out[1024];for (unsigned i=0;i<sizeof(data);++i) data[i]=(uint8_t)(i*13U);
        assert(disk_write(0,data,3,2)==RES_OK && writes==2);
        assert(disk_read(0,out,3,2)==RES_OK && !memcmp(data,out,sizeof(data)));
        assert(disk_ioctl(0,CTRL_SYNC,NULL)==RES_OK && services);
        assert(disk_read(0,out,1024,1)==RES_PARERR);
        bad_crc=1;assert(disk_read(0,out,3,1)==RES_ERROR && disk_status(0)==STA_NOINIT);
        assert(!selected && !tft_selected);
    }
    init(0);bad_write=1;uint8_t data[512]={0};assert(disk_write(0,data,0,1)==RES_ERROR);
    init(0);forever_busy=1;assert(disk_write(0,data,0,1)==RES_ERROR && tick<3000 && services);
    init(0);absent=1;assert(disk_initialize(0)==STA_NOINIT && !selected);
    assert(spi.Init.BaudRatePrescaler==SPI_BAUDRATEPRESCALER_8);
    puts("PASS SD SPI: SDHC/SDSC addressing, init clocks/CRC, sectors, CS isolation, timeouts, no card and data errors");
}
