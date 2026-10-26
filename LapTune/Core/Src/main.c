/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "acc.h"
#include "acc_display.h"
#include "gps.h"
#include "gps_display.h"
#include "sd_spi.h"
#include "sd_logger.h"
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

float ax, max_x=0, min_x=0, ay, max_y=0, min_y=0, az, max_z=0, min_z=0;
float gx, min_gx=0, max_gx=0, gy, min_gy=0, max_gy=0, gz, min_gz=0, max_gz=0;
static uint8_t sensor_ok, have_sample;
static uint32_t last_sensor_retry, last_sensor_data;
static uint8_t app_ready;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void ServiceAcquisition(void)
{
  if (!app_ready) return;
  GPS_Process(HAL_GetTick());
  LogClock_Update(GPS_GetData());
  if (!sensor_ok) return;
  for (unsigned n=0; n<32; ++n) {
    LSM6DS3_Sample sample;
    uint16_t pending=0;
    LSM6DS3_ReadResult result=LSM6DS3_ReadFIFO(&sample,&pending);
    if (result==LSM6DS3_READ_NOT_READY) {
      if ((uint32_t)(HAL_GetTick()-last_sensor_data)>=1000U) {
        sensor_ok=0; SDLogger_DataLoss();
      }
      break;
    }
    if (result!=LSM6DS3_READ_OK) {
      sensor_ok=0; SDLogger_DataLoss(); break;
    }
    last_sensor_data=HAL_GetTick();
    uint32_t sample_ms=HAL_GetTick()-(uint32_t)(pending-1U)*1000U/104U;
    ax=sample.ax; ay=sample.ay; az=sample.az;
    gx=sample.gx; gy=sample.gy; gz=sample.gz;
    if (!have_sample) {
      min_x=max_x=ax; min_y=max_y=ay; min_z=max_z=az;
      min_gx=max_gx=gx; min_gy=max_gy=gy; min_gz=max_gz=gz;
      have_sample=1;
    }
    if (ax>max_x) max_x=ax;
    if (ax<min_x) min_x=ax;
    if (ay>max_y) max_y=ay;
    if (ay<min_y) min_y=ay;
    if (az>max_z) max_z=az;
    if (az<min_z) min_z=az;
    if (gx>max_gx) max_gx=gx;
    if (gx<min_gx) min_gx=gx;
    if (gy>max_gy) max_gy=gy;
    if (gy<min_gy) min_gy=gy;
    if (gz>max_gz) max_gz=gz;
    if (gz<min_gz) min_gz=gz;
    SDLogger_Append(&sample,LogClock_At(sample_ms));
  }
}

void SD_SPI_Service(void) { ServiceAcquisition(); }

static HAL_StatusTypeDef ShowLogState(void)
{
  static int previous=-1;
  static char previous_filename[sizeof(sd_log_filename)];
  if ((int)sd_log_state==previous && !strcmp(previous_filename,sd_log_filename)) return HAL_OK;
  char text[32];
  const char *label="SD OFF";
  switch (sd_log_state) {
  case LOG_RECORDING: label="SD REC"; break;
  case LOG_STOPPED: label="SD SAVED"; break;
  case LOG_CARD_ERROR: label="SD ERROR"; break;
  case LOG_FULL: label="SD FULL"; break;
  case LOG_DATA_LOSS: label="SD DATA LOSS"; break;
  default: break;
  }
  (void)snprintf(text,sizeof(text),"%-12s%-12s",label,sd_log_filename);
  HAL_StatusTypeDef result=TFT_Text(4,120,text,1,sd_log_state==LOG_RECORDING || sd_log_state==LOG_STOPPED ? TFT_WHITE : TFT_RED,TFT_BLACK);
  if (result==HAL_OK) {
    previous=(int)sd_log_state;
    (void)strcpy(previous_filename,sd_log_filename);
  }
  return result;
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_SPI2_Init();
  MX_SPI1_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */

  if (TFT_Init(&hspi1) != HAL_OK || AccDisplay_Init() != HAL_OK) {
    Error_Handler();
  }
  if (GPS_Init(&huart3) != HAL_OK) Error_Handler();
  sensor_ok = LSM6DS3_Init();
  HAL_Delay(100U); /* Let gyro/accelerometer settle before enabling FIFO. */
  if (sensor_ok) sensor_ok=LSM6DS3_EnableFIFO();
  last_sensor_retry=last_sensor_data=HAL_GetTick();
  app_ready=1;
  SD_SPI_Bind(&hspi1);
  SDLogger_Start();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now=HAL_GetTick();
    ServiceAcquisition();
    if (!sensor_ok && (uint32_t)(now-last_sensor_retry)>=1000U) {
      sensor_ok=LSM6DS3_Init();
      HAL_Delay(100U);
      if (sensor_ok) sensor_ok=LSM6DS3_EnableFIFO();
      last_sensor_retry=last_sensor_data=HAL_GetTick();
    }
    SDLogger_Process(HAL_GetTick());
    const IMU_DisplayData readings = {
      .accel = {ax, ay, az},
      .accel_min = {min_x, min_y, min_z},
      .accel_max = {max_x, max_y, max_z},
      .gyro = {gx, gy, gz},
      .gyro_min = {min_gx, min_gy, min_gz},
      .gyro_max = {max_gx, max_gy, max_gz}
    };
    if (AccDisplay_Update(HAL_GetTick(), &readings,
                          sensor_ok && have_sample) != HAL_OK) {
      Error_Handler();
    }
    if (GPSDisplay_Update(HAL_GetTick(), GPS_GetData()) != HAL_OK) {
      Error_Handler();
    }
    if (ShowLogState()!=HAL_OK) Error_Handler();
    HAL_Delay(1U);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1: ST7735S, mode 0, 8 MHz at PCLK2=64 MHz.
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi2.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3: NEO-6M default NMEA, 9600 baud, 8N1.
  */
static void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK) Error_Handler();
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure TFT control pins PA8/PA9. PA5 is now SPI1 SCK. */
  GPIO_InitStruct.Pin = TFT_RST_Pin|TFT_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure TFT CS (PB6) and LSM6DS3 CS (PB12), both inactive high. */
  GPIO_InitStruct.Pin = GPIO_PIN_12|TFT_CS_Pin|SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  /* The Nucleo B1 release edge requests a foreground flush/close, even during SD I/O. */
  if (pin==B1_Pin) SDLogger_RequestStop();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  GPS_OnRxComplete(uart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  GPS_OnError(uart);
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
