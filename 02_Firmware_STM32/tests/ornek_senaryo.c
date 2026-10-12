/* ============================================================================
 * ORNEK SENARYO: gercek firmware modulleri (guvenlik.c, bms_veri.c, saat.c,
 * Task_SDCard.c) PC'de, sahte donanimla calisir; SD kartin icerigi ornek_sd/
 * klasorune cikarilir (Excel'de acilabilir), flash olay kaydi ornek_sd/flash_olay_kaydi.bin.
 *
 *   09:00:00  CAN'a 25S BMS (LFP ~82 V, -42 A desarj)  -- 09:00:20..35 batarya 57 C (kural 4)
 *   09:02:00  CAN kablosu cikti (BMS degistiriliyor)    -- 5 sn sonra kural 7 (haberlesme)
 *   09:02:40  UART'a 8S BMS (~27 V, +15 A sarj)          -- kural 7 kalkar
 *   09:04:00  bitis
 *
 *   derle: zig cc -DSAAT_PC_TEST -I fake_sd -I fake_hal -I ../ZASS_MANTIS_115/Core/Inc ornek_senaryo.c
 * ========================================================================== */
#define GVN_PORT_OVERRIDE
#include "stm32g0xx_hal.h"
#include "ff_gen_drv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- zaman / kesme */
static uint32_t g_ms, g_primask;
uint32_t HAL_GetTick(void) { return g_ms; }
uint32_t __get_PRIMASK(void) { return g_primask; }
void     __set_PRIMASK(uint32_t v) { g_primask = v; }
void     __disable_irq(void) { g_primask = 1; }

#include "sahte_sd.h"                                  /* sahte RTC + bellek ici SD kart */

/* ---------------------------------------------------------------- sahte flash (olay kaydi) */
static uint8_t g_flash[128 * 1024];
static int     g_fl_acik;
#define GVN_FW_SON()           (FLASH_BASE + 118u * 1024u)    /* gercek derlemedeki gibi ~118 KB */
#define GVN_FLASH_KB()         128u
#define GVN_FLASH_OKU(a, d, n) memcpy((d), &g_flash[(uint32_t)(a) - FLASH_BASE], (n))
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { g_fl_acik = 1; return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void)   { g_fl_acik = 0; return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t adr, uint64_t data)
{
    uint32_t off = adr - FLASH_BASE;
    (void)type;
    if (!g_fl_acik || (adr & 7u) || off + 8u > sizeof(g_flash)) return HAL_ERROR;
    memcpy(&g_flash[off], &data, 8);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *hata)
{
    (void)hata;
    memset(&g_flash[e->Page * FLASH_PAGE_SIZE], 0xFF, FLASH_PAGE_SIZE);
    return HAL_OK;
}

/* ---------------------------------------------------------------- sahte DWIN + buzzer */
static int g_serit = -1, g_popup = -1, g_buz;
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *p, uint16_t n, uint32_t to)
{
    (void)h; (void)to;
    if (n == 8 && p[0] == 0x5A && p[3] == 0x82) {
        uint16_t vp = (uint16_t)((p[4] << 8) | p[5]), v = (uint16_t)((p[6] << 8) | p[7]);
        if (vp == 0x1402) g_serit = v;
        if (vp == 0x1404) g_popup = v;
    }
    return HAL_OK;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState s) { (void)port; (void)pin; g_buz = (int)s; }

/* ---------------------------------------------------------------- gercek firmware kaynaklari */
#include "../ZASS_MANTIS_115/Core/Src/guvenlik.c"
#include "../ZASS_MANTIS_115/Core/Src/bms_veri.c"
#include "../ZASS_MANTIS_115/Core/Src/saat.c"
#include "../ZASS_MANTIS_115/Core/Src/Task_SDCard.c"

/* ---------------------------------------------------------------- main.c'deki baglanti (aynisi) */
bms_data_t my_bms_can, my_bms;
RTC_HandleTypeDef hrtc;
static UART_HandleTypeDef g_dwin;
static uint8_t  can_data_ready, bms_data_ready;
static uint32_t can_last_rx_ms, bms_last_rx_ms;
uint8_t CAN_IsReady(void) { return can_data_ready && (HAL_GetTick() - can_last_rx_ms) <= 3000u; }
uint8_t BMS_IsReady(void) { return bms_data_ready && (HAL_GetTick() - bms_last_rx_ms) <= 3000u; }

/* ReceiveFDCANMessage -> CAN_Mesajini_Coz + Guvenlik_Kare */
static void can_kare(uint32_t id, uint8_t ext, const uint8_t *d)
{
    if (BMS_CAN_Coz(id, ext, d, &my_bms_can, HAL_GetTick())) { can_data_ready = 1; can_last_rx_ms = HAL_GetTick(); }
    Guvenlik_Kare(id, ext, d);
}
/* HAL_UARTEx_RxEventCallback (USART6) */
static void uart_paket(const uint8_t *b, uint16_t n)
{
    if (BMS_UART_Coz(b, n, &my_bms)) { bms_data_ready = 1; bms_last_rx_ms = HAL_GetTick(); BMS_UART_Guvenlige(&my_bms); }
}

/* ---------------------------------------------------------------- BMS 1: CAN, 25S */
static void le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void be16w(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static void bms_can_25s(uint32_t t)
{
    uint16_t h[25], mx = 0, mn = 0xFFFF;
    uint32_t sum = 0;
    uint8_t d[8];
    int i, k;
    int sicak = (t >= 20000 && t < 35000) ? 57 : 31;
    uint16_t dusus = (uint16_t)(t / 10000);                      /* yuk altinda yavas dusus */
    for (i = 0; i < 25; i++) {
        h[i] = (uint16_t)(3302 - dusus - (i % 5) * 3 + ((i * 7) % 4));
        if (i == 12) h[i] = (uint16_t)(3268 - dusus);            /* 13. hucre biraz zayif */
        sum += h[i];
        if (h[i] > mx) mx = h[i];
        if (h[i] < mn) mn = h[i];
    }
    if (t % 20 == 0) {                                           /* BATT_ST1, 20 ms */
        int16_t akim = (int16_t)(-420 + (int16_t)((t / 700) % 9) - 4);
        memset(d, 0, 8);
        le16(d, (uint16_t)(sum / 100u));
        le16(d + 2, (uint16_t)(akim + 4000));
        d[4] = (uint8_t)(76 - t / 60000);
        can_kare(0x2F4, 0, d);
    }
    if (t % 100 == 0) {                                          /* hucre max/min, sicaklik */
        memset(d, 0, 8); le16(d, mx); d[2] = 1; le16(d + 3, mn); d[5] = 13;
        can_kare(0x4F4, 0, d);
        memset(d, 0, 8); d[0] = (uint8_t)(sicak + 50); d[1] = 1; d[2] = (uint8_t)(sicak - 2 + 50); d[3] = 2;
        can_kare(0x5F4, 0, d);
    }
    if (t % 200 == 0) {                                          /* hucreler E0..E6 */
        for (k = 0; k < 7; k++) {
            memset(d, 0, 8);
            for (i = 0; i < 4; i++) if (k * 4 + i < 25) le16(d + 2 * i, h[k * 4 + i]);
            can_kare(0x18E028F4u + ((uint32_t)k << 16), 1, d);
        }
    }
    if (t % 500 == 0) {                                          /* ALL_TEMP, ERR, SwSta */
        memset(d, 0xFF, 8); d[0] = 0x07; d[1] = (uint8_t)(sicak + 50); d[2] = (uint8_t)(sicak - 2 + 50); d[3] = 36 + 50;
        can_kare(0x18F228F4u, 1, d);
        memset(d, 0, 8); can_kare(0x18F328F4u, 1, d);
        memset(d, 0, 8); d[0] = 0x03; can_kare(0x18F528F4u, 1, d);
    }
}

/* ---------------------------------------------------------------- BMS 2: UART, 8S */
static void bms_uart_8s(uint32_t t)
{
    static const uint16_t H[8] = { 3428, 3433, 3421, 3440, 3436, 3425, 3431, 3438 };
    uint8_t b[96], *p = b + 6;
    uint32_t sum = 0;
    uint16_t art = (uint16_t)((t - 160000) / 8000);              /* sarjda yavas yukselis */
    int i;
    memset(b, 0, sizeof(b));
    b[0] = 0xA5; b[1] = 0x5A; b[2] = 0x5D; b[3] = 0x82; b[4] = 0x10; b[5] = 0x00;
    for (i = 0; i < 8; i++) { be16w(p + 24 + 2 * i, (uint16_t)(H[i] + art)); sum += H[i] + art; }
    be16w(p + 0, (uint16_t)(sum / 10u));                         /* V x100 */
    be16w(p + 2, (uint16_t)(152 - (int)((t / 900) % 5)));        /* +15.2 A civari */
    be16w(p + 6, (uint16_t)(54 + (t - 160000) / 60000));
    be16w(p + 8, 19); be16w(p + 10, 29); be16w(p + 12, 26); be16w(p + 14, 0);
    be16w(p + 16, (uint16_t)(sum / 8u)); be16w(p + 18, 0); be16w(p + 20, 1); be16w(p + 22, 1);
    uart_paket(b, sizeof(b));
}

/* ---------------------------------------------------------------- ekran zaman cizelgesi */
static const char *serit_adi(int s)
{
    static const char *A[] = { "bos", "\"Batarya durumu normal\"", "\"BATARYA SICAKLIGI YUKSEK\"", "\"SENSOR HATASI\"",
                               "\"Uyari! Direnc\"", "canli SOC/V/A/C cubugu", "\"BMS verisi alinamiyor\"" };
    return (s >= 0 && s <= 6) ? A[s] : "?";
}
static const char *popup_adi(int p)
{
    static const char *A[] = { "KOD 1 Sarji durdurun", "KOD 2 Kullanimi durdurun", "KOD 5 Sicaklik cok yuksek",
                               "KOD 6 Sarj etmeyin", "KOD 7 Haberlesme hatasi" };
    return (p >= 1 && p <= 10) ? A[(p - 1) / 2] : "yok";
}

int main(void)
{
    const uint32_t SON = 240000u;
    int son_serit = -2, son_kod = -2, n;
    char z[24];
    saat_t s;
    FILE *f;

    R.y = 2026; R.a = 10; R.g = 8; R.s = 9; R.d = 0; R.sn = 0; R.hg = 4;
    g_bkp[1] = SAAT_IMZA;                                        /* saat onceden kurulu (pil) */
    s_kaynak = SAAT_KAYNAK_LSE;
    g_kart = 1;                                                  /* SD kart takili */
    memset(g_flash, 0xFF, sizeof(g_flash));
    Saat_Baslat(&hrtc);
    Guvenlik_Init(&g_dwin, &hrtc);

    printf("EKRAN ZAMAN CIZELGESI (gercek guvenlik modulunun DWIN'e yazdiklari)\n");
    for (uint32_t t = 20; t <= SON; t += 20) {
        g_ms = t;
        if (t % 1000 == 0) rtc_saniye();
        if (t < 120000u) bms_can_25s(t);
        else if (t >= 160000u && t % 400 == 0) bms_uart_8s(t);
        if (t % 100 == 0) {
            int kod;
            Guvenlik_Task();
            kod = (g_popup > 0) ? (g_popup - 1) / 2 : -1;
            if (g_serit != son_serit || kod != son_kod) {
                Saat_Oku(&s); Saat_Metin(&s, z, sizeof(z));
                printf("  %s  pop-up: %-26s serit: %s\n", z + 11, popup_adi(g_popup), serit_adi(g_serit));
                son_serit = g_serit; son_kod = kod;
            }
        }
        if (t % 1000 == 0) SD_Log_Process();
    }

    printf("\nSD KART (sahte) -> ornek_sd/\n");
    for (int i = 0; i < FMAX; i++) if (g_f[i].var) {
        int satir = 0;
        for (uint32_t k = 0; k < g_f[i].boy; k++) if (g_f[i].veri[k] == '\n') satir++;
        printf("  %-36s %6u bayt  %4d satir\n", g_f[i].ad, (unsigned)g_f[i].boy, satir);
    }
    n = sahte_sd_disa_aktar("ornek_sd");
    f = fopen("ornek_sd/flash_olay_kaydi.bin", "wb");
    if (f) { fwrite(&g_flash[0x1F000], 1, 0x1000, f); fclose(f); }
    printf("  %d dosya + flash_olay_kaydi.bin yazildi. sd_satir_sayisi=%u, sd_hata=%u\n", n,
           (unsigned)sd_satir_sayisi, (unsigned)sd_hata_sayisi);

    /* ---- senaryonun beklenen sonuclari (regresyon) ---- */
    {
        const char *L = "2026/10_2026/08_10_2026_LOG.CSV", *O = "2026/10_2026/OLAYLAR_10_2026.CSV";
        int hata = 0;
#define BEKLE(k, ...) do { if (!(k)) { hata++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
        BEKLE(satir_say(L, "Saat kaynagi LSE, BMS kaynagi CAN, 25 hucre") == 1, "CAN 25S oturum satiri");
        BEKLE(satir_say(L, "Saat kaynagi LSE, BMS kaynagi UART, 8 hucre") == 1, "UART 8S oturum satiri");
        BEKLE(satir_say(L, "Tarih Saat,") == 2, "iki baslik");
        BEKLE(satir_say(L, "2026.10.08 09:00:01,") == 0, "acilista eksik veriyle satir yok");
        BEKLE(strstr(icerik(L), ",T3 C") == NULL && strstr(icerik(L), ",T4 C") == NULL && strstr(icerik(L), ",-50") == NULL,
              "bos yuva / MOS tekrar sutunu yok");
        BEKLE(strstr(icerik(L), ",H25 mV\n") != NULL && strstr(icerik(L), ",H8 mV\n") != NULL, "25 ve 8 hucre sutunlari");
        BEKLE(satir_say(L, "2026.10.08 09:02:03,YOK") == 1 && strstr(strstr(icerik(L), ",YOK\n") + 1, ",YOK\n") == NULL, "tek YOK satiri");
        /* CAN 09:00:02..09:02:02 = 121 (acilista tum kareler gelene kadar yazilmaz; kablo cikinca 3 sn 'taze'),
         * YOK = 1, UART 09:02:40..09:04:00 = 81 */
        BEKLE(satir_say(L, "2026.10.08 09:0") == 121 + 1 + 81, "satir sayisi: %d", satir_say(L, "2026.10.08 09:0"));
        BEKLE(satir_say(L, "2026.10.08 09:00:22,CAN,") == 1 && strstr(icerik(L), "09:00:22,CAN,82.30,-42.0,76,36,57,55,37,3294,3303,3266,1,1,0,0000,K4,57,55,25,") != NULL,
              "K4 aninda CAN satiri");
        BEKLE(strstr(icerik(L), "09:02:40,UART,27.45,15.0,54,29,26,26,19,3431,3440,3421,1,1,-,0000,K7,26,8,3428,") != NULL, "ilk UART satiri");
        /* her olay TEK satir: ne oldu, ne zaman (bitti / sure yok) */
        BEKLE(satir_say(O, "2026.10.08 09:00:22,K4,Batarya sıcaklığı yüksek: 57 °C (sınır 55 °C)") == 1, "K4 olayi");
        BEKLE(satir_say(O, "2026.10.08 09:02:05,K7,BMS haberleşme hatası: 5 sn'den uzun süre veri gelmedi") == 1, "K7 olayi");
        BEKLE(strstr(icerik(O), "itti") == NULL && strstr(icerik(O), "evam") == NULL, "bitti / devam satiri yok");
        BEKLE(sd_hata_sayisi == 0 && g_acik_max <= 2 && g_primask == 0, "yan hatalar");
        printf("\n%s: %d senaryo kontrolu basarisiz\n", hata ? "BASARISIZ" : "SENARYO GECTI", hata);
        return hata ? 1 : 0;
    }
}
