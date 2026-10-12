/* ============================================================================
 * ZASS MANTIS - SAAT (RTC + pil)  (saat.h + saat.c)
 * ----------------------------------------------------------------------------
 * Pil (VBAT) takiliyken saatin elektrik kesikken de ilerlemesi icin RTC'nin
 * LSE (32.768 kHz kristal, PC14/PC15) ile calismasi SARTTIR: LSI ana besleme
 * kesilince durur. Acilista kaynak secimi (Saat_Kaynak_Hazirla):
 *   - RTC zaten LSE'de calisiyorsa (pil saati korudu): HICBIR SEYE dokunulmaz.
 *   - Degilse LSE baslatilir. Kalkarsa RTC LSE'ye alinir; ilk gecis yedek alani
 *     sifirlar, saat bir kez yeniden kurulur.
 *   - Kristal yok / baslamiyor: LSI ile devam. Saat calisir ama elektrik kesilince
 *     durur, dogrulugu ~%5. SD dosyasinin basinda "Saat kaynagi: LSI" yazar.
 *
 * SAAT KURMA:  (1) yedek alan bossa derleme zamani,
 *              (2) SD kartin kokune SAAT.TXT:  2026-10-07 15:30:00
 *                  (ya da 07.10.2026 15:30:00). Acilista / kart takilinca uygulanir,
 *                  dosyanin adi SAAT_OK.TXT olur (tekrar uygulanmaz).
 * ========================================================================== */
#ifndef SAAT_H
#define SAAT_H

#include "main.h"

#define SAAT_LSE_BEKLE_MS  2500u     /* LSE'nin kalkmasi icin en fazla bekleme     */
#define SAAT_IMZA          0x32F5u   /* RTC_BKP_DR1: "saat kuruldu" imzasi
                                        (eski kodun 0x32F4'u = sabit 19.06.2026)  */

enum { SAAT_KAYNAK_YOK = 0, SAAT_KAYNAK_LSE = 1, SAAT_KAYNAK_LSI = 2 };

typedef struct { uint16_t yil; uint8_t ay, gun, saat, dk, sn; } saat_t;

/* HAL_RTC_MspInit "USER CODE BEGIN RTC_MspInit 0" icinden cagrilir: RTC saat
 * kaynagini secer ve RTC saatini acar. 1 donerse MspInit'in kalanini (CubeMX'in
 * LSI secimini) ATLA - yoksa her acilista pilin korudugu saat silinir.        */
uint8_t     Saat_Kaynak_Hazirla(void);

/* MX_RTC_Init "USER CODE BEGIN RTC_Init 2" icinden: imza yoksa saati derleme
 * zamanina kurar; LSI'de bolucuyu 32 kHz'e gore duzeltir.                      */
void        Saat_Baslat(RTC_HandleTypeDef *rtc);

uint8_t     Saat_Kaynak(void);                 /* SAAT_KAYNAK_*                  */
const char *Saat_Kaynak_Adi(void);             /* "LSE" / "LSI" / "YOK"          */

uint8_t     Saat_Oku(saat_t *s);               /* tutarli okuma; 1 = basarili    */
uint8_t     Saat_Ayarla(const saat_t *s);      /* dogrulamali; 1 = basarili      */
uint8_t     Saat_Gecerli(const saat_t *s);
uint8_t     Saat_Metin_Coz(const char *m, saat_t *s);   /* 1 = gecerli tarih-saat */
void        Saat_Metin(const saat_t *s, char *buf, uint16_t boyut);  /* 2026.10.07 15:30:01 */
uint32_t    Saat_FatZaman(void);               /* FatFs get_fattime() bicimi     */
uint8_t     Saat_HaftaGunu(const saat_t *s);   /* 1 = Pazartesi ... 7 = Pazar   */
/* pil destekli yedek yazmaclar (RTC_BKP_DRx). DR1 = saat imzasi, DR2..DR4 = HardFault kaydi
 * (stm32g0xx_it.c). Saat_Baslat'tan once 0 dondurur.                                       */
uint32_t    Saat_Yedek_Oku(uint32_t reg);
void        Saat_Yedek_Yaz(uint32_t reg, uint32_t v);

#endif /* SAAT_H */
