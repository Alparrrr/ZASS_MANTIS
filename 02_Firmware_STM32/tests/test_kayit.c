/* ============================================================================
 * PC TESTI: bms_veri.c (UART6 cozumu, guvenlik besleme, CSV), saat.c (takvim,
 * SAAT.TXT cozumu, derleme zamani) ve Task_SDCard.c (SD kayit akisi).
 * Sahte HAL + sahte RTC + BELLEK ICI sahte FatFs; gercek kaynaklar #include edilir.
 *   derle: zig cc -DSAAT_PC_TEST -I fake_sd -I fake_hal -I ../ZASS_MANTIS_115/Core/Inc test_kayit.c
 * ========================================================================== */
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

#include "sahte_sd.h"                      /* sahte RTC + bellek ici FatFs */

/* ---------------------------------------------------------------- gercek kaynaklar */
#include "../ZASS_MANTIS_115/Core/Src/bms_veri.c"
#include "../ZASS_MANTIS_115/Core/Src/saat.c"
#include "../ZASS_MANTIS_115/Core/Src/Task_SDCard.c"

/* ---------------------------------------------------------------- sahte guvenlik + main.c */
static gvn_olcum_t g_olc;
static int         g_olc_n;
void Guvenlik_Olcum(const gvn_olcum_t *o) { g_olc = *o; g_olc_n++; }
static char g_olay[8][96];
static int  g_olay_bas, g_olay_n;
static void olay_ekle(const char *m) { snprintf(g_olay[(g_olay_bas + g_olay_n) % 8], 96, "%s", m); g_olay_n++; }
uint8_t  Guvenlik_SD_Bekleyen(void) { return (uint8_t)g_olay_n; }
uint16_t Guvenlik_SD_Satiri(char *buf, uint16_t boyut)
{
    if (!g_olay_n) return 0;
    snprintf(buf, boyut, "%s", g_olay[g_olay_bas]); g_olay_bas = (g_olay_bas + 1) % 8; g_olay_n--;
    return (uint16_t)strlen(buf);
}
static uint8_t g_maske;
uint8_t Guvenlik_Aktif_Maske(void) { return g_maske; }
static uint8_t g_mm_ok = 3; static int16_t g_mm_maks = 27, g_mm_min = 24;
uint8_t Guvenlik_Sicaklik(int16_t *maks, int16_t *min)
{
    if (maks && (g_mm_ok & 1u)) *maks = g_mm_maks;
    if (min && (g_mm_ok & 2u)) *min = g_mm_min;
    return g_mm_ok;
}

bms_data_t my_bms_can, my_bms;
RTC_HandleTypeDef hrtc;
static uint8_t g_can_hazir, g_uart_hazir;
uint8_t CAN_IsReady(void) { return g_can_hazir; }
uint8_t BMS_IsReady(void) { return g_uart_hazir; }

/* ---------------------------------------------------------------- test altyapisi */
static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("    FAIL satir %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)
#define BASLIK(s) printf("\n[%s]\n", s)

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void paket(uint8_t *b, uint16_t v100, int16_t i10, uint16_t soc, int16_t mos, int16_t bat,
                  uint16_t chg, uint16_t dchg, const uint16_t *h, int n)
{
    uint8_t *p = b + 6;
    memset(b, 0, 96);
    b[0] = 0xA5; b[1] = 0x5A; b[2] = 0x5D; b[3] = 0x82; b[4] = 0x10; b[5] = 0x00;
    put16(p + 0, v100); put16(p + 2, (uint16_t)i10); put16(p + 4, 0xBEEF); put16(p + 6, soc);
    put16(p + 8, 15); put16(p + 10, (uint16_t)mos); put16(p + 12, (uint16_t)bat); put16(p + 14, 0x0040);
    put16(p + 16, 3300); put16(p + 18, 1); put16(p + 20, chg); put16(p + 22, dchg);
    for (int k = 0; k < n && k < 33; k++) put16(p + 24 + 2 * k, h[k]);
}
static const uint16_t H8[8] = { 3290, 3305, 3300, 3298, 3301, 3299, 3297, 3302 };

/* ================================================================ 1) UART6 paketi */
static void t_uart_coz(void)
{
    uint8_t b[256];
    bms_data_t y;
    uint16_t h[33], mx, mn;

    BASLIK("K1  UART6 paketi cozumu");
    paket(b, 2633, -125, 85, 31, 27, 1, 0, H8, 8);
    memset(&y, 0, sizeof(y));
    CHECK(BMS_UART_Coz(b, 96, &y) == 1, "tam paket cozulmeli");
    CHECK(y.battery_volts > 26.329f && y.battery_volts < 26.331f && y.battery_current == -125 && y.soc == 85,
          "V/A/SOC: %.3f %d %u", (double)y.battery_volts, y.battery_current, y.soc);
    CHECK(y.mos_temp == 31 && y.batt_temp == 27 && y.warning == 0x40 && y.cell_delta == 15 && y.ave_cell_volt == 3300,
          "sicaklik/uyari/fark/ort");
    CHECK(y.charge_mos == 1 && y.discharge_mos == 0 && y.balance_status == 1, "MOS durumlari");
    CHECK(y.cell_count == 8 && !memcmp(y.cell_volts, H8, sizeof(H8)) && y.cell_volts[8] == 0, "8 hucre: n=%u", y.cell_count);

    /* cop + yanlis VP'li sahte baslik + 37'de gercek paket */
    memset(b, 0x5A, sizeof(b));
    b[3] = 0xA5; b[4] = 0x5A; b[5] = 0x5D; b[6] = 0x82; b[7] = 0x11; b[8] = 0x00;    /* VP 0x1100: degil */
    paket(b + 37, 2410, 300, 50, 20, 21, 1, 1, H8, 8);
    memset(&y, 0, sizeof(y));
    CHECK(BMS_UART_Coz(b, 200, &y) == 1 && y.battery_current == 300 && y.soc == 50, "kaymis paket bulunmali");
    CHECK(BMS_UART_Coz(b, 133, &y) == 1, "tam sigan paket (37+96 = 133) kabul");
    CHECK(BMS_UART_Coz(b, 132, &y) == 0, "bir bayt eksik (132) reddedilir");
    paket(b + 120, 2410, 0, 50, 20, 21, 1, 1, H8, 8);
    memset(b, 0, 100);
    memset(&y, 0x77, sizeof(y));
    CHECK(BMS_UART_Coz(b, 200, &y) == 0 && y.soc == 0x7777, "yarim kalan paket (120+96>200) reddedilir, b'ye dokunulmaz");
    CHECK(BMS_UART_Coz(b, 95, &y) == 0 && BMS_UART_Coz(NULL, 200, &y) == 0 && BMS_UART_Coz(b, 200, NULL) == 0, "kisa/NULL");

    paket(b, 0, 0, 0, 0, 0, 0, 0, H8, 0);
    memset(&y, 0x77, sizeof(y));
    CHECK(BMS_UART_Coz(b, 96, &y) == 0 && y.soc == 0x7777, "bos paket (0 V, hucre yok) reddedilir");

    for (int k = 0; k < 33; k++) h[k] = (uint16_t)(3200 + k);
    h[3] = 0;                                                       /* aradaki 0 mV: sutun kaymamali */
    paket(b, 5290, 0, 70, 25, 25, 1, 1, h, 16);
    CHECK(BMS_UART_Coz(b, 96, &y) == 1 && y.cell_count == 16 && y.cell_volts[3] == 0 && y.cell_volts[15] == 3215,
          "16 hucre, 4. hucre 0: n=%u", y.cell_count);
    CHECK(BMS_Hucre_MaxMin(&y, &mx, &mn) == 1 && mx == 3215 && mn == 3200, "max/min 0'i atlar: %u/%u", mx, mn);
    paket(b, 8000, 0, 70, 25, 25, 1, 1, h, 30);
    CHECK(BMS_UART_Coz(b, 96, &y) == 1 && y.cell_count == 25 && y.cell_volts[24] == 3224, "30 hucreli pakette ilk 25: n=%u", y.cell_count);
    paket(b, 8000, 0, 70, 25, 25, 1, 1, h, 25);
    CHECK(BMS_UART_Coz(b, 96, &y) == 1 && y.cell_count == 25 && y.cell_volts[24] == 3224, "25 hucreli paket: n=%u", y.cell_count);
    memset(&y, 0, sizeof(y));
    CHECK(BMS_Hucre_MaxMin(&y, &mx, &mn) == 0, "hucre yoksa max/min yok");
}

/* ================================================================ 1b) CAN kareleri */
static void can(bms_data_t *b, uint32_t id, int ext, const uint8_t *d8, uint32_t t, uint8_t *st1)
{
    uint8_t r = BMS_CAN_Coz(id, (uint8_t)ext, d8, b, t);
    if (st1) *st1 = r;
}
static void hucre_kareleri(bms_data_t *b, const uint16_t *h, int n, int kare_sayisi, uint32_t t, uint32_t dugum)
{
    for (int k = 0; k < kare_sayisi; k++) {
        uint8_t d[8] = {0};
        for (int i = 0; i < 4; i++) {
            int c = k * 4 + i;
            uint16_t v = (c < n) ? h[c] : 0;
            d[2 * i] = (uint8_t)v; d[2 * i + 1] = (uint8_t)(v >> 8);
        }
        can(b, 0x18E02800u + ((uint32_t)k << 16) + dugum, 1, d, t, NULL);
    }
}
static void t_can_coz(void)
{
    bms_data_t b;
    uint16_t h[25];
    uint8_t d[8], st1 = 9;
    BASLIK("K1b CAN karesi cozumu (25 hucre, E0..E6)");
    memset(&b, 0, sizeof(b));
    d[0] = 826 & 0xFF; d[1] = 826 >> 8; d[2] = (uint8_t)(3580 & 0xFF); d[3] = (uint8_t)(3580 >> 8); d[4] = 76; d[5] = d[6] = d[7] = 0;
    can(&b, 0x2F4, 0, d, 0, &st1);
    CHECK(st1 == 1 && b.battery_volts > 82.59f && b.battery_volts < 82.61f && b.battery_current == -420 && b.soc == 76,
          "BATT_ST1: 1 doner, 82.6 V, -42.0 A, %%76: %u %.2f %d %u", st1, (double)b.battery_volts, b.battery_current, b.soc);
    can(&b, 0x2F5, 0, d, 0, &st1);
    CHECK(st1 == 1, "cihaz adresi F5 de kabul");
    memset(d, 0, 8); d[0] = 3325 & 0xFF; d[1] = 3325 >> 8; d[3] = 3265 & 0xFF; d[4] = 3265 >> 8;
    can(&b, 0x4F4, 0, d, 0, &st1);
    CHECK(st1 == 0 && b.cell_delta == 60, "hucre farki 60 mV: %u", b.cell_delta);
    memset(d, 0, 8); d[0] = 31 + 50; can(&b, 0x5F4, 0, d, 0, NULL);
    CHECK(b.batt_temp == 31, "batarya sicakligi");
    for (int i = 0; i < 25; i++) h[i] = (uint16_t)(3300 + i);
    hucre_kareleri(&b, h, 25, 7, 100, 0xF4);
    CHECK(b.cell_count == 25 && b.cell_volts[24] == 3324 && b.cell_volts[0] == 3300, "25 hucre (E6 dahil): n=%u h25=%u", b.cell_count, b.cell_volts[24]);
    CHECK(b.ave_cell_volt == 3312, "ortalama: %u", b.ave_cell_volt);
    memset(d, 0, 8); d[3] = 0xFF; b.mos_temp = 44; can(&b, 0x18F228F4u, 1, d, 100, NULL);
    CHECK(b.mos_temp == 44, "MOS NA (0xFF) iken eski deger korunur");
    d[3] = 80; can(&b, 0x18F228F4u, 1, d, 100, NULL);
    CHECK(b.mos_temp == 30, "MOS 30 C");
    memset(d, 0, 8); d[0] = 0x34; d[1] = 0x12; d[2] = 1; can(&b, 0x18F328F5u, 1, d, 100, NULL);
    CHECK(b.warning == 0x9234, "uyari + MOS arizasi biti: %04X", b.warning);
    memset(d, 0, 8); d[0] = 0x13; can(&b, 0x18F528F4u, 1, d, 100, NULL);
    CHECK(b.charge_mos == 1 && b.discharge_mos == 1 && b.balance_status == 0 && b.chg_plug == 1, "SwSta bitleri");
    can(&b, 0x18E728F4u, 1, d, 100, NULL);
    CHECK(b.cell_count == 25 && b.cell_volts[24] == 3324, "E7 (28+) yok sayilir, tasma yok");
    memset(&b, 0, sizeof(b)); hucre_kareleri(&b, h, 24, 7, 200, 0xF4);
    CHECK(b.cell_count == 24, "24S BMS (E6 sifir): %u", b.cell_count);
    /* 25S BMS cikti, yerine 16S takildi (sadece E0..E3 gelir): 5 sn sonra eski hucreler silinir */
    memset(&b, 0, sizeof(b)); hucre_kareleri(&b, h, 25, 7, 1000, 0xF4);
    for (uint32_t t = 1200; t <= 6000; t += 200) hucre_kareleri(&b, h, 16, 4, t, 0xF5);
    CHECK(b.cell_count == 25, "5 sn dolmadan eski hucreler duruyor: %u", b.cell_count);
    for (uint32_t t = 6200; t <= 7000; t += 200) hucre_kareleri(&b, h, 16, 4, t, 0xF5);
    CHECK(b.cell_count == 16 && b.cell_volts[16] == 0 && b.cell_volts[24] == 0, "16S: eski 17-25 temizlendi: n=%u", b.cell_count);
    CHECK(b.ave_cell_volt == 3307, "ortalama 16 hucreden: %u", b.ave_cell_volt);
    can(&b, 0x123, 0, d, 7000, &st1);
    CHECK(st1 == 0 && b.cell_count == 16, "ilgisiz kare degistirmez");
}

/* ================================================================ 2) guvenlik eslemesi */
static void t_uart_guvenlik(void)
{
    uint8_t b[96];
    bms_data_t y;
    BASLIK("K2  UART6 -> guvenlik olcumu");
    paket(b, 2633, -125, 85, 31, 27, 1, 0, H8, 8);
    BMS_UART_Coz(b, 96, &y);
    g_olc_n = 0; BMS_UART_Guvenlige(&y);
    CHECK(g_olc_n == 1, "bir olcum gitmeli");
    CHECK(g_olc.paket_dV == 263 && g_olc.akim_dA == -125 && g_olc.soc == 85, "dV=%u dA=%d soc=%u", g_olc.paket_dV, g_olc.akim_dA, g_olc.soc);
    CHECK(g_olc.hucre_max_mV == 3305 && g_olc.hucre_min_mV == 3290, "max/min %u/%u", g_olc.hucre_max_mV, g_olc.hucre_min_mV);
    CHECK(g_olc.hucre_max_no == 2 && g_olc.hucre_min_no == 1, "hangi hucre: max %u. min %u.", g_olc.hucre_max_no, g_olc.hucre_min_no);
    CHECK(g_olc.sicak_max_C == 27 && g_olc.sicak_min_C == 27 && g_olc.mos_C == 31, "sicakliklar");
    CHECK(g_olc.sarj_mos == 1 && g_olc.desarj_mos == 0 && g_olc.sarj_fisi == -1, "MOS 1/0, fis bilinmiyor");
    y.batt_temp = -60; BMS_UART_Guvenlige(&y);
    CHECK(g_olc.sicak_max_C == GVN_OLC_NA_C && g_olc.sicak_min_C == GVN_OLC_NA_C, "-60 C gecersiz -> NA (kural 8)");
    y.batt_temp = 130; BMS_UART_Guvenlige(&y);
    CHECK(g_olc.sicak_max_C == GVN_OLC_NA_C, "130 C gecersiz -> NA");
    y.batt_temp = -45; BMS_UART_Guvenlige(&y);
    CHECK(g_olc.sicak_min_C == -45, "-45 C gecerli sinir");
    y.cell_count = 0; BMS_UART_Guvenlige(&y);
    CHECK(g_olc.hucre_max_mV == 0 && g_olc.hucre_min_mV == 0, "hucre yoksa max/min 0 (kural 1-3 bu kaynaktan beslenmez)");
}

/* ================================================================ 3) CSV + etkin kaynak */
static void t_csv(void)
{
    char s[400];
    bms_data_t b, k;
    uint16_t n;
    BASLIK("K3  CSV satirlari + etkin kaynak");
    n = BMS_CSV_Baslik(s, sizeof(s), 3, 0x03);
    CHECK(n == strlen(s) && !strcmp(s, "Tarih Saat,Kaynak,Paket V,Akim A,SOC %,MOS C,Maks C,Min C,Hucre Fark mV,Ort Hucre mV,"
                                       "Hucre Max mV,Hucre Min mV,Sarj MOS,Desarj MOS,Sarj Fisi,BMS Uyari,Guvenlik,"
                                       "T1 C,T2 C,Hucre Sayisi,H1 mV,H2 mV,H3 mV\n"), "baslik: %s", s);
    n = BMS_CSV_Baslik(s, sizeof(s), 1, 0x1F);
    CHECK(strstr(s, ",Guvenlik,T1 C,T2 C,T4 C,T5 C,Hucre Sayisi,") != NULL, "T3 (MOS) sutunu tekrar yazilmaz: %s", s);
    memset(&b, 0, sizeof(b));
    b.battery_volts = 26.33f; b.battery_current = -125; b.soc = 85; b.mos_temp = 31; b.batt_temp = 27;
    b.cell_delta = 15; b.ave_cell_volt = 3300; b.charge_mos = 1; b.discharge_mos = 1; b.chg_plug = 1; b.warning = 0x40;
    b.cell_volts[0] = 3290; b.cell_volts[1] = 3305; b.cell_volts[2] = 3300; b.cell_count = 3;
    b.sicak_kaynak = 1; b.sicak_maske = 0x04;                                   /* sahadaki gibi: maske sadece T3 */
    b.sicak_ham[0] = 70; b.sicak_ham[1] = 69; b.sicak_ham[2] = 69; b.sicak_ham[3] = 0xFF; b.sicak_ham[4] = 0xFF;
    b.sicak_mm_ok = 3; b.sicak_maks = 27; b.sicak_min = 19;
    CHECK(BMS_Sicak_Gecerli(&b) == 0x03, "gecerli problar T1 T2 (T3 MOS sayilmaz): %02X", BMS_Sicak_Gecerli(&b));
    n = BMS_CSV_Satir(s, sizeof(s), "2026.10.07 15:30:01", BMS_KAYNAK_CAN, &b, 0x41, 0x03);
    CHECK(n == strlen(s) && !strcmp(s, "2026.10.07 15:30:01,CAN,26.33,-12.5,85,31,27,19,15,3300,3305,3290,1,1,1,0040,K1 K7,20,19,3,3290,3305,3300\n"),
          "CAN satiri: %s", s);
    b.sicak_ham[3] = 0; b.sicak_ham[4] = 0;                                     /* bos yuvalar 0 = -50 C */
    CHECK(BMS_Sicak_Gecerli(&b) == 0x03, "-50 C (0) gecersiz: yazilmaz");
    b.sicak_ham[1] = 0xFF;                                                      /* oturumdaki T2 koptu */
    BMS_CSV_Satir(s, sizeof(s), "z", BMS_KAYNAK_CAN, &b, 0, 0x03);
    CHECK(strstr(s, ",0040,,20,-,3,") != NULL, "oturumdaki prob okunmuyor: '-': %s", s);
    b.sicak_ham[1] = 69;
    b.sicak_mm_ok = 1;
    BMS_CSV_Satir(s, sizeof(s), "z", BMS_KAYNAK_UART, &b, 0, 0x01);
    CHECK(strstr(s, ",UART,") && strstr(s, ",31,27,-,15,") && strstr(s, ",1,1,-,0040,,20,3,"), "UART: fis '-', min yok '-': %s", s);
    b.sicak_kaynak = 0; b.sicak_mm_ok = 0;
    BMS_CSV_Satir(s, sizeof(s), "z", BMS_KAYNAK_CAN, &b, 0, 0);
    CHECK(strstr(s, ",85,31,-,-,15,") && strstr(s, ",0040,,3,"), "sicaklik yok: maks/min '-', T sutunu yok: %s", s);
    b.battery_current = -5;  BMS_CSV_Satir(s, sizeof(s), "z", 1, &b, 0, 0); CHECK(strstr(s, ",-0.5,") != NULL, "-0.5 A: %s", s);
    b.battery_current = 7;   BMS_CSV_Satir(s, sizeof(s), "z", 1, &b, 0, 0); CHECK(strstr(s, ",0.7,") != NULL, "0.7 A");
    b.battery_current = 1234; BMS_CSV_Satir(s, sizeof(s), "z", 1, &b, 0, 0); CHECK(strstr(s, ",123.4,") != NULL, "123.4 A");
    b.battery_volts = 0.0f;  BMS_CSV_Satir(s, sizeof(s), "z", 1, &b, 0, 0); CHECK(strstr(s, ",CAN,0.00,") != NULL, "0 V");
    b.battery_volts = 79.999f; BMS_CSV_Satir(s, sizeof(s), "z", 1, &b, 0, 0); CHECK(strstr(s, ",CAN,80.00,") != NULL, "yuvarlama: %s", s);
    CHECK(BMS_CSV_Satir(s, 20, "2026.10.07 15:30:01", 1, &b, 0, 0) == 0 && s[0] == '\0', "kucuk tampon: 0 ve bos");
    for (int i = 0; i < 25; i++) b.cell_volts[i] = 4199; b.cell_count = 25; b.battery_current = -32768; b.warning = 0xFFFF;
    b.sicak_kaynak = 1; memset(b.sicak_ham, 5, sizeof(b.sicak_ham));            /* 4 x "-45" (en uzun) */
    b.sicak_mm_ok = 3; b.sicak_maks = -45; b.sicak_min = -45; b.mos_temp = -45;
    n = BMS_CSV_Satir(s, sizeof(s), "2026.10.07 15:30:01", 2, &b, 0xFF, 0x1B);
    CHECK(n > 0 && n < sizeof(s) && n < sizeof(s_satir) && strstr(s, "K1 K2 K3 K4 K5 K6 K7 K8,-45,-45,-45,-45,25,"),
          "25 hucre en kotu durum sigmali: n=%u", n);
    n = BMS_CSV_Baslik(s, sizeof(s), 25, 0x1B);
    CHECK(n > 0 && strstr(s, "H25 mV\n") && n < sizeof(s_satir), "25 hucre basligi SD tamponuna sigmali: n=%u / %u", n, (unsigned)sizeof(s_satir));

    memset(&my_bms_can, 0, sizeof(my_bms_can)); my_bms_can.soc = 11;
    memset(&my_bms, 0, sizeof(my_bms));         my_bms.soc = 22;
    memset(&k, 0x55, sizeof(k));
    g_can_hazir = 0; g_uart_hazir = 0;
    CHECK(BMS_Aktif_Kopya(&k) == 0 && k.soc == 0x5555, "kaynak yok: 0, kopyaya dokunulmaz");
    g_uart_hazir = 1;
    CHECK(BMS_Aktif_Kopya(&k) == 2 && k.soc == 22, "sadece UART: 2");
    g_can_hazir = 1;
    CHECK(BMS_Aktif_Kopya(&k) == 1 && k.soc == 11 && g_primask == 0, "CAN oncelikli: 1, kesmeler geri acik");
    g_can_hazir = g_uart_hazir = 0;
    CHECK(!strcmp(BMS_Kaynak_Adi(0), "YOK") && !strcmp(BMS_Kaynak_Adi(1), "CAN") && !strcmp(BMS_Kaynak_Adi(2), "UART"), "adlar");
}

/* ================================================================ 4) saat */
static void t_saat(void)
{
    saat_t s;
    char m[24];
    BASLIK("K4  saat: metin, takvim, derleme zamani");
    CHECK(Saat_Metin_Coz("2026-10-07 15:30:00", &s) && s.yil == 2026 && s.ay == 10 && s.gun == 7 && s.saat == 15 && s.dk == 30 && s.sn == 0, "Y-A-G");
    CHECK(Saat_Metin_Coz("07.10.2026 15:30:45\r\n", &s) && s.yil == 2026 && s.gun == 7 && s.sn == 45, "G.A.Y");
    CHECK(Saat_Metin_Coz("07/10/2026 9:05", &s) && s.saat == 9 && s.dk == 5 && s.sn == 0, "saniyesiz");
    CHECK(Saat_Metin_Coz("\xEF\xBB\xBF" "2026-10-07 15:30:00", &s) && s.yil == 2026, "UTF-8 BOM");
    CHECK(Saat_Metin_Coz("2024-02-29 10:00:00", &s), "artik yil 29 Subat");
    CHECK(!Saat_Metin_Coz("2026-02-29 10:00:00", &s), "2026'da 29 Subat yok");
    CHECK(!Saat_Metin_Coz("2026-13-01 10:00:00", &s) && !Saat_Metin_Coz("2026-10-07 24:00:00", &s) &&
          !Saat_Metin_Coz("2026-10-07 23:60:00", &s) && !Saat_Metin_Coz("2026-10-07", &s) &&
          !Saat_Metin_Coz("abc", &s) && !Saat_Metin_Coz("", &s) && !Saat_Metin_Coz(NULL, &s), "gecersizler");
    CHECK(!Saat_Metin_Coz("1999-12-31 23:59:59", &s) && !Saat_Metin_Coz("2100-01-01 00:00:00", &s), "2000..2099 disi");
    s.yil = 2026; s.ay = 10; s.gun = 7;  CHECK(Saat_HaftaGunu(&s) == 3, "07.10.2026 Carsamba");
    s.yil = 2024; s.ay = 2;  s.gun = 29; CHECK(Saat_HaftaGunu(&s) == 4, "29.02.2024 Persembe");
    s.yil = 2000; s.ay = 1;  s.gun = 1;  CHECK(Saat_HaftaGunu(&s) == 6, "01.01.2000 Cumartesi");
    s.yil = 2026; s.ay = 10; s.gun = 11; CHECK(Saat_HaftaGunu(&s) == 7, "11.10.2026 Pazar");

    /* derleme zamani: imza yoksa (eski 0x32F4 dahil) kurulur */
    g_bkp[1] = 0x32F4; g_rtc_set_n = 0;
    Saat_Baslat(&hrtc);
    {
        saat_t d; derleme_zamani(&d);
        CHECK(g_rtc_set_n == 1 && g_bkp[1] == SAAT_IMZA && R.y == d.yil && R.a == d.ay && R.g == d.gun && R.s == d.saat,
              "derleme zamanina kuruldu: %u.%u.%u %u", R.g, R.a, R.y, R.s);
        CHECK(Saat_Gecerli(&d) && d.yil >= 2026, "derleme zamani gecerli: %u", d.yil);
        CHECK(R.hg == Saat_HaftaGunu(&d), "hafta gunu de yazildi");
    }
    Saat_Baslat(&hrtc);
    CHECK(g_rtc_set_n == 1, "imza varken saate dokunulmaz (pil korudu)");
    s.yil = 2026; s.ay = 2; s.gun = 30; s.saat = 1; s.dk = 0; s.sn = 0;
    CHECK(Saat_Ayarla(&s) == 0 && g_rtc_set_n == 1, "gecersiz tarih yazilmaz");
    s.ay = 10; s.gun = 7; s.saat = 15; s.dk = 30; s.sn = 46;
    CHECK(Saat_Ayarla(&s) == 1 && R.g == 7 && R.sn == 46 && R.hg == 3, "ayarla");
    CHECK(Saat_FatZaman() == (((2026u - 1980u) << 25) | (10u << 21) | (7u << 16) | (15u << 11) | (30u << 5) | 23u), "FatFs zaman damgasi");
    CHECK(Saat_Oku(&s) && s.yil == 2026 && s.sn == 46 && g_primask == 0, "oku + kesmeler geri acik");
    Saat_Metin(&s, m, sizeof(m));
    CHECK(!strcmp(m, "2026.10.07 15:30:46"), "metin: %s", m);
}

/* ================================================================ 5) SD akisi */
/* gercek CAN'i taklit: kare turleri bms_veri.c oturum sayacina girer (veri my_bms_can'e degil) */
static bms_data_t g_kukla;
static int        g_can_besle = 1;
static void can_oturum(void)
{
    static const uint32_t ID[] = { 0x2F4, 0x4F4, 0x5F4, 0x18E028F4u, 0x18F228F4u, 0x18F528F4u };
    uint8_t d[8] = { 70, 0, 70, 0, 0, 0, 0, 0 };
    for (unsigned i = 0; i < sizeof(ID) / sizeof(ID[0]); i++) (void)BMS_CAN_Coz(ID[i], ID[i] > 0x7FFu, d, &g_kukla, g_ms);
}
static void sd_sim_s(int n)
{
    for (int i = 0; i < n; i++) {
        g_ms += 1000u; rtc_saniye();
        if (g_can_hazir && g_can_besle) can_oturum();
        SD_Log_Process();
    }
}
static void can_8_hucre(void)
{
    memset(&my_bms_can, 0, sizeof(my_bms_can));
    my_bms_can.battery_volts = 26.4f; my_bms_can.battery_current = -52; my_bms_can.soc = 60;
    my_bms_can.mos_temp = 30; my_bms_can.batt_temp = 26; my_bms_can.cell_delta = 12; my_bms_can.ave_cell_volt = 3300;
    my_bms_can.charge_mos = 1; my_bms_can.discharge_mos = 1; my_bms_can.chg_plug = 0;
    memcpy(my_bms_can.cell_volts, H8, sizeof(H8)); my_bms_can.cell_count = 8;
}

static void t_sd(void)
{
    const char *V1 = "2026/10_2026/07_10_2026_LOG.CSV", *V2 = "2026/10_2026/08_10_2026_LOG.CSV";
    const char *OL = "2026/10_2026/OLAYLAR_10_2026.CSV";
    BASLIK("K5  SD kayit akisi (sahte kart)");
    s_kaynak = SAAT_KAYNAK_LSE;                                   /* saat.c static'i */
    R.y = 2026; R.a = 6; R.g = 19; R.s = 15; R.d = 10; R.sn = 0;
    g_ms = 0; g_kart = 0; g_mount_n = 0; g_init_ihlal = 0;
    can_8_hucre(); g_can_hazir = 1;
    /* onceki calisma HardFault ile yeniden baslamis gibi (stm32g0xx_it.c yedek yazmaclara yazar) */
    g_bkp[2] = 0xFA170001u; g_bkp[3] = 0x08001234u; g_bkp[4] = 0x08005679u;
    fake_rcc.CSR = RCC_CSR_SFTRSTF | RCC_CSR_PINRSTF;

    sd_sim_s(15);
    CHECK(sd_hata_pc == 0x08001234u && sd_hata_lr == 0x08005679u && g_bkp[2] == 0u && (fake_rcc.CSR & RCC_CSR_RMVF),
          "acilis: HardFault kaydi okundu ve silindi, reset bayraklari temizlendi");
    CHECK(g_mount_n == 0 && sd_durum == 0 && !sd_kart_var && sd_cd_ham == 1 && g_cd_init == 1,
          "kart yok (CD = 1): baglanma denenmez: mount=%d cd=%u init=%d", g_mount_n, sd_cd_ham, g_cd_init);

    dosya_koy("SAAT.TXT", "07.10.2026 23:59:30\r\n");
    g_kart = 1;
    sd_sim_s(1);                                                  /* CD = 0 ama kart henuz oturmadi */
    CHECK(g_mount_n == 0 && sd_durum == 0, "takildi: 1. okuma, oturma beklemesi (baglanma yok)");
    sd_sim_s(1);                                                  /* 2. okuma: baglanir */
    CHECK(g_init_ihlal == 0, "her denemeden once disk.is_initialized sifirlanmali");
    CHECK(g_mount_n == 1 && sd_kart_var && sd_cd_ham == 0 && s_takili && icerik("SAAT.TXT") == NULL && icerik("SAAT_OK.TXT") != NULL, "kart takildi, SAAT.TXT uygulandi");
    CHECK(R.g == 7 && R.a == 10 && R.y == 2026 && R.s == 23, "RTC SAAT.TXT'ye kuruldu: %u.%u.%u %u:%u:%u", R.g, R.a, R.y, R.s, R.d, R.sn);
    CHECK(satir_say(OL, "2026.10.07 23:59:30,SISTEM,Cihaz açıldı: yazılım hatası (HardFault) - cihaz kendini "
                        "yeniden başlattı / hata adresi PC=0x08001234 LR=0x08005679") == 1,
          "acilis satiri OLAY'da (HardFault PC/LR): %s", icerik(OL) ? icerik(OL) : "(yok)");
    {
        uint32_t c0 = sd_acilis_csr, h0 = s_hata_imza;
        s_hata_imza = 0;
        sd_acilis_csr = RCC_CSR_PWRRSTF | RCC_CSR_PINRSTF;
        CHECK(!strcmp(acilis_nedeni(), "güç açıldı ya da besleme düştü"), "neden: guc (%s)", acilis_nedeni());
        sd_acilis_csr = RCC_CSR_PINRSTF;
        CHECK(!strcmp(acilis_nedeni(), "reset düğmesi / programlayıcı"), "neden: pin");
        sd_acilis_csr = RCC_CSR_SFTRSTF | RCC_CSR_PINRSTF;
        CHECK(!strcmp(acilis_nedeni(), "yazılım reseti"), "neden: yazilim");
        sd_acilis_csr = c0; s_hata_imza = h0;
    }
    CHECK(satir_say(OL, "\xEF\xBB\xBFTarih Saat,Kod,Olay\n") == 1 &&
          satir_say(OL, "2026.10.07 23:59:30,SAAT,Saat SAAT.TXT ile ayarlandı: önceki saat ") == 1,
          "olay dosyasinda baslik + saat ayari:\n%s", icerik(OL) ? icerik(OL) : "(yok)");
    CHECK(icerik(OL) && strstr(icerik(OL), "önceki saat 2026.06.19 15:10:1") != NULL, "eski saat de yazili");
    CHECK(satir_say(V1, "Saat kaynagi LSE, BMS kaynagi CAN, 8 hucre") == 1 && satir_say(V1, "Tarih Saat,Kaynak,") == 1,
          "veri dosyasi: oturum + baslik:\n%s", icerik(V1) ? icerik(V1) : "(yok)");
    CHECK(strstr(icerik(V1), ",H8 mV\n") && !strstr(icerik(V1), "H9 mV"), "8 hucre sutunu");
    sd_sim_s(5);                                                  /* 23:59:35 */
    CHECK(satir_say(V1, "2026.10.07 23:59:") == 6, "her saniye bir satir: %d", satir_say(V1, "2026.10.07 23:59:"));
    CHECK(!strcmp(son_satir(V1) + 20, "CAN,26.40,-5.2,60,30,27,24,12,3300,3305,3290,1,1,0,0000,,8,3290,3305,3300,3298,3301,3299,3297,3302"),
          "satir icerigi: %s", son_satir(V1));

    sd_sim_s(50);                                                 /* gece yarisi -> 00:00:25 */
    CHECK(icerik(V2) != NULL && satir_say(V2, "Tarih Saat,Kaynak,") == 1 && satir_say(V2, "2026.10.08 00:00:") >= 20,
          "gun degisti: yeni dosya + baslik");
    CHECK(satir_say(V1, "2026.10.08") == 0, "eski gunun dosyasina yeni gun yazilmaz");
    CHECK(g_sync_n >= 3, "10 sn'de bir f_sync: %d", g_sync_n);

    my_bms_can.cell_volts[15] = 3311; my_bms_can.cell_count = 16;
    sd_sim_s(2);
    CHECK(satir_say(V2, "Saat kaynagi LSE, BMS kaynagi CAN, 16 hucre") == 1 && strstr(icerik(V2), ",H16 mV\n"), "hucre sayisi degisti: yeni baslik");

    memset(&my_bms, 0, sizeof(my_bms)); my_bms.battery_volts = 26.1f; my_bms.soc = 59;
    memcpy(my_bms.cell_volts, H8, sizeof(H8)); my_bms.cell_count = 8;
    g_can_hazir = 0; g_uart_hazir = 1;
    sd_sim_s(2);
    CHECK(satir_say(V2, "Saat kaynagi LSE, BMS kaynagi UART, 8 hucre") == 1 && strstr(son_satir(V2), ",UART,26.10,") &&
          strstr(son_satir(V2), ",-,"), "kaynak UART'a gecti: yeni baslik, fis '-': %s", son_satir(V2));

    g_uart_hazir = 0;
    sd_sim_s(5);
    CHECK(satir_say(V2, "2026.10.08") > 0 && strstr(son_satir(V2), ",YOK") && satir_say(V2, "2026.10.08 00:0") > 0, "veri kesildi");
    {
        int yok = 0; const char *p = icerik(V2);
        while ((p = strstr(p, ",YOK\n")) != NULL) { yok++; p++; }
        CHECK(yok == 1, "YOK isareti bir kez: %d", yok);
    }

    olay_ekle("2026.10.08 00:01:00,7,BASLADI,59,0,0,0\n");
    olay_ekle("2026.10.08 00:01:20,7,BITTI,59,0,0,20\n");
    sd_sim_s(1);
    CHECK(satir_say(OL, "2026.10.08 00:01:00,7,BASLADI") == 1 && satir_say(OL, "2026.10.08 00:01:20,7,BITTI") == 1 &&
          Guvenlik_SD_Bekleyen() == 0, "olaylar yazildi");
    CHECK(g_acik_max <= 2, "_FS_LOCK 2 asilmadi: %d", g_acik_max);

    /* olay dosyasi acilamazsa olay kuyrukta kalir */
    g_olay_ac_hata = 1;
    olay_ekle("2026.10.08 00:02:00,4,BASLADI,59,57,25,0\n");
    sd_sim_s(1);
    CHECK(Guvenlik_SD_Bekleyen() == 1 && sd_durum == 3, "olay dosyasi acilamadi: olay kuyrukta, kart birakildi");
    g_olay_ac_hata = 0;
    g_can_hazir = 1; can_8_hucre();
    sd_sim_s(11);
    CHECK(Guvenlik_SD_Bekleyen() == 0 && satir_say(OL, "2026.10.08 00:02:00,4,BASLADI") == 1, "kurtarinca olay yazildi");

    /* kart cikti (CD): karta yazmadan birakilir, takilinca HEMEN baglanir */
    {
        int m0 = g_mount_n, s0 = satir_say(V2, "Tarih Saat,Kaynak,");
        uint32_t h0 = sd_hata_sayisi;
        g_kart = 0;
        sd_sim_s(1);
        CHECK(sd_durum == 0 && !s_takili && !s_veri_acik && g_acik == 0 && sd_hata_sayisi == h0,
              "kart cikti: hata sayilmadan birakildi (acik=%d)", g_acik);
        olay_ekle("2026.10.08 00:03:00,4,BITTI,59,57,25,60\n");
        sd_sim_s(25);
        CHECK(g_mount_n == m0 && Guvenlik_SD_Bekleyen() == 1, "kart yokken baglanma denenmez, olay kuyrukta: %d", g_mount_n - m0);
        g_kart = 1;
        sd_sim_s(2);
        CHECK(s_takili && sd_durum == 2 && g_mount_n == m0 + 1 && satir_say(V2, "Tarih Saat,Kaynak,") == s0 + 1,
              "kart geri: 2 sn'de (oturunca) baglandi, yeni oturum basligi");
        CHECK(Guvenlik_SD_Bekleyen() == 0 && satir_say(OL, "2026.10.08 00:03:00,4,BITTI") == 1, "bekleyen olay yazildi");
    }
    /* CD 'takili' derken yazma hatasi (kart bozuk / CD gecikmeli): eski yol, 10 sn'de bir dene */
    {
        int m0 = g_mount_n;
        g_yaz_hata = 1;
        sd_sim_s(1);
        CHECK(sd_durum == 3 && !s_takili, "yazma hatasi: birakildi");
        sd_sim_s(25);
        CHECK(g_mount_n - m0 >= 2 && g_init_ihlal == 0, "10 sn'de bir yeniden deneme: %d", g_mount_n - m0);
        g_yaz_hata = 0;
        sd_sim_s(11);
        CHECK(s_takili && sd_durum == 2, "hata gecti: yeniden baglandi");
        g_yaz_hata = 1; sd_sim_s(1);                              /* hata -> 10 sn bekleme basladi */
        g_yaz_hata = 0; g_kart = 0; sd_sim_s(1);                  /* bozuk kart cikarildi ...      */
        g_kart = 1; sd_sim_s(2);                                  /* ... yenisi takildi (oturma)    */
        CHECK(s_takili && sd_durum == 2, "hatadan sonra kart degisti: 10 sn beklemeden baglandi");
    }
    /* CAN oturumu: bazi kare turleri hic gelmiyorsa 5 sn sonra yine de kaydedilir */
    {
        int r0;
        g_can_besle = 0; sd_sim_s(5);                             /* CAN sustu -> oturum sifirlanir */
        r0 = (int)sd_satir_sayisi;
        for (int i = 0; i < 4; i++) {                             /* sadece BATT_ST1 geliyor */
            uint8_t d[8] = { 0 };
            g_ms += 1000u; rtc_saniye(); (void)BMS_CAN_Coz(0x2F4, 0, d, &g_kukla, g_ms); SD_Log_Process();
        }
        CHECK((int)sd_satir_sayisi == r0 && !BMS_CAN_Tam(g_ms), "eksik kareyle 4 sn: satir yok");
        for (int i = 0; i < 3; i++) {
            uint8_t d[8] = { 0 };
            g_ms += 1000u; rtc_saniye(); (void)BMS_CAN_Coz(0x2F4, 0, d, &g_kukla, g_ms); SD_Log_Process();
        }
        CHECK((int)sd_satir_sayisi > r0 && BMS_CAN_Tam(g_ms), "5 sn zaman asimi: kayit basladi");
        g_can_besle = 1;
    }
    /* CD teli bagli degil (pull-up -> 1): kart takili olsa da kayit YAPILMAZ -> SD_CD_KULLAN 0 */
    {
        g_cd_kopuk = 1;
        sd_sim_s(2);
        CHECK(!s_takili && sd_cd_ham == 1 && sd_durum == 0, "CD kopuk: kart yok sayilir (sd_cd_ham=1)");
        g_cd_kopuk = 0;
        sd_sim_s(2);
        CHECK(s_takili, "CD geri: baglandi");
    }
    CHECK(g_init_ihlal == 0 && g_acik_max <= 2 && g_primask == 0, "yan hatalar");
    CHECK(sd_satir_sayisi > 50, "satir sayaci: %u", (unsigned)sd_satir_sayisi);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);                         /* cokerse son satir gorunsun */
    printf("bms_veri.c + saat.c + Task_SDCard.c PC testi\n");
    t_uart_coz(); t_can_coz(); t_uart_guvenlik(); t_csv(); t_saat(); t_sd();
    printf("\n%s: %d kontrol, %d basarisiz\n", g_fail ? "BASARISIZ" : "TUM TESTLER GECTI", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
