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
    uint16_t cell_volts[24];
} JK_BMS_Data;

// Global Veri Havuzu (16 Mesajlık Slot)
extern FDCAN_Message_t receivedMessage[16];
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
