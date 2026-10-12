/* PC testi icin SAHTE HAL: modulun kullandigi kadari. Gercek donanima dair
 * davranislar (flash'a ayni hucreye iki kez yazamamak, sayfa silme, PRIMASK) taklit edilir. */
#ifndef FAKE_STM32G0XX_HAL_H
#define FAKE_STM32G0XX_HAL_H

#include <stdint.h>
#include <string.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;

typedef struct { int dummy; } UART_HandleTypeDef;
typedef struct { int dummy; } RTC_HandleTypeDef;
typedef struct { int dummy; } SPI_HandleTypeDef;          /* main.h icin */
typedef struct { int dummy; } FDCAN_HandleTypeDef;
typedef struct { int dummy; } FDCAN_ProtocolStatusTypeDef;
typedef struct { int id; } GPIO_TypeDef;
typedef struct { uint8_t Hours, Minutes, Seconds, TimeFormat; uint32_t DayLightSaving, StoreOperation; } RTC_TimeTypeDef;
typedef struct { uint8_t Year, Month, Date, WeekDay; } RTC_DateTypeDef;
#define RTC_DAYLIGHTSAVING_NONE   0u
#define RTC_STOREOPERATION_RESET  0u
#define RTC_BKP_DR1               1u
#define RTC_BKP_DR2               2u
#define RTC_BKP_DR3               3u
#define RTC_BKP_DR4               4u
typedef struct { volatile uint32_t CSR; } RCC_TypeDef;
extern RCC_TypeDef fake_rcc;
#define RCC (&fake_rcc)
#define RCC_CSR_RMVF      (1u << 23)
#define RCC_CSR_OBLRSTF   (1u << 25)
#define RCC_CSR_PINRSTF   (1u << 26)
#define RCC_CSR_PWRRSTF   (1u << 27)
#define RCC_CSR_SFTRSTF   (1u << 28)
#define RCC_CSR_IWDGRSTF  (1u << 29)
#define RCC_CSR_WWDGRSTF  (1u << 30)
#define RCC_CSR_LPWRRSTF  (1u << 31)

extern GPIO_TypeDef fake_gpioa, fake_gpiob;
#define GPIOA (&fake_gpioa)
#define GPIOB (&fake_gpiob)
#define GPIO_PIN_2   0x0004u
#define GPIO_PIN_3   0x0008u
#define GPIO_PIN_10  0x0400u
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
#define GPIO_MODE_INPUT  0u
#define GPIO_NOPULL      0u
#define GPIO_PULLUP      1u
#define __HAL_RCC_GPIOA_CLK_ENABLE() do { } while (0)
void          HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *g);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
#define RTC_FORMAT_BIN 0u

uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *p, uint16_t n, uint32_t to);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState s);
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *h, RTC_TimeTypeDef *t, uint32_t fmt);
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *h, RTC_DateTypeDef *d, uint32_t fmt);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *h, RTC_TimeTypeDef *t, uint32_t fmt);
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *h, RTC_DateTypeDef *d, uint32_t fmt);
uint32_t          HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *h, uint32_t reg);
void              HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *h, uint32_t reg, uint32_t v);

/* ---- flash ---- */
#define FLASH_BASE      0x08000000UL
#define FLASH_PAGE_SIZE 0x800UL
#define FLASHSIZE_BASE  0x1FFF75E0UL
#define FLASH_TYPEERASE_PAGES        1u
#define FLASH_TYPEPROGRAM_DOUBLEWORD 1u
#define FLASH_BANK_1    1u
#define FLASH_BANK_2    2u
#define FLASH_DBANK_SUPPORT
#ifndef FAKE_BANK_NB
#define FAKE_BANK_NB 1u
#endif
#define FLASH_BANK_NB   FAKE_BANK_NB
#define FLASH_BANK_SIZE ((128u * 1024u) / FLASH_BANK_NB)   /* HAL: FLASH_SIZE (tek bank) ya da FLASH_SIZE/2 */
typedef struct { uint32_t TypeErase, Banks, Page, NbPages; } FLASH_EraseInitTypeDef;
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t adr, uint64_t data);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *hata);

/* ---- CMSIS ---- */
uint32_t __get_PRIMASK(void);
void     __set_PRIMASK(uint32_t v);
void     __disable_irq(void);

#endif
