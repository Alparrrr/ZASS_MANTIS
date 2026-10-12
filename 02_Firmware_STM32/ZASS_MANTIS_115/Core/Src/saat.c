/* ============================================================================
 * ZASS MANTIS - SAAT (RTC + pil). Aciklama: saat.h
 * ========================================================================== */
#include "saat.h"
#include <string.h>
#include <stdio.h>

static RTC_HandleTypeDef *s_rtc;
static uint8_t s_kaynak = SAAT_KAYNAK_YOK;

/* ==========================================================================
 * DONANIM: saat kaynagi (PC testinde derlenmez)
 * ========================================================================== */
#ifndef SAAT_PC_TEST
static uint8_t lse_baslat(void)
{
    uint32_t t0;
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);   /* hizli ve guvenli kalkis */
    __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
    t0 = HAL_GetTick();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == 0U) {
        if ((HAL_GetTick() - t0) > SAAT_LSE_BEKLE_MS) {
            __HAL_RCC_LSE_CONFIG(RCC_LSE_OFF);             /* kristal yok / calismiyor */
            return 0;
        }
    }
    return 1;
}

uint8_t Saat_Kaynak_Hazirla(void)
{
    RCC_PeriphCLKInitTypeDef pc;

    HAL_PWR_EnableBkUpAccess();                            /* BDCR'ye yazma izni */
    if (__HAL_RCC_GET_RTC_SOURCE() == RCC_RTCCLKSOURCE_LSE &&
        __HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != 0U) {
        s_kaynak = SAAT_KAYNAK_LSE;                        /* pil korudu: dokunma */
    } else {
        memset(&pc, 0, sizeof(pc));
        pc.PeriphClockSelection = RCC_PERIPHCLK_RTC;
        if (lse_baslat()) { pc.RTCClockSelection = RCC_RTCCLKSOURCE_LSE; s_kaynak = SAAT_KAYNAK_LSE; }
        else              { pc.RTCClockSelection = RCC_RTCCLKSOURCE_LSI; s_kaynak = SAAT_KAYNAK_LSI; }
        /* kaynak degisirse HAL yedek alani sifirlar (saat + BKP silinir, bir kerelik) */
        if (HAL_RCCEx_PeriphCLKConfig(&pc) != HAL_OK) { s_kaynak = SAAT_KAYNAK_YOK; return 0; }
    }
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_RTCAPB_CLK_ENABLE();
    return 1;
}

/* LSI = 32 kHz: 32000 / (128 x 250) = 1 Hz. CubeMX'in 255'i 32.768 kHz'e gore (LSI'de %2,3 geri). */
static void lsi_bolucu(void)
{
    if ((s_rtc->Instance->PRER & RTC_PRER_PREDIV_S) == 249U) return;
    s_rtc->Init.SynchPrediv = 249U;
    __HAL_RTC_WRITEPROTECTION_DISABLE(s_rtc);
    if (RTC_EnterInitMode(s_rtc) == HAL_OK) {               /* takvim korunur */
        s_rtc->Instance->PRER = 249U;
        s_rtc->Instance->PRER |= (127U << RTC_PRER_PREDIV_A_Pos);
        (void)RTC_ExitInitMode(s_rtc);
    }
    __HAL_RTC_WRITEPROTECTION_ENABLE(s_rtc);
}
#endif /* SAAT_PC_TEST */

/* derleme zamani: __DATE__ = "Oct  7 2026", __TIME__ = "15:30:00" */
static void derleme_zamani(saat_t *s)
{
    static const char AY[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__, *t = __TIME__;
    uint8_t i;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < 12u; i++)
        if (strncmp(d, &AY[i * 3u], 3) == 0) { s->ay = (uint8_t)(i + 1u); break; }
    s->gun  = (uint8_t)(((d[4] == ' ') ? 0 : (d[4] - '0')) * 10 + (d[5] - '0'));
    s->yil  = (uint16_t)((d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + (d[10] - '0'));
    s->saat = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
    s->dk   = (uint8_t)((t[3] - '0') * 10 + (t[4] - '0'));
    s->sn   = (uint8_t)((t[6] - '0') * 10 + (t[7] - '0'));
}

void Saat_Baslat(RTC_HandleTypeDef *rtc)
{
    saat_t s;
    s_rtc = rtc;
#ifndef SAAT_PC_TEST
    if (s_kaynak == SAAT_KAYNAK_LSI) lsi_bolucu();
#endif
    if (HAL_RTCEx_BKUPRead(rtc, RTC_BKP_DR1) != SAAT_IMZA) {
        derleme_zamani(&s);
        (void)Saat_Ayarla(&s);                                 /* imzayi da yazar */
    }
}

uint8_t Saat_Kaynak(void) { return s_kaynak; }

uint32_t Saat_Yedek_Oku(uint32_t reg) { return (s_rtc != NULL) ? HAL_RTCEx_BKUPRead(s_rtc, reg) : 0u; }
void     Saat_Yedek_Yaz(uint32_t reg, uint32_t v) { if (s_rtc != NULL) HAL_RTCEx_BKUPWrite(s_rtc, reg, v); }

const char *Saat_Kaynak_Adi(void)
{
    return (s_kaynak == SAAT_KAYNAK_LSE) ? "LSE" : (s_kaynak == SAAT_KAYNAK_LSI) ? "LSI" : "YOK";
}

/* ==========================================================================
 * TAKVIM
 * ========================================================================== */
static uint8_t ay_gunu(uint16_t y, uint8_t a)
{
    static const uint8_t G[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (a < 1u || a > 12u) return 0;
    if (a == 2u && ((y % 4u == 0u && y % 100u != 0u) || y % 400u == 0u)) return 29;
    return G[a - 1u];
}

uint8_t Saat_Gecerli(const saat_t *s)
{
    if (s == NULL || s->yil < 2000u || s->yil > 2099u) return 0;
    if (s->gun < 1u || s->gun > ay_gunu(s->yil, s->ay)) return 0;
    return (uint8_t)(s->saat < 24u && s->dk < 60u && s->sn < 60u);
}

uint8_t Saat_HaftaGunu(const saat_t *s)                /* Sakamoto; HAL: 1 Pzt .. 7 Paz */
{
    static const uint8_t T[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    uint16_t y = s->yil;
    uint8_t g;
    if (s->ay < 3u) y--;
    g = (uint8_t)((y + y / 4u - y / 100u + y / 400u + T[(s->ay - 1u) % 12u] + s->gun) % 7u);   /* 0 = Pazar */
    return (g == 0u) ? 7u : g;
}

uint8_t Saat_Oku(saat_t *s)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    uint32_t pm;
    uint8_t ok;
    if (s_rtc == NULL || s == NULL) return 0;
    pm = __get_PRIMASK();
    __disable_irq();                      /* GetTime golge kaydi kilitler, GetDate acar */
    ok = (uint8_t)(HAL_RTC_GetTime(s_rtc, &t, RTC_FORMAT_BIN) == HAL_OK);
    if (HAL_RTC_GetDate(s_rtc, &d, RTC_FORMAT_BIN) != HAL_OK) ok = 0;
    __set_PRIMASK(pm);
    if (!ok) return 0;
    s->yil = (uint16_t)(2000u + d.Year); s->ay = d.Month; s->gun = d.Date;
    s->saat = t.Hours; s->dk = t.Minutes; s->sn = t.Seconds;
    return 1;
}

uint8_t Saat_Ayarla(const saat_t *s)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (s_rtc == NULL || !Saat_Gecerli(s)) return 0;
    memset(&t, 0, sizeof(t));
    memset(&d, 0, sizeof(d));
    t.Hours = s->saat; t.Minutes = s->dk; t.Seconds = s->sn;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    d.Year = (uint8_t)(s->yil - 2000u); d.Month = s->ay; d.Date = s->gun;
    d.WeekDay = Saat_HaftaGunu(s);
    if (HAL_RTC_SetTime(s_rtc, &t, RTC_FORMAT_BIN) != HAL_OK) return 0;
    if (HAL_RTC_SetDate(s_rtc, &d, RTC_FORMAT_BIN) != HAL_OK) return 0;
    HAL_RTCEx_BKUPWrite(s_rtc, RTC_BKP_DR1, SAAT_IMZA);
    return 1;
}

/* Metindeki ilk 6 sayiyi alir: "2026-10-07 15:30:00" ya da "07.10.2026 15:30" (saniye istege bagli) */
uint8_t Saat_Metin_Coz(const char *m, saat_t *s)
{
    uint32_t n[6];
    uint8_t adet = 0, hane = 0, h0 = 0;
    saat_t y;
    if (m == NULL || s == NULL) return 0;
    memset(n, 0, sizeof(n));
    for (;; m++) {
        if (*m >= '0' && *m <= '9') {
            if (adet >= 6u) break;
            if (hane < 5u) n[adet] = n[adet] * 10u + (uint32_t)(*m - '0');
            hane++;
        } else {
            if (hane) { if (adet == 0u) h0 = hane; adet++; hane = 0; }
            if (*m == '\0' || *m == '\r' || *m == '\n' || adet >= 6u) break;
        }
    }
    if (adet < 5u) return 0;
    memset(&y, 0, sizeof(y));
    if (h0 == 4u)              { y.yil = (uint16_t)n[0]; y.ay = (uint8_t)n[1]; y.gun = (uint8_t)n[2]; }
    else if (n[2] >= 2000u)    { y.gun = (uint8_t)n[0]; y.ay = (uint8_t)n[1]; y.yil = (uint16_t)n[2]; }
    else return 0;
    if (n[1] > 12u || n[3] > 23u || n[4] > 59u || (adet >= 6u && n[5] > 59u)) return 0;
    y.saat = (uint8_t)n[3]; y.dk = (uint8_t)n[4]; y.sn = (uint8_t)((adet >= 6u) ? n[5] : 0u);
    if (!Saat_Gecerli(&y)) return 0;
    *s = y;
    return 1;
}

void Saat_Metin(const saat_t *s, char *buf, uint16_t boyut)
{
    if (buf == NULL || boyut == 0u) return;
    if (s == NULL) { buf[0] = '\0'; return; }
    (void)snprintf(buf, boyut, "%04u.%02u.%02u %02u:%02u:%02u", (unsigned)s->yil, (unsigned)s->ay,
                   (unsigned)s->gun, (unsigned)s->saat, (unsigned)s->dk, (unsigned)s->sn);
}

uint32_t Saat_FatZaman(void)
{
    saat_t s;
    if (!Saat_Oku(&s) || !Saat_Gecerli(&s)) return 0;
    return ((uint32_t)(s.yil - 1980u) << 25) | ((uint32_t)s.ay << 21) | ((uint32_t)s.gun << 16) |
           ((uint32_t)s.saat << 11) | ((uint32_t)s.dk << 5) | ((uint32_t)s.sn >> 1);
}
