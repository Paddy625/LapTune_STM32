GPIO_TypeDef gpio_a,gpio_b; SPI_HandleTypeDef hspi2;
uint8_t lsm6ds3_registers[16],lsm6ds3_config_verified;
static uint8_t registers[128]={[0x0F]=0x69,[0x10]=0x4C,[0x11]=0x44,[0x12]=0x44,[0x13]=4};
#ifndef IMU_FIXTURE_EXTERNAL_CLOCK
static uint64_t now;
uint64_t Timebase_NowUs(void) { return now; }
#endif
static uint8_t *dma_rx; static int fail_dma,corrupt_configuration;
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

uint8_t LSM6DS3_Init(void) { return 1; }
void HAL_Delay(uint32_t ms) { now+=(uint64_t)ms*1000U; }
