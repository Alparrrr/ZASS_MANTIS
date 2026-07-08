#ifndef __USER_SD_SPI_H
#define __USER_SD_SPI_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32g0xx_hal.h"

/* ==============================================================================
                     DONANIM AYARLARI (KENDİ KARTINA GÖRE DÜZENLE)
============================================================================== */
extern SPI_HandleTypeDef hspi1;       // CubeMX'te hangi SPI'ı seçtiysen (hspi1, hspi2 vb.)
#define SD_SPI_HANDLE    hspi1

#define SD_CS_PORT       GPIOA        // Chip Select Portu
#define SD_CS_PIN        GPIO_PIN_4   // Chip Select Pini

// Eğer fiziksel bir Kart Algılama (CD) pini kullanmıyorsan burayı 0 yap
#define USE_SD_DETECT_PIN 0
#define SD_DETECT_PORT   GPIOB
#define SD_DETECT_PIN    GPIO_PIN_0
/* ============================================================================== */

/* Statü Tanımlamaları */
#define MSD_OK           0x00
#define MSD_ERROR        0x01
#define BSP_SD_ERROR     0x01
#define BSP_SD_TIMEOUT   0x02
#define SD_PRESENT       0x01
#define SD_NOT_PRESENT   0x00

/* ST Evaluation Kartından Çıkarılan Gerekli Yapılar */
typedef struct {
  uint8_t  ManufacturerID;
  uint16_t OEM_AppliID;
  uint32_t ProdName1;
  uint8_t  ProdName2;
  uint8_t  ProdRev;
  uint32_t ProdSN;
  uint8_t  Reserved1;
  uint16_t ManufactDate;
  uint8_t  CID_CRC;
  uint8_t  Reserved2;
} SD_CID;

typedef struct {
  uint8_t  CSDStruct;
  uint8_t  Reserved1;
  uint8_t  TAAC;
  uint8_t  NSAC;
  uint8_t  MaxBusClkFrec;
  uint16_t CardComdClasses;
  uint8_t  RdBlockLen;
  uint8_t  PartBlockRead;
  uint8_t  WrBlockMisalign;
  uint8_t  RdBlockMisalign;
  uint8_t  DSRImpl;
  union {
    struct {
      uint8_t  Reserved1;
      uint32_t DeviceSize;
      uint8_t  MaxRdCurrentVDDMin;
      uint8_t  MaxRdCurrentVDDMax;
      uint8_t  MaxWrCurrentVDDMin;
      uint8_t  MaxWrCurrentVDDMax;
      uint8_t  DeviceSizeMul;
    } v1;
    struct {
      uint8_t  Reserved1;
      uint32_t DeviceSize;
      uint8_t  Reserved2;
    } v2;
  } version;
  uint8_t  EraseSingleBlockEnable;
  uint8_t  EraseSectorSize;
  uint8_t  WrProtectGrSize;
  uint8_t  WrProtectGrEnable;
  uint8_t  Reserved2;
  uint8_t  WrSpeedFact;
  uint8_t  MaxWrBlockLen;
  uint8_t  WriteBlockPartial;
  uint8_t  Reserved3;
  uint8_t  FileFormatGrouop;
  uint8_t  CopyFlag;
  uint8_t  PermWrProtect;
  uint8_t  TempWrProtect;
  uint8_t  FileFormat;
  uint8_t  Reserved4;
  uint8_t  crc;
  uint8_t  Reserved5;
} SD_CSD;

typedef struct {
  SD_CSD Csd;
  SD_CID Cid;
  uint32_t CardCapacity;
  uint32_t CardBlockSize;
  uint32_t LogBlockNbr;
  uint32_t LogBlockSize;
} SD_CardInfo;

/* Public Fonksiyon Prototipleri */
uint8_t BSP_SD_Init(void);
uint8_t BSP_SD_IsDetected(void);
uint8_t BSP_SD_GetCardInfo(SD_CardInfo *pCardInfo);
uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout);
uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout);
uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr);
uint8_t BSP_SD_GetCardState(void);

#ifdef __cplusplus
}
#endif

#endif /* __USER_SD_SPI_H */
