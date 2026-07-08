/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "cmsis_os.h"
#include "app_fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdint.h"
#include "stdbool.h"
#include "string.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BMS_VOLT_MIN 16.0f
#define BMS_VOLT_MAX 30.0f
volatile uint8_t  bms_data_ready;    /* yeni cozulmus veri var */
volatile uint8_t  bms_dump_ready;    /* yeni ham paket var (dump icin) */
volatile uint8_t  bms_data_valid;    /* en az bir GECERLI (sifir-olmayan) paket geldi */
volatile uint32_t bms_last_rx_ms;    /* son gecerli paketin HAL_GetTick() zamani */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart3;
UART_HandleTypeDef huart6;
DMA_HandleTypeDef hdma_usart6_rx;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 256 * 4
};
/* Definitions for myTask02 */
osThreadId_t myTask02Handle;
const osThreadAttr_t myTask02_attributes = {
  .name = "myTask02",
  .priority = (osPriority_t) osPriorityLow,
  .stack_size = 2048 * 4
};
/* USER CODE BEGIN PV */
RTC_DateTypeDef sDate;
RTC_TimeTypeDef sTime;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_RTC_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USART6_UART_Init(void);
void StartDefaultTask(void *argument);
void StartTask02(void *argument);

/* USER CODE BEGIN PFP */
// JK BMS "Tüm Verileri Oku" sihirli dizisi

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE BEGIN PTD */
// JK BMS'den gelen anlamlı verileri tutacağımız yapı (Struct)
void battery_screen_task(void);

// Durum makinesi (State Machine) adımları
typedef enum {
    WAIT_A5, WAIT_5A, WAIT_5D, WAIT_82, WAIT_10, WAIT_00, RECEIVE_PAYLOAD
} BMS_State;
/* USER CODE END PTD */


/* USER CODE BEGIN PV */
uint16_t BMS = 0;
#define RX_BUF_SIZE 256
uint8_t dma_rx_buf[RX_BUF_SIZE];
#define BMS_PAYLOAD_LENGTH 90
float gercek_akim;
uint16_t CurentCounter ;
uint8_t isFirstStart = 0;
JK_BMS_Data my_bms;              // Her yerden erişebileceğimiz asıl BMS verimiz
BMS_State bms_state = WAIT_A5;   // Başlangıç durumu

uint8_t rx_byte;                 // Anlık okunan 1 byte
uint8_t bms_payload[BMS_PAYLOAD_LENGTH]; // Yakalanan 90 bytelık ana veri
uint16_t payload_index = 0;
volatile uint8_t bms_data_ready = 0;     // Ana döngüye haber verecek bayrak
uint8_t x;
uint8_t dma_raw_buffer[256]; // DMA'nın ham veri karalama defteri
   // Asla kaymayan, jilet gibi sabit dizimiz
volatile uint8_t packet_count = 0; // Kaç tane sağlam paket yakaladık (Debug için)
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
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_RTC_Init();
  if (MX_FATFS_Init() != APP_OK) {
    Error_Handler();
  }
  MX_USART3_UART_Init();
  MX_USART6_UART_Init();
  /* USER CODE BEGIN 2 */
  // HAL_UART_Receive_IT(&huart6, &rx_byte, 1);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, RESET);
// UART Idle kesmesini başlat
  HAL_UARTEx_ReceiveToIdle_DMA(&huart6, dma_raw_buffer, 256);
  __HAL_DMA_DISABLE_IT(huart6.hdmarx, DMA_IT_HT);
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of myTask02 */
  myTask02Handle = osThreadNew(StartTask02, NULL, &myTask02_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  // 1. Buffer'ı temizle
	  // Komutu hatta gönder (6 byte)


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

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutPullUp = RTC_OUTPUT_PULLUP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  /* USER CODE BEGIN RTC_Init 2 */

    // Yedekleme register'ını kontrol et.
    // Eğer daha önce 0x32F2 (rastgele seçilmiş bir imza) yazılmamışsa, saat hiç ayarlanmamış demektir.
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) != 0x32F4)
    {
        // 1. SAAT AYARI (Örnek: 14:30:00)
        sTime.Hours = 15;
        sTime.Minutes = 10;
        sTime.Seconds = 0;
        sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        sTime.StoreOperation = RTC_STOREOPERATION_RESET;

        // RTC_FORMAT_BIN kullanıyoruz çünkü sayıları direkt onluk tabanda (14, 30 vb.) yazdık.
        if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
        {
            Error_Handler();
        }

        // 2. TARİH AYARI (Örnek: 21 Nisan 2026, Salı)
        sDate.WeekDay = RTC_WEEKDAY_TUESDAY;
        sDate.Month = RTC_MONTH_JUNE;
        sDate.Date = 19;
        sDate.Year = 26; // 2026'nın 26'sı

        if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
        {
            Error_Handler();
        }

        // 3. İMZAYI YAZ
        // Saat başarıyla ayarlandı. Artık Vbat (pil) bitene kadar bu if bloğuna bir daha girmeyecek.
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0x32F4);
    }
  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USART6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */

  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */

  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 2400;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  huart6.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart6.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart6.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */

  /* USER CODE END USART6_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, SPI1_CS_Pin|GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin : SPI1_CS_Pin */
  GPIO_InitStruct.Pin = SPI1_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(SPI1_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/*void BMS_Mesajini_Coz(void) {
    my_bms.battery_volts   = (float)((bms_payload[0] << 8) | bms_payload[1]) / 100.0;
    my_bms.battery_current = (int16_t)((bms_payload[2] << 8) | bms_payload[3]);
    my_bms.soc             = (bms_payload[6] << 8) | bms_payload[7];
    my_bms.cell_delta      = (bms_payload[8] << 8) | bms_payload[9];
    my_bms.mos_temp        = (int16_t)((bms_payload[10] << 8) | bms_payload[11]);
    my_bms.batt_temp       = (int16_t)((bms_payload[12] << 8) | bms_payload[13]);
    my_bms.warning         = (bms_payload[14] << 8) | bms_payload[15];
    my_bms.ave_cell_volt   = (bms_payload[16] << 8) | bms_payload[17];
    my_bms.balance_status  = (bms_payload[18] << 8) | bms_payload[19];
    my_bms.charge_mos      = (bms_payload[20] << 8) | bms_payload[21];
    my_bms.discharge_mos   = (bms_payload[22] << 8) | bms_payload[23];

    // İlk 8 hücrenin voltajını okuma (Kendi hücre sayına göre artırabilirsin)
    for (int i = 0; i < 8; i++) {
        uint16_t cell_index = 24 + (i * 2);
        my_bms.cell_volts[i] = (bms_payload[cell_index] << 8) | bms_payload[cell_index + 1];
    }
}*/
void BMS_Mesajini_Coz(uint16_t header_index) {

    // Asıl veriler (payload) başlıktan 6 byte sonra başlıyor
    // Eski kodundaki 0. index, artık p + 0 anlamına gelecek.
    uint16_t p = header_index + 6;

    my_bms.battery_volts   = (float)((dma_raw_buffer[p + 0] << 8) | dma_raw_buffer[p + 1]) / 100.0f;
    my_bms.battery_current = (int16_t)((dma_raw_buffer[p + 2] << 8) | dma_raw_buffer[p + 3]);
    my_bms.soc             = (dma_raw_buffer[p + 6] << 8) | dma_raw_buffer[p + 7];
    my_bms.cell_delta      = (dma_raw_buffer[p + 8] << 8) | dma_raw_buffer[p + 9];
    my_bms.mos_temp        = (int16_t)((dma_raw_buffer[p + 10] << 8) | dma_raw_buffer[p + 11]);
    my_bms.batt_temp       = (int16_t)((dma_raw_buffer[p + 12] << 8) | dma_raw_buffer[p + 13]);
    my_bms.warning         = (dma_raw_buffer[p + 14] << 8) | dma_raw_buffer[p + 15];
    my_bms.ave_cell_volt   = (dma_raw_buffer[p + 16] << 8) | dma_raw_buffer[p + 17];
    my_bms.balance_status  = (dma_raw_buffer[p + 18] << 8) | dma_raw_buffer[p + 19];
    my_bms.charge_mos      = (dma_raw_buffer[p + 20] << 8) | dma_raw_buffer[p + 21];
    my_bms.discharge_mos   = (dma_raw_buffer[p + 22] << 8) | dma_raw_buffer[p + 23];

    // İlk 8 hücrenin voltajını okuma
    for (int i = 0; i < 8; i++) {
        uint16_t cell_index = p + 24 + (i * 2);
        my_bms.cell_volts[i] = (dma_raw_buffer[cell_index] << 8) | dma_raw_buffer[cell_index + 1];
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART6)
    {
        for (int i = 0; i <= (Size - 96); i++)
        {
            if (dma_raw_buffer[i] == 0xA5 &&
                dma_raw_buffer[i+1] == 0x5A &&
                dma_raw_buffer[i+2] == 0x5D &&
                dma_raw_buffer[i+3] == 0x82)
            {
                // BİNGO! Başlığı bulduk.
                // Tek yapmamız gereken "i" (başlangıç indexi) değerini fonksiyonumuza göndermek.
                BMS_Mesajini_Coz(i);

                bms_data_ready = 1; // Ana döngüye haber ver
                bms_last_rx_ms = HAL_GetTick();   // <-- SADECE BU SATIRI EKLE (mesaj geldi -> zaman damgası)
                break;
            }
        }


        // Yeniden dinlemeyi kur
        HAL_UART_AbortReceive(huart);
        HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_raw_buffer, 256);
        __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
    }
}

// Hata olursa kilitlenmeyi çözen kurtarıcı
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);

        HAL_UART_AbortReceive(huart);
        HAL_UARTEx_ReceiveToIdle_DMA(huart, dma_raw_buffer, 256);
        __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
    }
}
void DWIN_Ekrana_Yaz_RTOS(uint16_t vp_address, uint16_t value) {
    uint8_t dwin_frame[8];
    dwin_frame[0] = 0x5A;
    dwin_frame[1] = 0xA5;
    dwin_frame[2] = 0x05;
    dwin_frame[3] = 0x82;
    dwin_frame[4] = (vp_address >> 8);
    dwin_frame[5] = (vp_address & 0xFF);
    dwin_frame[6] = (value >> 8);
    dwin_frame[7] = (value & 0xFF);

    HAL_UART_Transmit(&huart3, dwin_frame, 8, 100);

    // RTOS'ta bu görev uyurken diğer görevler (SD Kart, CAN) çalışmaya devam eder
    osDelay(5); // CMSIS-RTOS kullanıyorsan osDelay, saf FreeRTOS ise vTaskDelay(pdMS_TO_TICKS(5))
}
void DWIN_Float_Yaz(uint16_t vp_address, float value) {
    uint8_t dwin_frame[10]; // Float için paket daha uzundur (10 byte)

    dwin_frame[0] = 0x5A;
    dwin_frame[1] = 0xA5;
    dwin_frame[2] = 0x07; // Uzunluk: Komut(1) + Adres(2) + Float Veri(4) = 7
    dwin_frame[3] = 0x82; // Yazma Komutu

    dwin_frame[4] = (vp_address >> 8);  // VP Adres High
    dwin_frame[5] = (vp_address & 0xFF);// VP Adres Low

    // Sihirli Kısım: Float değişkenini RAM'de byte byte parçalıyoruz
    uint8_t *float_bytes = (uint8_t *)&value;

    // STM32 (Little-Endian) -> DWIN (Big-Endian) dönüşümü için ters sıralıyoruz
    dwin_frame[6] = float_bytes[3];
    dwin_frame[7] = float_bytes[2];
    dwin_frame[8] = float_bytes[1];
    dwin_frame[9] = float_bytes[0];

    HAL_UART_Transmit(&huart3, dwin_frame, 10, 100);
}
/* Cozulen paket makul mu? (sifir / cop paketleri ele) */
static uint8_t bms_frame_plausible(void)
{
    if (my_bms.battery_volts < BMS_VOLT_MIN) return 0;  /* 0 dahil mantiksiz */
    if (my_bms.battery_volts > BMS_VOLT_MAX) return 0;
    return 1;
}

uint8_t BMS_IsReady(void)
{
    if (!bms_data_ready) return 0;
    return ((HAL_GetTick() - bms_last_rx_ms) <= 3000) ? 1 : 0;
}

void BMS_Buzzer_Update(void)
{
    /* 1) Henuz gecerli veri yok VEYA veri bayatladi -> KESIN SUS */
    if (!BMS_IsReady()) {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
        return;
    }
    /* 2) Gercek alarm varsa ot, yoksa sus */
    if (my_bms.warning != 0 || my_bms.soc <= 20){

        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
        CurentCounter = 0;

        if(my_bms.soc <= 20 && my_bms.battery_current >=1){
        	CurentCounter++;

        			if(CurentCounter >=30){
        				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, RESET);
        			}

        	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
        }
    }
    else
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {

	  x++;
	  HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_12);
	  // 1. ADIM: 36 Byte'lık paketi UART6 üzerinden gönder
	  //
	 /* // Eğer kesme fonksiyonu tam bir 0x1000 paketini başarıyla aldıysa:
	  if (bms_data_ready == 1) {

	            bms_data_ready = 0; // Bayrağı hemen indir

	            // Ham byte dizisini float ve int değerlerine çevir
	            BMS_Mesajini_Coz();

	            // -> TEBRİKLER! VERİLER HAZIR <-
	            // Debug modunda "my_bms" struct'ını izlemeye al.
	            // my_bms.battery_volts 26.33 vb. değerleri canlı göreceksin.
	        }*/
	  // SoC - 0x1000
	             // DWIN_Ekrana_Yaz_RTOS(0x1000, my_bms.soc);

	              // Voltaj - 0x1060 (26.33V -> 263 gönderiyoruz)
	        //      DWIN_Float_Yaz(0x1060, my_bms.battery_volts);

	              // Akım - 0x1080
	           //   float gercek_akim = (float)my_bms.battery_current / 10.0f ;
	                        // Not: BMS akımı 100 katı veriyorsa burayı 100.0f yapmalısın.
	           //             DWIN_Float_Yaz(0x1080, gercek_akim);

	              // Sıcaklıklar
	           //   DWIN_Ekrana_Yaz_RTOS(0x1020, (uint16_t)my_bms.mos_temp);
	     //         DWIN_Ekrana_Yaz_RTOS(0x1040, (uint16_t)my_bms.batt_temp);
	       //       SOC_Bar_Guncelle(gercek_akim, my_bms.soc);
	  DWIN_Float_Yaz(0x1060, my_bms.battery_volts);          // voltaj (senin)
	      float gercek_akim = (float)my_bms.battery_current / 10.0f;
	      DWIN_Float_Yaz(0x1080, gercek_akim);                   // akim (senin)
	      Akim_Renk_Guncelle(gercek_akim);
	      DWIN_Ekrana_Yaz_RTOS(0x1020, (uint16_t)my_bms.mos_temp);
	      DWIN_Ekrana_Yaz_RTOS(0x1040, (uint16_t)my_bms.batt_temp);
	      Sicaklik_Renk_Guncelle(my_bms.mos_temp, my_bms.batt_temp);
	      Akim_Renk_Guncelle(gercek_akim);
	      if (HAL_GetTick() - bms_last_rx_ms > 3000) {
	               bms_data_ready = 0;
	           }

	      if (BMS_IsReady()) {
	          BMS_Buzzer_Update();
	      }

	      SOC_Bar_Guncelle(my_bms.soc, gercek_akim);             // <-- EKLE: SOC+bar+durum+renk
    osDelay(100);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the myTask02 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  /* Infinite loop */
  for(;;)
  {
	SD_Log_Process();
	//  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, SET);

    osDelay(1000);
  }
  /* USER CODE END StartTask02 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
