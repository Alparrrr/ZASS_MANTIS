/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
extern FDCAN_ProtocolStatusTypeDef protocolStatus;
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
extern	FDCAN_HandleTypeDef hfdcan1;
extern	FDCAN_HandleTypeDef hfdcan3;
extern  SPI_HandleTypeDef hspi1;
extern  RTC_HandleTypeDef hrtc;
extern  RTC_DateTypeDef sDate;
extern  RTC_TimeTypeDef sTime;
#define SD_SPI_HANDLE hspi1
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SPI1_CS_Pin GPIO_PIN_4
#define SPI1_CS_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */
typedef struct {
    uint32_t ID;
    uint32_t IDE; // ID Type
    uint32_t RTR;
    uint32_t DLC;
    uint8_t  Data[8];
} FDCAN_Message_t;

         /* ACC / kontak    (bit5) */
void DWIN_Ekrana_Yaz_RTOS(uint16_t vp_address, uint16_t value);
void DWIN_Float_Yaz(uint16_t vp_address, float value);
void SOC_Bar_Guncelle(uint16_t soc, float akim_A);
void Sicaklik_Renk_Guncelle(int16_t t1, int16_t t2);
void Akim_Renk_Guncelle(float akim_A);
#define BMS_HUCRE_MAX  25u          /* BMS: en fazla hucre (CAN E0..E6, UART paketi) */
typedef struct {
    float    battery_volts;
    int16_t  battery_current;
    uint16_t soc;
    uint16_t cell_delta;
    int16_t  mos_temp;
    int16_t  batt_temp;
    uint16_t warning;
    uint16_t ave_cell_volt;
    uint16_t balance_status;
    uint16_t charge_mos;
    uint16_t discharge_mos;
    uint16_t cell_volts[BMS_HUCRE_MAX];   // BMS: 25 hucreye kadar (8 -> 24 -> 25)
    uint16_t cell_count;       // kac hucre var: sifir olmayan en son hucre (8/16/24/25)
	uint8_t  chg_plug;        /* sarj cihazi takili mi (BmsSwSta bit4) */
    uint8_t  heat_mos;        /* isitma MOS      (bit3) */
    uint8_t  acc_sta;
    uint8_t  sicak_kaynak;    /* sicak_ham'i kim doldurdu: 0 yok, 1 CAN ALL_TEMP, 2 UART */
    uint8_t  sicak_maske;     /* CAN ALL_TEMP TempMaskCode (bit0..4 = T1..T5 destekli) */
    uint8_t  sicak_ham[5];    /* T1..T5 ham (C + 50, 0xFF = yok); UART: T1 batarya, T3 MOS */
    uint8_t  sicak_mm_ok;     /* SD satiri icin (Task_SDCard doldurur): bit0 maks, bit1 min gecerli */
    int16_t  sicak_maks;      /* ekrandaki maks / min ile ayni (Guvenlik_Sicaklik)              */
    int16_t  sicak_min;
} bms_data_t;

// Global Veri Havuzu (16 Mesajlık Slot)
extern FDCAN_Message_t receivedMessage[16];
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
