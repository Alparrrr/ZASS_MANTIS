/* ============================================================================
 * guvenlik.c  PC TESTI  -  gercek C kodunu, sahte HAL ile calistirir.
 * Gercek kare periyotlari simule edilir (ST1 20 ms, CELL_VOLT 100 ms,
 * CELL_TEMP / ALL_TEMP / SwSta 500 ms) ve modul 100 ms'de bir cagrilir.
 *
 *   derle+calistir:  test/calistir.bat   (zig cc kullanir)
 * ========================================================================== */
#define GVN_PORT_OVERRIDE
#include "stm32g0xx_hal.h"
#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- sahte HW */
static uint32_t g_clock;
static uint8_t  g_flash[128 * 1024];
static uint32_t g_fw_son = FLASH_BASE + 79u * 1024u;
GPIO_TypeDef fake_gpioa, fake_gpiob;

#define GVN_FW_SON()           (g_fw_son)
#define GVN_FLASH_KB()         128u
#define GVN_FLASH_OKU(a, d, n) memcpy((d), &g_flash[(uint32_t)(a) - FLASH_BASE], (n))

#include "../ZASS_MANTIS_115/Core/Src/guvenlik.c"          /* test, modulun static'lerine de bakabilsin */

/* ---- HAL taklidi ---- */
static UART_HandleTypeDef g_uart;
static RTC_HandleTypeDef  g_rtc;
static int      g_page = 100, g_page_cmds, g_bad_frames;
static uint32_t g_page_t[256];
static struct { uint16_t vp; int val; int writes; } g_vp[] = {
    { GVN_VP_SERIT, -1, 0 }, { GVN_VP_POPUP, -1, 0 },
    { GVN_SP_MINI_SOC + GVN_MINI_RENK_OFS, -1, 0 }, { GVN_SP_MINI_V + GVN_MINI_RENK_OFS, -1, 0 },
    { GVN_SP_MINI_A + GVN_MINI_RENK_OFS, -1, 0 },   { GVN_SP_MINI_T + GVN_MINI_RENK_OFS, -1, 0 },
    { GVN_SP_MINI_SOC + GVN_MINI_Y_OFS, -1, 0 },   { GVN_SP_MINI_V + GVN_MINI_Y_OFS, -1, 0 },
    { GVN_SP_MINI_A + GVN_MINI_Y_OFS, -1, 0 },     { GVN_SP_MINI_T + GVN_MINI_Y_OFS, -1, 0 } };
/* bilgi cubugu rakamlarinin 4'u de ayni renkte mi? Konumla gizlemede Y de renge uygun olmali:
 * gorunurken GVN_MINI_Y, gizliyken ekran disi. */
static int mini_hepsi(int renk)
{
    if (!(g_vp[2].val == renk && g_vp[3].val == renk && g_vp[4].val == renk && g_vp[5].val == renk)) return 0;
#if GVN_MINI_KONUMLA_GIZLE
    int y = (renk == GVN_MINI_ACIK) ? GVN_MINI_Y : GVN_MINI_Y_GIZLI;
    if (!(g_vp[6].val == y && g_vp[7].val == y && g_vp[8].val == y && g_vp[9].val == y)) return 0;
#endif
    return 1;
}
#define VP_SERIT  (g_vp[0].val)
#define VP_POPUP  (g_vp[1].val)
/* ekrandaki durumu eski "sayfa" diliyle ozetle: 102 = pop-up acik, 101 = seritte uyari, 100 = digerleri */
static int sayfa(void)
{
    if (VP_POPUP > 0) return 102;
    if (VP_SERIT == GVN_SERIT_K4 || VP_SERIT == GVN_SERIT_K8) return 101;
    return 100;
}
static const uint8_t T_SIRA[5] = { 1, 2, 5, 6, 7 };
static int kod(void)
{
    if (VP_POPUP > 0 && VP_POPUP <= 10) return T_SIRA[(VP_POPUP - 1) / 2];
    if (VP_SERIT == GVN_SERIT_K4) return 4;
    if (VP_SERIT == GVN_SERIT_K8) return 8;
    return 0;
}

static int g_buz_a, g_buz_b, g_buz_mismatch;
static long g_buz_on, g_buz_n;
static int g_fl_unlocked, g_fl_viol, g_fl_progs, g_fl_erases, g_inject_prog_fail, g_inject_prog_silent;
static uint32_t g_primask;

uint32_t HAL_GetTick(void) { return g_clock; }
uint32_t __get_PRIMASK(void) { return g_primask; }
void     __set_PRIMASK(uint32_t v) { g_primask = v; }
void     __disable_irq(void) { g_primask = 1; }

static void (*g_tx_hook)(uint32_t, const uint8_t *, uint16_t);   /* uart_izle.c: giden baytlari dok */
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, uint8_t *p, uint16_t n, uint32_t to)
{
    (void)h; (void)to;
    if (g_tx_hook) g_tx_hook(g_clock, p, n);
    if (n < 8 || p[0] != 0x5A || p[1] != 0xA5 || p[2] != n - 3 || p[3] != 0x82) { g_bad_frames++; return HAL_ERROR; }
    uint16_t adr = (uint16_t)((p[4] << 8) | p[5]);
    if (adr == 0x0084) {
        if (n != 10 || p[6] != 0x5A || p[7] != 0x01) { g_bad_frames++; return HAL_ERROR; }
        g_page = (p[8] << 8) | p[9];
        if (g_page_cmds < 256) g_page_t[g_page_cmds] = g_clock;
        g_page_cmds++;
        return HAL_OK;
    }
    if (n != 8) { g_bad_frames++; return HAL_ERROR; }
    for (unsigned i = 0; i < sizeof(g_vp) / sizeof(g_vp[0]); i++)
        if (g_vp[i].vp == adr) { g_vp[i].val = (p[6] << 8) | p[7]; g_vp[i].writes++; return HAL_OK; }
    g_bad_frames++;                                  /* tanimsiz VP'ye yazma */
    return HAL_ERROR;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState s)
{
    if (port == GPIOA && pin == GPIO_PIN_10) g_buz_a = (int)s;
    else if (port == GPIOB && pin == GPIO_PIN_2) g_buz_b = (int)s;
}

HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *h, RTC_TimeTypeDef *t, uint32_t f)
{
    (void)h; (void)f;
    uint32_t s = g_clock / 1000u;
    t->Hours = (uint8_t)((s / 3600u) % 24u); t->Minutes = (uint8_t)((s / 60u) % 60u); t->Seconds = (uint8_t)(s % 60u);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *h, RTC_DateTypeDef *d, uint32_t f)
{
    (void)h; (void)f; d->Year = 26; d->Month = 10; d->Date = 5; return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Unlock(void) { g_fl_unlocked = 1; return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void)   { g_fl_unlocked = 0; return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t adr, uint64_t data)
{
    uint32_t off = adr - FLASH_BASE;
    (void)type;
    if (!g_fl_unlocked || (adr & 7u) || off + 8u > sizeof(g_flash)) { g_fl_viol++; return HAL_ERROR; }
    if (adr < g_fw_son)                      { g_fl_viol++; return HAL_ERROR; }   /* firmware'i ezme */
    if (g_inject_prog_fail)                  return HAL_ERROR;
    if (g_inject_prog_silent)                return HAL_OK;      /* HAL_OK doner ama hucreye YAZMAZ */
    for (int i = 0; i < 8; i++) if (g_flash[off + i] != 0xFF) { g_fl_viol++; return HAL_ERROR; }
    memcpy(&g_flash[off], &data, 8);
    g_fl_progs++;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *hata)
{
    /* gercek donanim: tek bank'ta Banks yok sayilir, sayfa mutlak; cift bank'ta sayfa bank ici + BKER */
    uint32_t off = e->Page * FLASH_PAGE_SIZE + ((FLASH_BANK_NB == 2u && e->Banks == FLASH_BANK_2) ? FLASH_BANK_SIZE : 0u);
    if (FLASH_BANK_NB == 2u && e->Page >= FLASH_BANK_SIZE / FLASH_PAGE_SIZE) { g_fl_viol++; return HAL_ERROR; }   /* bank disi sayfa */
    (void)hata;
    if (!g_fl_unlocked || e->NbPages != 1 || off + FLASH_PAGE_SIZE > sizeof(g_flash)) { g_fl_viol++; return HAL_ERROR; }
    if (FLASH_BASE + off < g_fw_son) { g_fl_viol++; return HAL_ERROR; }          /* firmware sayfasi silinmez */
    memset(&g_flash[off], 0xFF, FLASH_PAGE_SIZE);
    g_fl_erases++;
    return HAL_OK;
}

/* ------------------------------------------------------------- BMS taklidi */
typedef struct {
    int16_t  akim_dA;  uint16_t paket_dV;  uint8_t soc;
    uint16_t hmax, hmin;
    uint8_t  tmax_raw, tmin_raw;
    uint8_t  at_mask, at_raw[5];
    uint8_t  chg_mos, dchg_mos, plug;
    uint8_t  st1, cv, ct, at, sw;                    /* 1 = bu kare akisi acik */
} bms_t;
static bms_t B;
static long  g_step;

static void bms_saglikli(void)
{
    memset(&B, 0, sizeof(B));
    B.paket_dV = 262; B.soc = 60; B.akim_dA = 0;
    B.hmax = 3330; B.hmin = 3320;
    B.tmax_raw = 75; B.tmin_raw = 72;                /* 25 C / 22 C */
    B.at_mask = 0x07; B.at_raw[0] = 75; B.at_raw[1] = 72; B.at_raw[2] = 80; B.at_raw[3] = 0xFF; B.at_raw[4] = 0xFF;
    B.chg_mos = 1; B.dchg_mos = 1; B.plug = 0;
    B.st1 = B.cv = B.ct = B.at = B.sw = 1;
}

static void tx_st1(void) { uint8_t d[8] = {0}; uint16_t raw = (uint16_t)(B.akim_dA + 4000);
    d[0] = (uint8_t)B.paket_dV; d[1] = (uint8_t)(B.paket_dV >> 8); d[2] = (uint8_t)raw; d[3] = (uint8_t)(raw >> 8); d[4] = B.soc;
    Guvenlik_Kare(0x2F4, 0, d); }
static void tx_cv(void)  { uint8_t d[8] = {0}; d[0] = (uint8_t)B.hmax; d[1] = (uint8_t)(B.hmax >> 8); d[2] = 3;
    d[3] = (uint8_t)B.hmin; d[4] = (uint8_t)(B.hmin >> 8); d[5] = 5; Guvenlik_Kare(0x4F4, 0, d); }
static void tx_ct(void)  { uint8_t d[8] = {0}; d[0] = B.tmax_raw; d[1] = 1; d[2] = B.tmin_raw; d[3] = 2; d[4] = 74;
    Guvenlik_Kare(0x5F4, 0, d); }
static void tx_at(void)  { uint8_t d[8] = {0}; d[0] = B.at_mask; for (int i = 0; i < 5; i++) d[1 + i] = B.at_raw[i];
    Guvenlik_Kare(0x18F228F4u, 1, d); }
static void tx_sw(void)  { uint8_t d[8] = {0}; d[0] = (uint8_t)((B.chg_mos & 1) | ((B.dchg_mos & 1) << 1) | ((B.plug & 1) << 4));
    Guvenlik_Kare(0x18F528F4u, 1, d); }

static void adim(void)                               /* 20 ms */
{
    g_clock += 20u; g_step++;
    if (B.st1) tx_st1();
    if (g_step % 5 == 0 && B.cv) tx_cv();
    if (g_step % 25 == 0) { if (B.ct) tx_ct(); if (B.at) tx_at(); if (B.sw) tx_sw(); }
    if (g_step % 5 == 0) {
        Guvenlik_Task();
        g_buz_n++; if (g_buz_a) g_buz_on++;
        if (g_buz_a != g_buz_b) g_buz_mismatch++;
    }
}
static void sim_ms(uint32_t ms) { for (uint32_t n = 0; n < ms / 20u; n++) adim(); }
static void sim_s(uint32_t s)   { sim_ms(s * 1000u); }

/* ----------------------------------------------------------- test altyapisi */
static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf("    FAIL satir %d: ", __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static void reset_all(uint32_t t0)
{
    memset(g_flash, 0xFF, sizeof(g_flash));
    g_clock = t0; g_step = 0; g_page = 100; g_page_cmds = 0; g_bad_frames = 0;
    for (unsigned i = 0; i < sizeof(g_vp) / sizeof(g_vp[0]); i++) { g_vp[i].val = -1; g_vp[i].writes = 0; }
    g_buz_a = g_buz_b = 0; g_buz_on = g_buz_n = 0; g_buz_mismatch = 0;
    g_fl_unlocked = 0; g_fl_viol = 0; g_fl_progs = 0; g_fl_erases = 0; g_inject_prog_fail = 0;
    g_inject_prog_silent = 0; g_primask = 0; g_fw_son = FLASH_BASE + 79u * 1024u;
    bms_saglikli();
    gvn_test_maske = 0;
    Guvenlik_Init(&g_uart, &g_rtc);
}

static int aktif(int r)  { return (Guvenlik_Aktif_Maske() >> (r - 1)) & 1; }

static void buz_baslat(void) { g_buz_on = 0; g_buz_n = 0; }

/* flash'taki tum kayitlari eskiden yeniye oku */
#define KAYIT_OLAY (GVN_LOG_SADECE_OLAY ? 1 : 2)                /* olay basina kayit: sadece baslangic / +bitti */
static int log_hepsi(gv_kayit_t *out, int max)
{
    gv_baslik_t h[2];
    int g[2], n = 0, sira[2], ns = 0;
    g[0] = fl_baslik_oku(0, &h[0]);
    g[1] = fl_baslik_oku(1, &h[1]);
    if (g[0] && g[1]) { sira[0] = ((int32_t)(h[1].seq - h[0].seq) > 0) ? 0 : 1; sira[1] = 1 - sira[0]; ns = 2; }
    else if (g[0]) { sira[0] = 0; ns = 1; } else if (g[1]) { sira[0] = 1; ns = 1; }
    for (int s = 0; s < ns; s++)
        for (unsigned i = 1; i < LOG_SLOT; i++) {
            uint8_t b[16]; int bos = 1;
            GVN_FLASH_OKU(s_fl_adr[sira[s]] + i * 16u, b, 16);
            for (int j = 0; j < 16; j++) if (b[j] != 0xFF) { bos = 0; break; }
            if (bos) break;
            if (b[0] == KAYIT_IMZA && n < max) memcpy(&out[n++], b, 16);
        }
    return n;
}
static int log_say(int kural, int tip)
{
    static gv_kayit_t l[600]; int n = log_hepsi(l, 600), c = 0;
    for (int i = 0; i < n; i++) if ((!kural || l[i].kural == kural) && (!tip || l[i].tip == tip)) c++;
    return c;
}
static const gv_kayit_t *log_bul(int kural, int tip, int kacinci)
{
    static gv_kayit_t l[600]; int n = log_hepsi(l, 600), c = 0;
    for (int i = 0; i < n; i++)
        if (l[i].kural == kural && l[i].tip == tip && c++ == kacinci) return &l[i];
    return NULL;
}

#define BASLIK(s) printf("\n[%s]\n", s)

/* ================================================================ TESTLER */
static void t_saglikli(void)
{
    BASLIK("T1  saglikli paket 30 sn: hicbir sey olmamali");
    reset_all(0);
    sim_s(30);
    CHECK(Guvenlik_Aktif_Maske() == 0, "maske=%02X", Guvenlik_Aktif_Maske());
    CHECK(g_page_cmds == 0 && sayfa() == 100, "sayfa komutu=%d", g_page_cmds);
    CHECK(g_buz_on == 0, "buzzer calisti");
    CHECK(Guvenlik_Hat_Maske() == 0x1F, "hat maskesi=%02X", Guvenlik_Hat_Maske());
    CHECK(log_say(0, 0) == 0, "log bos olmali");
    CHECK(VP_POPUP == 0 && VP_SERIT == GVN_SERIT_NORMAL, "pop-up kapali, serit NORMAL olmali: popup=%d serit=%d", VP_POPUP, VP_SERIT);
    CHECK(mini_hepsi(GVN_MINI_GIZLI), "normalde bilgi cubugu rakamlari GIZLI (kart rengi)");
    CHECK(Guvenlik_Buzzer_Aktif() == 0, "buzzer sahipligi");
    CHECK(g_bad_frames == 0, "gecersiz DWIN cercevesi");
}

static void t_k7(void)
{
    BASLIK("T2  kural 7: haberlesme yok");
    reset_all(0);
    B.st1 = B.cv = B.ct = B.at = B.sw = 0;                      /* BMS hic konusmuyor */
    sim_ms(4800 + GVN_R7_ILK_EK_MS);
    CHECK(!aktif(7), "4.8 sn'de aktif olmamali");
    CHECK(VP_SERIT == GVN_SERIT_BOS && VP_POPUP == 0, "veri hic gelmediyse serit BOS ('normal' yazmaz): serit=%d", VP_SERIT);
    sim_ms(1000);
    CHECK(aktif(7) && Guvenlik_Aktif_Maske() == 0x40, "5.8 sn'de aktif olmali, maske=%02X", Guvenlik_Aktif_Maske());
#if GVN_R7_ILK_EK_MS == 0
    CHECK(g_page_cmds == 0 && sayfa() == 102, "pop-up hemen acilir, sayfa komutu yok");
#endif
    CHECK(g_buz_on > 0 && Guvenlik_Buzzer_Aktif(), "buzzer 5 sn desenine girmeli");
    sim_ms(1500);
    CHECK(sayfa() == 102 && g_page_cmds == 0, "pop-up acik, sayfa komutu YOK: popup=%d cmds=%d", VP_POPUP, g_page_cmds);
    CHECK(VP_SERIT == GVN_SERIT_VERIYOK && mini_hepsi(GVN_MINI_GIZLI), "veri yokken bilgi cubugu bayat rakam GOSTERMEZ: serit=%d", VP_SERIT);
    CHECK(kod() == 7 && (VP_POPUP == 9 || VP_POPUP == 10), "kod=%d popup=%d", kod(), VP_POPUP);
    buz_baslat(); sim_s(10);
    CHECK(g_buz_on >= 7 && g_buz_on <= 9, "10 sn'de ~8 ornek ON beklenir (2 bip x 400 ms): %ld", g_buz_on);
    /* veri geri geldi */
    bms_saglikli(); sim_ms(1500);
    CHECK(aktif(7), "veri geldikten 1.5 sn sonra hala aktif (2 sn bekleme)");
    sim_ms(900);
    CHECK(!aktif(7) && sayfa() == 100 && VP_SERIT == GVN_SERIT_NORMAL, "2.4 sn sonra kapanmali, popup=%d serit=%d", VP_POPUP, VP_SERIT);
    CHECK(!Guvenlik_Buzzer_Aktif() && g_buz_a == 0 && g_buz_b == 0, "buzzer susmali");
    sim_s(70);
#if GVN_LOG_SADECE_OLAY
    CHECK(log_say(7, 1) == 1 && log_say(0, 0) == 1, "olay basinda TEK kayit (bitti yok): %d", log_say(0, 0));
#else
    CHECK(log_say(7, 1) == 1 && log_say(7, 3) == 1, "BASLADI+BITTI: %d/%d", log_say(7, 1), log_say(7, 3));
    const gv_kayit_t *k = log_bul(7, 3, 0);              /* 5.1 sn'de basladi, ~19.4 sn'de bitti */
    CHECK(k && k->c >= 13 && k->c <= 16, "sure=%d sn", k ? k->c : -1);
#endif
    CHECK(g_buz_mismatch == 0 && g_fl_viol == 0 && g_bad_frames == 0, "yan hatalar");
}

#if GVN_R7_ILK_EK_MS > 0
static void t_k7_ilk_ek(void)
{
    BASLIK("T7b kural 7: acilista ek sure (GVN_R7_ILK_EK_MS)");
    reset_all(0);
    B.st1 = B.cv = B.ct = B.at = B.sw = 0;
    sim_ms(5000 + GVN_R7_ILK_EK_MS - 300);
    CHECK(!aktif(7), "ek sure dolmadan alarm yok");
    sim_ms(800);
    CHECK(aktif(7), "ek sure + 5 sn sonra alarm");
    /* ilk kare geldikten sonra ek sure UYGULANMAZ */
    reset_all(0); sim_s(2);
    B.st1 = B.cv = B.ct = B.at = B.sw = 0; sim_ms(5400);
    CHECK(aktif(7), "ilk kare alindiktan sonra normal 5 sn");
}
#else
static void t_k7_ilk_ek(void) {}
#endif

static void k1_kur(uint16_t hmax, uint8_t chg_mos, int16_t akim)
{
    B.hmax = hmax; B.hmin = (uint16_t)(hmax - 50); B.chg_mos = chg_mos; B.akim_dA = akim; B.plug = 1;
}

static void t_k1(void)
{
    BASLIK("T3  kural 1: asiri sarj + sarj kontaktoru yapisik");
    reset_all(0);
    sim_s(10);
    k1_kur(3710, 0, 60);                                         /* 3.71 V, kontaktor kesik, +6 A */
    sim_ms(4800);
    CHECK(!aktif(1), "4.8 sn'de aktif olmamali");
    sim_ms(1000);
    CHECK(aktif(1), "5.8 sn'de aktif olmali");
    sim_ms(500);
    CHECK(sayfa() == 102 && kod() == 1 && (VP_POPUP == 1 || VP_POPUP == 2), "sayfa=%d kod=%d kritik=%d", sayfa(), kod(), VP_POPUP);
    buz_baslat(); sim_s(3);
    CHECK(g_buz_on == g_buz_n, "buzzer SUREKLI olmali (%ld/%ld)", g_buz_on, g_buz_n);
    CHECK(VP_SERIT == GVN_SERIT_BILGI && mini_hepsi(GVN_MINI_ACIK), "kural 1 pop-up'inin altinda canli bilgi cubugu");
    /* yanip sonme: iki farkli kare gorulmeli */
    int gor0 = 0, gor1 = 0;
    for (int i = 0; i < 40; i++) { sim_ms(100); if (VP_POPUP == 1) gor0 = 1; if (VP_POPUP == 2) gor1 = 1; }
    CHECK(gor0 && gor1, "parlak/sonuk kare gorulmeli");
    /* histerezis: 3.66 V + 6 A -> hala aktif; akim kesilince 3 sn sonra kapanir */
    k1_kur(3660, 0, 60); sim_s(8);
    CHECK(aktif(1), "3.66 V'ta (histerezis 3.65) hala aktif olmali");
    /* VEYA modunda 3.66 V tek basina alarmi tutar; kapanmasi icin hucre de dusmeli */
    k1_kur(GVN_R1_VEYA ? 3600 : 3660, 0, 0); sim_ms(2500);
    CHECK(aktif(1), "kosul bittikten 2.5 sn sonra hala aktif (3 sn bekleme)");
    sim_ms(1500);
    CHECK(!aktif(1) && sayfa() == 100 && !Guvenlik_Buzzer_Aktif(), "kapanmali");
    CHECK(VP_SERIT == GVN_SERIT_NORMAL && mini_hepsi(GVN_MINI_GIZLI), "kapaninca serit NORMAL, rakamlar gizli");

#if !GVN_R1_VEYA
    /* VE mantigi: eksik olan her kosulda ALARM OLMAMALI */
    reset_all(0); sim_s(5);
    k1_kur(3710, 1, 60); sim_s(20);                              /* kontaktor KAPALI(iletiyor) */
    CHECK(!aktif(1), "kontaktor iletiyorken (bit=1) alarm yok (spec: VE)");
    reset_all(0); sim_s(5);
    k1_kur(3710, 0, 40); sim_s(20);                              /* 4 A < 5 A */
    CHECK(!aktif(1), "akim 4 A iken alarm yok");
    reset_all(0); sim_s(5);
    k1_kur(3690, 0, 60); sim_s(20);                              /* 3.69 V < 3.7 V */
    CHECK(!aktif(1), "hucre 3.69 V iken alarm yok");
    reset_all(0); sim_s(5);
    k1_kur(3700, 0, 60); sim_s(20);                              /* tam esik: GECMIS degil */
    CHECK(!aktif(1), "tam 3.700 V 'gecmis' sayilmaz");
    reset_all(0); sim_s(5);
    k1_kur(3710, 0, 50); sim_s(20);                              /* tam 5.0 A: fazla degil */
    CHECK(!aktif(1), "tam +5.0 A 'fazla' sayilmaz");
#else
    /* VEYA modu: hucre tek basina da, "kontaktor kesik + akim" tek basina da yeter */
    reset_all(0); sim_s(5);
    k1_kur(3710, 1, 0); sim_s(8);
    CHECK(aktif(1), "VEYA: hucre 3.71 V tek basina yeter");
    reset_all(0); sim_s(5);
    k1_kur(3600, 0, 60); sim_s(8);
    CHECK(aktif(1), "VEYA: kontaktor kesik + 6 A tek basina yeter");
    reset_all(0); sim_s(5);
    k1_kur(3690, 1, 40); sim_s(20);
    CHECK(!aktif(1), "VEYA: ikisi de saglanmiyorsa alarm yok");
    reset_all(0); sim_s(5);
    k1_kur(3700, 0, 50); sim_s(20);
    CHECK(!aktif(1), "VEYA: tam esiklerde alarm yok");
#endif
    /* 4.9 sn sonra kosul bozulursa sayac sifirlanmali (iki modda da) */
    reset_all(0); sim_s(5);
    k1_kur(3710, 0, 60); sim_ms(4000);
    k1_kur(3600, 0, 0);  sim_ms(1500);                           /* iki mod icin de kosul YANLIS */
    k1_kur(3710, 0, 60); sim_ms(4000);
    CHECK(!aktif(1), "kesintili kosul 5 sn'ye ulasmamali");
    sim_ms(2000);
    CHECK(aktif(1), "kesintisiz 5 sn sonra aktif");
}

#if GVN_MOS_DAHIL
static void t_mos(void)
{
    BASLIK("T20 MOS sicakligi dahil (GVN_MOS_DAHIL=1)");
    reset_all(0); sim_s(8);
    B.at_raw[2] = 111; sim_s(10);                                /* MOS 61 C, hucreler 25 C */
    CHECK(aktif(5) && sayfa() == 102, "MOS 61 C kural 5'i tetiklemeli");
    B.at_raw[2] = 80; sim_s(6);
    CHECK(!aktif(5) && !aktif(4), "MOS normale donunce kapanir");
}
#else
static void t_mos(void)
{
    BASLIK("T20 MOS sicakligi varsayilanda DAHIL DEGIL");
    reset_all(0); sim_s(8);
    B.at_raw[2] = 125; sim_s(20);                                /* MOS 75 C, hucreler 25 C */
    CHECK(!aktif(4) && !aktif(5) && sayfa() == 100, "MOS 75 C hucre uyarilarini tetiklememeli");
}
#endif

static void t_k2(void)
{
    BASLIK("T4  kural 2: asiri desarj / desarj kontaktoru yapisik");
    reset_all(0); sim_s(8);
    B.hmax = 3300; B.hmin = 2590; sim_ms(4500);                  /* A kolu: hucre < 2.6 V */
    CHECK(!aktif(2), "4.5 sn'de aktif degil");
    sim_ms(1500);
    CHECK(aktif(2) && sayfa() == 102 && kod() == 2 && (VP_POPUP == 3 || VP_POPUP == 4), "A kolu: sayfa=%d kod=%d kr=%d", sayfa(), kod(), VP_POPUP);
    buz_baslat(); sim_s(2);
    CHECK(g_buz_on == g_buz_n, "surekli buzzer");
    B.hmin = 2650; sim_s(8);
    CHECK(aktif(2), "2.65 V: histerezis (2.7) altinda hala aktif");
    B.hmin = 2750; sim_ms(2500);
    CHECK(aktif(2), "2.5 sn sonra hala aktif");
    sim_ms(1500);
    CHECK(!aktif(2), "2.75 V + 3 sn: kapanmali");

    reset_all(0); sim_s(8);                                      /* B kolu: kontaktor KESIK + -6 A */
    B.dchg_mos = 0; B.akim_dA = -60; sim_ms(4800);
    CHECK(!aktif(2), "B kolu 4.8 sn aktif degil");
    sim_ms(1500);
    CHECK(aktif(2), "B kolu: kontaktor kesik + -6 A + 5 sn -> aktif");
    B.akim_dA = 0; sim_s(5);
    CHECK(!aktif(2), "akim kesilince kapanir");

    reset_all(0); sim_s(8);
    B.dchg_mos = 0; B.akim_dA = -40; sim_s(20);
    CHECK(!aktif(2), "-4 A: alarm yok");
    reset_all(0); sim_s(8);
    B.dchg_mos = 1; B.akim_dA = -300; sim_s(20);                 /* normal surus: 30 A desarj */
    CHECK(!aktif(2), "kontaktor iletirken -30 A (normal kullanim): alarm YOK");
    reset_all(0); sim_s(8);
    B.hmin = 2600; sim_s(20);
    CHECK(!aktif(2), "tam 2.600 V 'altina dusmus' degil");
}

static void t_k3(void)
{
    BASLIK("T5  kural 3: hucre dengesizligi, seritte 15 dk (sonuk)");
    reset_all(0); sim_s(8);
    B.hmax = 3330; B.hmin = 3020;                                /* 310 mV */
    sim_ms(1500);
    CHECK(!aktif(3), "1.5 sn'de aktif degil");
    sim_ms(1500);
    CHECK(aktif(3) && VP_SERIT == GVN_SERIT_K3, "2 sn sonra seritte kural 3: serit=%d", VP_SERIT);
    CHECK(g_page_cmds == 0 && VP_POPUP == 0, "pop-up acilmamali, popup=%d", VP_POPUP);
    sim_s(14 * 60);
    CHECK(VP_SERIT == GVN_SERIT_K3, "14 dk sonra hala gorunur");
    sim_s(90);
    CHECK(VP_SERIT == GVN_SERIT_BOS, "15 dk dolunca kalkmali (kural hala aktif -> NORMAL da yazilmaz): serit=%d", VP_SERIT);
    CHECK(aktif(3), "durum hala aktif (kayit icin)");
    sim_s(20 * 60);
    CHECK(VP_SERIT == GVN_SERIT_BOS, "ayni bozulma icin TEKRAR gosterilmemeli");
    CHECK(g_buz_on == 0, "buzzer hic calmamali");
    CHECK(g_page_cmds == 0, "hic sayfa degismemeli");
    B.hmin = 3230; sim_ms(2500);                                 /* 100 mV: duzeldi */
    CHECK(aktif(3), "2.5 sn: hala aktif");
    sim_ms(1500);
    CHECK(!aktif(3), "3 sn sonra kapandi");
    B.hmin = 3020; sim_s(4);
    CHECK(aktif(3) && VP_SERIT == GVN_SERIT_K3, "tekrar bozulunca yeniden gosterilmeli");
    B.hmin = 3050; sim_s(8);                                     /* 280 mV: 250 uzerinde -> sürüyor */
    CHECK(aktif(3), "280 mV: histerezis (250) uzerinde aktif kalir");
    B.hmin = 3100; sim_s(5);                                     /* 230 mV */
    CHECK(!aktif(3), "230 mV: kapanir");
    reset_all(0); sim_s(8); B.hmax = 3330; B.hmin = 3031; sim_s(20);   /* 299 mV */
    CHECK(!aktif(3), "299 mV: alarm yok");
    reset_all(0); sim_s(8); B.hmax = 3330; B.hmin = 3030; sim_s(20);   /* tam 300 mV: 'olursa' -> aktif */
    CHECK(aktif(3), "tam 300 mV: aktif ('0.3 V olursa')");
}

static void t_k4k5(void)
{
    BASLIK("T6  kural 4 (>55 C) ve kural 5 (>60 C)");
    reset_all(0); sim_s(8);
    B.tmax_raw = 105; sim_s(10);                                 /* tam 55 C */
    CHECK(!aktif(4), "tam 55 C 'gecmis' degil");
    B.tmax_raw = 106; sim_ms(1500);                              /* 56 C */
    CHECK(!aktif(4), "1.5 sn: henuz degil");
    sim_ms(1500);
    CHECK(aktif(4) && sayfa() == 101 && VP_SERIT == GVN_SERIT_K4 && VP_POPUP == 0, "serit=%d popup=%d", VP_SERIT, VP_POPUP);
    buz_baslat(); sim_s(10);
    CHECK(g_buz_on >= 7 && g_buz_on <= 9 && g_buz_on < g_buz_n / 4, "5 sn'de bir kisa bip: %ld/%ld", g_buz_on, g_buz_n);
    B.tmax_raw = 104; sim_s(8);
    CHECK(aktif(4), "54 C: histerezis (53) uzerinde aktif");
    B.tmax_raw = 102; sim_s(4);
    CHECK(!aktif(4) && sayfa() == 100 && VP_SERIT == GVN_SERIT_NORMAL, "52 C: kapanir");

    reset_all(0); sim_s(8);
    B.tmax_raw = 110; sim_s(10);                                 /* tam 60 C */
    CHECK(aktif(4) && !aktif(5), "tam 60 C: kural 4 evet, kural 5 hayir");
    B.tmax_raw = 111; sim_s(4);                                  /* 61 C */
    CHECK(aktif(5) && sayfa() == 102 && kod() == 5 && (VP_POPUP == 5 || VP_POPUP == 6), "sayfa=%d kod=%d popup=%d", sayfa(), kod(), VP_POPUP);
    buz_baslat(); sim_s(3);
    CHECK(g_buz_on == g_buz_n, "kural 5: SUREKLI buzzer");
    B.tmax_raw = 108; sim_s(8);                                  /* 58 C */
    CHECK(aktif(5), "58 C: kural 5 histerezis (57) uzerinde hala aktif");
    B.tmax_raw = 106; sim_s(4);                                  /* 56 C */
    CHECK(!aktif(5) && aktif(4) && sayfa() == 101 && kod() == 4, "56 C: kural 5 biter, kural 4 kalir (popup=%d serit=%d)", VP_POPUP, VP_SERIT);
    buz_baslat(); sim_s(10);
    CHECK(g_buz_on < g_buz_n / 4, "tekrar 5 sn desenine donmeli");
    B.tmax_raw = 90; sim_s(4);
    CHECK(Guvenlik_Aktif_Maske() == 0 && sayfa() == 100, "her sey kapanir");
    CHECK(g_buz_a == 0, "buzzer sustu");
}

static void t_k6(void)
{
    BASLIK("T7  kural 6: < -10 C");
    reset_all(0); sim_s(8);
    B.tmin_raw = 40; sim_s(10);                                  /* tam -10 C */
    CHECK(!aktif(6), "tam -10 C 'altina inmis' degil");
    B.tmin_raw = 39; sim_s(4);                                   /* -11 C */
    CHECK(aktif(6) && sayfa() == 102 && kod() == 6 && (VP_POPUP == 7 || VP_POPUP == 8), "sayfa=%d kod=%d popup=%d", sayfa(), kod(), VP_POPUP);
    buz_baslat(); sim_s(3);
    CHECK(g_buz_on == g_buz_n, "surekli buzzer");
    B.tmin_raw = 41; sim_s(8);                                   /* -9 C */
    CHECK(aktif(6), "-9 C: histerezis (-8) altinda aktif");
    B.tmin_raw = 43; sim_s(4);                                   /* -7 C */
    CHECK(!aktif(6) && sayfa() == 100, "-7 C: kapanir");
    /* NA / sacma deger soguk alarmi VERMEMELI; sensor hatasi vermeli */
    reset_all(0); sim_s(8);
    B.tmin_raw = 0; sim_s(8);                                    /* -50 C = sensor yok sayilir */
    CHECK(!aktif(6), "ham 0 (-50 C) soguk alarmi vermemeli");
    CHECK(aktif(8), "ama sensor hatasi (kural 8) vermeli");
    reset_all(0); sim_s(8);
    B.tmin_raw = 0xFF; sim_s(8);
    CHECK(!aktif(6) && aktif(8), "0xFF: NA -> kural 8");
}

static void t_k8(void)
{
    BASLIK("T8  kural 8: sicaklik sensoru NA");
    reset_all(0); sim_s(8);
    B.at_raw[0] = 0xFF; sim_ms(4200);
    CHECK(!aktif(8), "4.2 sn: henuz degil");
    sim_ms(1500);
    CHECK(aktif(8) && sayfa() == 101 && VP_SERIT == GVN_SERIT_K8, "serit=%d", VP_SERIT);
    buz_baslat(); sim_s(10);
    CHECK(g_buz_on >= 7 && g_buz_on <= 9, "5 sn desen: %ld", g_buz_on);
    B.at_raw[0] = 75; sim_ms(4200);
    CHECK(aktif(8), "duzeldi, 4.2 sn: hala aktif (5 sn bekleme)");
    sim_ms(1500);
    CHECK(!aktif(8) && sayfa() == 100, "5 sn sonra kapanir");
    /* CAN: en az GVN_SENSOR_CAN_ADET (3) gecerli sensor (kullanici karari 2026-10-09) */
    reset_all(0); sim_s(8);
    CHECK(!aktif(8) && gvn_sensor_adet == 3, "CAN 3 sensor: kural 8 YOK (adet=%u)", gvn_sensor_adet);
    B.at_raw[2] = 0xFF; sim_ms(4200);                            /* 3. sensor (T3) okunmuyor */
    CHECK(!aktif(8), "2 sensor, 4.2 sn: henuz degil");
    sim_ms(1500);
    CHECK(aktif(8) && gvn_sensor_adet == 2 && gvn_sensor_na == 0x44, "CAN 2 sensor: kural 8 (adet=%u na=0x%02X)",
          gvn_sensor_adet, gvn_sensor_na);
    reset_all(0); sim_s(8);
    /* sahada (2026-10-09, 16S CAN, 3 sensor takili): OLAY A=0x43 -> T1/T2 maske/yuva suphesi.
     * Sensor varligi DEGERE gore: maske biti 0 ama deger gecerli -> sayilir */
    B.at_mask = 0x04; sim_s(10);
    CHECK(!aktif(8) && gvn_sensor_adet == 3, "maske sadece T3 ama T1 T2 T3 degerleri gecerli: kural 8 YOK");
    B.at_raw[1] = 0xFF; sim_s(6);                                /* T2 gercekten yok (FF) */
    CHECK(aktif(8) && (gvn_sensor_na & 0x02), "T2 = FF: kural 8 (na=0x%02X)", gvn_sensor_na);
    CHECK(log_say(8, 0) == 1 && s_o[K8].b == 2, "olay kaydinda B = sensor sayisi: %d", s_o[K8].b);
    reset_all(0);
    B.at_mask = 0x0B; B.at_raw[2] = 0xFF; B.at_raw[3] = 70; sim_s(15);   /* T1 T2 T4 */
    CHECK(!aktif(8) && gvn_sensor_adet == 3, "3 sensor farkli yuvalarda (T1 T2 T4): kural 8 YOK");
    reset_all(0);
    B.at_mask = 0x0F; B.at_raw[3] = 70; sim_s(15);
    CHECK(!aktif(8) && gvn_sensor_adet == 4 && gvn_sensor_na == 0, "4 sensor: kural 8 YOK (na=0x%02X)", gvn_sensor_na);
    reset_all(0);
    B.at_mask = 0x01; B.at_raw[1] = B.at_raw[2] = 0xFF; sim_s(8);
    CHECK(aktif(8) && gvn_sensor_adet == 1 && gvn_sensor_na == 0x46, "tek sensor: kural 8 (na=0x%02X)", gvn_sensor_na);
    reset_all(0); sim_s(8);
    B.tmax_raw = 0xFF; sim_s(8);                                 /* BMS max/min gecersiz */
    CHECK(aktif(8), "CELL_TEMP gecersiz: kural 8");
    CHECK(!aktif(4) && !aktif(5), "ve sicak alarmlari VERILMEZ (veri yok)");
    /* 5 sn'den kisa NA (ilk acilis saniyesi gibi) alarm vermemeli */
    reset_all(0); sim_s(8);
    B.at_raw[0] = 0xFF; sim_s(3); B.at_raw[0] = 75; sim_s(10);
    CHECK(!aktif(8) && log_say(8, 0) == 0, "3 sn'lik NA alarm/kayit uretmemeli");
    CHECK(gvn_sensor_na == 0 && gvn_at_maske == 0x07, "debug: na=0x%02X maske=0x%02X", gvn_sensor_na, gvn_at_maske);
    CHECK(gvn_sicak_ham[0] == 0x07 && gvn_sicak_ham[1] == 75 && gvn_sicak_ham[2] == 72 && gvn_sicak_ham[3] == 80 &&
          gvn_sicak_ham[4] == 0xFF && gvn_sicak_ham[6] == 75 && gvn_sicak_ham[7] == 72, "debug: ham sicakliklar");
}

static void t_oncelik(void)
{
    BASLIK("T9  oncelik ve donusumlu gosterim");
    reset_all(0); sim_s(8);
    k1_kur(3710, 0, 60); B.tmax_raw = 111; sim_s(8);             /* kural 1 + kural 5 */
    CHECK(aktif(1) && aktif(5), "ikisi de aktif");
    int s1 = 0, s5 = 0, diger = 0, son = kod(), nd = 0;
    uint32_t t_deg[16];
    for (int i = 0; i < 160; i++) {                              /* 16 sn izle */
        sim_ms(100);
        if (kod() == 1) s1++; else if (kod() == 5) s5++; else diger++;
        if (kod() != son) { if (nd < 16) t_deg[nd++] = g_clock; son = kod(); }
    }
    int seg_ok = (nd >= 3);                                      /* ilk (kismi) segment sayilmaz */
    for (int i = 1; i < nd; i++) if (t_deg[i] - t_deg[i - 1] < 3800u || t_deg[i] - t_deg[i - 1] > 4200u) seg_ok = 0;
    CHECK(diger == 0 && s1 > 40 && s5 > 40, "donusumlu gosterim: kural1=%d kural5=%d diger=%d", s1, s5, diger);
    CHECK(seg_ok, "her uyari ~4 sn gorunmeli (degisim sayisi=%d)", nd);
    CHECK(sayfa() == 102, "tam ekran");
    buz_baslat(); sim_s(2);
    CHECK(g_buz_on == g_buz_n, "surekli buzzer");

    reset_all(0); sim_s(8);
    B.tmax_raw = 106; B.at_raw[0] = 0xFF; sim_s(10);             /* kural 4 + kural 8 */
    s1 = 0; s5 = 0;
    for (int i = 0; i < 160; i++) { sim_ms(100); if (VP_SERIT == GVN_SERIT_K4) s1++; if (VP_SERIT == GVN_SERIT_K8) s5++; }
    CHECK(sayfa() == 101 && s1 > 40 && s5 > 40, "uyari seridi donusumu: %d/%d", s1, s5);

    reset_all(0); sim_s(8);
    B.tmax_raw = 111; B.at_raw[0] = 0xFF; sim_s(10);             /* kural 4,5 + 8 */
    CHECK(sayfa() == 102 && kod() == 5, "kritik pop-up seridin ustune biner: kod=%d", kod());
    B.hmax = 3330; B.hmin = 3020; sim_s(5);                      /* + kural 3 */
    CHECK(VP_SERIT == GVN_SERIT_BILGI && mini_hepsi(GVN_MINI_ACIK), "pop-up acikken altta CANLI bilgi cubugu: serit=%d", VP_SERIT);
    CHECK(aktif(3), "kural 3 de aktif (kayit icin)");

    reset_all(0); sim_s(8);                                      /* kritik + sadece kural 3 */
    B.tmin_raw = 39; B.hmax = 3330; B.hmin = 3020; sim_s(6);
    CHECK(sayfa() == 102 && kod() == 6 && VP_SERIT == GVN_SERIT_BILGI, "pop-up kural 6, altta bilgi cubugu: popup=%d serit=%d", VP_POPUP, VP_SERIT);
    B.tmin_raw = 72; sim_s(5);                                   /* soguk bitti, kural 3 suruyor */
    CHECK(VP_POPUP == 0 && VP_SERIT == GVN_SERIT_K3 && mini_hepsi(GVN_MINI_GIZLI), "pop-up kapaninca serit kural 3'e doner, rakamlar gizlenir: serit=%d", VP_SERIT);
    CHECK(g_page_cmds == 0, "hicbir durumda sayfa komutu gonderilmez");
}

static void t_iletisim_kaybi(void)
{
    BASLIK("T10 alarm sirasinda haberlesme kopmasi");
    reset_all(0); sim_s(8);
    k1_kur(3710, 0, 60); sim_s(8);
    CHECK(aktif(1), "kural 1 aktif");
    B.st1 = B.cv = B.ct = B.at = B.sw = 0;
    sim_s(3);
    CHECK(aktif(1), "3 sn: veri bayat ama haberlesme alarmi henuz yok, durum korunur");
    sim_s(4);
    CHECK(!aktif(1) && aktif(7) && Guvenlik_Aktif_Maske() == 0x40, "kopunca sadece kural 7: maske=%02X", Guvenlik_Aktif_Maske());
    CHECK(VP_SERIT == GVN_SERIT_VERIYOK && mini_hepsi(GVN_MINI_GIZLI), "kopunca bilgi cubugu 'veri yok' olur, son rakamlar gizlenir");
    CHECK(sayfa() == 102 && kod() == 7, "kod=%d", kod());
    bms_saglikli(); k1_kur(3710, 0, 60);
    sim_s(10);
    CHECK(!aktif(7), "veri geldi, kural 7 bitti");
    sim_s(10);
    CHECK(aktif(1), "kural 1 kosulu surdugu icin tekrar aktif");
    sim_s(100);
    CHECK(log_say(1, 1) == 1, "kural 1: 60 sn icinde dondugu icin BIRLESTIRILDI (BASLADI=%d)", log_say(1, 1));
}

static void t_log_devam(void)
{
    BASLIK("T11 log: BASLADI -> DEVAM(60sn,5dk,15dk,1sa,2sa) -> BITTI");
    reset_all(0); sim_s(8);
    k1_kur(3720, 0, 80); sim_s(2 * 3600 + 120);
    k1_kur(3300, 1, 0); sim_s(200);
#if GVN_LOG_SADECE_OLAY
    {
        const gv_kayit_t *b = log_bul(1, 1, 0);
        CHECK(log_say(0, 0) == 1 && b && b->a == 3720 && b->b == 80 && b->c == 3,
              "2 saatlik olay: TEK kayit, hucre no 3 (BMS karesi): n=%d c=%d", log_say(0, 0), b ? b->c : -1);
        reset_all(0); sim_s(8); B.tmax_raw = 106; sim_s(1800); B.tmax_raw = 75; sim_s(120);
        CHECK(log_say(4, 1) == 1 && log_say(0, 0) == 1, "kural 4: tek kayit");
    }
    return;
#endif
    CHECK(log_say(1, 1) == 1, "BASLADI=%d", log_say(1, 1));
    CHECK(log_say(1, 2) == 5, "DEVAM=%d (1 dk,5 dk,15 dk,1 sa,2 sa)", log_say(1, 2));
    CHECK(log_say(1, 3) == 1, "BITTI=%d", log_say(1, 3));
    static const int bek[5] = { 60, 300, 900, 3600, 7200 };
    for (int i = 0; i < 5; i++) {
        const gv_kayit_t *k = log_bul(1, 2, i);
        CHECK(k && k->c >= bek[i] && k->c <= bek[i] + 2, "DEVAM %d: sure=%d (beklenen %d)", i, k ? k->c : -1, bek[i]);
    }
    const gv_kayit_t *b = log_bul(1, 1, 0), *e = log_bul(1, 3, 0);
    CHECK(b && b->a == 3720 && b->b == 80, "BASLADI olcumleri a=%d b=%d", b ? b->a : 0, b ? b->b : 0);
    CHECK(e && e->c >= 7300 && e->c <= 7340, "BITTI sure=%d (~7317 beklenir)", e ? e->c : -1);
    CHECK(b && b->yil == 26 && b->ay == 10 && b->gun == 5, "RTC alani");
    /* kural 4 icin DEVAM yazilmamali (maske disinda) */
    reset_all(0); sim_s(8); B.tmax_raw = 106; sim_s(1800); B.tmax_raw = 75; sim_s(120);
    CHECK(log_say(4, 2) == 0 && log_say(4, 1) == 1 && log_say(4, 3) == 1, "kural 4: sadece BASLADI+BITTI");
}

static void t_log_birlestir(void)
{
    BASLIK("T12 log: 60 sn icinde tekrar eden olay birlestirilir");
    reset_all(0); sim_s(8);
    for (int i = 0; i < 5; i++) {
        B.tmax_raw = (uint8_t)(106 + i); sim_s(10);              /* ac */
        B.tmax_raw = 75; sim_s(10);                              /* kapat (3 sn sonra biter) */
    }
    sim_s(70);
#if GVN_LOG_SADECE_OLAY
    CHECK(log_say(0, 0) == 1 && log_bul(4, 1, 0) && log_bul(4, 1, 0)->a == 56, "5 kez acip kapanan olay: TEK kayit (56 C)");
    B.tmax_raw = 106; sim_s(10); B.tmax_raw = 75; sim_s(80);
    CHECK(log_say(4, 1) == 2 && log_say(0, 0) == 2, "60 sn'den sonra yeni olay: yeni kayit");
#else
    CHECK(log_say(4, 1) == 1 && log_say(4, 3) == 1, "tek olay: BASLADI=%d BITTI=%d", log_say(4, 1), log_say(4, 3));
    const gv_kayit_t *e = log_bul(4, 3, 0);
    CHECK(e && e->a == 60, "tepe 110-50=60 C beklenir, a=%d", e ? e->a : -999);
    /* 60 sn'den uzun aradan sonra YENI olay */
    B.tmax_raw = 106; sim_s(10); B.tmax_raw = 75; sim_s(80);
    CHECK(log_say(4, 1) == 2 && log_say(4, 3) == 2, "ikinci olay ayri kayit: %d/%d", log_say(4, 1), log_say(4, 3));
#endif
}

static void t_halka(void)
{
    BASLIK("T13 flash halkasi: 400 olay, sayfa dolup doner");
    reset_all(0); sim_s(8);
    for (int i = 0; i < 400; i++) {
        B.hmax = 3330; B.hmin = (uint16_t)(3330 - 300 - (i % 200));       /* delta = 300 + i%200 */
        sim_s(4);
        B.hmin = 3320; sim_s(70);
    }
    CHECK(g_fl_viol == 0, "flash kural ihlali: %d", g_fl_viol);
    CHECK(s_fl_ok, "log hala acik olmali");
    CHECK(g_fl_erases >= (GVN_LOG_SADECE_OLAY ? 2 : 5), "sayfalar silinmis olmali: %d", g_fl_erases);
    static gv_kayit_t l[600];
    int n = log_hepsi(l, 600);
    CHECK(n >= 127 && n <= 254, "saklanan kayit sayisi=%d (127..254)", n);
    /* sira: ardisik BASLADI'lar +1 artmali (200'e gore mod) */
    int bozuk = 0, onceki = -1;
    for (int i = 0; i < n; i++) {
        if (l[i].tip != TIP_BASLADI) continue;
        int idx = l[i].a - 300;
        if (onceki >= 0 && idx != (onceki + 1) % 200) bozuk++;
        onceki = idx;
    }
    CHECK(bozuk == 0, "kayit sirasi bozuk: %d", bozuk);
    CHECK(l[n - 1].tip == (GVN_LOG_SADECE_OLAY ? TIP_BASLADI : TIP_BITTI) && l[n - 1].a == 300 + (399 % 200),
          "son kayit: tip=%d a=%d (BITTI, 499 beklenir)", l[n - 1].tip, l[n - 1].a);
    CHECK(s_fl_seq >= 4, "sayfa dizisi artmis olmali: seq=%u", (unsigned)s_fl_seq);
    int bos_olmayan_sayfa = 0;
    for (int p = 0; p < 2; p++) { gv_baslik_t h; if (fl_baslik_oku((uint8_t)p, &h)) bos_olmayan_sayfa++; }
    CHECK(bos_olmayan_sayfa == 2, "iki sayfa da basliga sahip");
    /* ayni seq'li iki sayfa olmamali */
    gv_baslik_t h0, h1; fl_baslik_oku(0, &h0); fl_baslik_oku(1, &h1);
    CHECK(h0.seq != h1.seq && (h0.seq + 1 == h1.seq || h1.seq + 1 == h0.seq), "seq'ler ardisik: %u %u", (unsigned)h0.seq, (unsigned)h1.seq);
}

static void t_yeniden_baslat(void)
{
    BASLIK("T14 reset sonrasi kayit kaldigi yerden surer");
    reset_all(0); sim_s(8);
    for (int i = 0; i < 10; i++) { B.hmax = 3330; B.hmin = 3020; sim_s(4); B.hmin = 3320; sim_s(70); }
    int once = log_say(0, 0);
    CHECK(once == 10 * KAYIT_OLAY, "10 olay = %d kayit: %d", 10 * KAYIT_OLAY, once);
    Guvenlik_Init(&g_uart, &g_rtc);                              /* reset */
    CHECK(s_fl_ok && s_fl_n == 10 * KAYIT_OLAY + 1, "n=%u", (unsigned)s_fl_n);
    bms_saglikli(); sim_s(8);
    B.hmax = 3330; B.hmin = 3020; sim_s(4); B.hmin = 3320; sim_s(70);
    CHECK(log_say(0, 0) == 11 * KAYIT_OLAY, "reset sonrasi eklendi: %d", log_say(0, 0));
    CHECK(g_fl_erases == 0, "gereksiz silme yok: %d", g_fl_erases);

    /* sayfa tam dolu iken reset */
    reset_all(0);
    for (int i = 0; i < 63; i++) { kayit_yaz(3, TIP_BASLADI, 0, 300 + i, 0, 50); kayit_yaz(3, TIP_BITTI, 5, 300 + i, 0, 50); }
    kayit_yaz(3, TIP_BASLADI, 0, 363, 0, 50);                    /* 127. kayit: sayfa dolu */
    CHECK(s_fl_n == LOG_SLOT, "sayfa dolu: n=%u", (unsigned)s_fl_n);
    Guvenlik_Init(&g_uart, &g_rtc);
    CHECK(s_fl_aktif == 0 && s_fl_n == LOG_SLOT, "dolu sayfa algilandi");
    kayit_yaz(3, TIP_BITTI, 5, 363, 0, 50);                      /* tasma -> sayfa 1 */
    CHECK(s_fl_aktif == 1 && s_fl_n == 2 && s_fl_seq == 2, "diger sayfaya gecti: aktif=%d n=%u seq=%u", s_fl_aktif, (unsigned)s_fl_n, (unsigned)s_fl_seq);
    CHECK(log_say(3, 0) == 128, "128 kayit eski->yeni: %d", log_say(3, 0));
    Guvenlik_Init(&g_uart, &g_rtc);
    CHECK(s_fl_aktif == 1 && s_fl_n == 2, "reset: yeni sayfa secildi");

    /* sayfa 1 silinmis ama basligi yazilmamis (guc kesildi) */
    reset_all(0);
    for (unsigned i = 0; i < 127; i++) kayit_yaz(3, TIP_BASLADI, 0, (int16_t)i, 0, 1);
    memset(&g_flash[(s_fl_adr[1] - FLASH_BASE)], 0xFF, FLASH_PAGE_SIZE);
    Guvenlik_Init(&g_uart, &g_rtc);
    CHECK(s_fl_aktif == 0 && s_fl_n == LOG_SLOT, "baslik yoksa eski sayfa");
    kayit_yaz(3, TIP_BITTI, 1, 1000, 0, 1);
    CHECK(s_fl_aktif == 1 && log_say(3, 0) == 128, "devam etti: aktif=%d n=%d", s_fl_aktif, log_say(3, 0));
}

static void t_yabanci_veri(void)
{
    BASLIK("T15 flash'ta yabanci veri / firmware koruma / yazma hatasi");
    reset_all(0);
    memset(&g_flash[(0x1F000 - 0)], 0x00, 4096);                 /* eski modulun ya da cop veri */
    for (int i = 0; i < 4096; i += 3) g_flash[0x1F000 + i] = (uint8_t)(i * 7);
    Guvenlik_Init(&g_uart, &g_rtc);
    CHECK(s_fl_ok && s_fl_aktif == 0 && s_fl_n == 1, "yabanci veri silinip sayfa hazirlandi");
    kayit_yaz(3, TIP_BASLADI, 0, 1, 2, 3);
    CHECK(log_say(0, 0) == 1 && g_fl_viol == 0, "yazma calisiyor");

    reset_all(0);
    g_fw_son = FLASH_BASE + 126u * 1024u;                        /* firmware son sayfalara ulasti */
    Guvenlik_Init(&g_uart, &g_rtc);                              /* (reset_all'daki ilk Init sayaclari kirletti) */
    g_fl_progs = 0; g_fl_erases = 0; g_fl_viol = 0;
    CHECK(!s_fl_ok, "firmware cakismasinda kayit kapanmali");
    sim_s(8); B.hmax = 3330; B.hmin = 3020; sim_s(10);
    CHECK(aktif(3) && g_fl_progs == 0 && g_fl_erases == 0 && g_fl_viol == 0,
          "alarm calisir, flash'a dokunulmaz (prog=%d sil=%d ihlal=%d)", g_fl_progs, g_fl_erases, g_fl_viol);

    reset_all(0); sim_s(8);
    g_inject_prog_fail = 1;
    B.hmax = 3330; B.hmin = 3020; sim_s(10);
    CHECK(!s_fl_ok && aktif(3), "yazma hatasi: kayit kapanir, alarm calismaya devam eder");
    B.hmin = 3320; sim_s(10);
    B.tmax_raw = 111; sim_s(8);
    CHECK(aktif(5) && sayfa() == 102, "kural 5 hala calisiyor");

    /* HAL_OK donup hucreye yazmayan flash (kilitli / bozuk): geri okuma yakalamali */
    reset_all(0); sim_s(8);
    g_inject_prog_silent = 1;
    B.hmax = 3330; B.hmin = 3020; sim_s(10);
    CHECK(!s_fl_ok && aktif(3), "sessiz yazma hatasi geri okumayla yakalanmali");
}

static void t_decode(void)
{
    BASLIK("T16 CAN kare cozumu");
    reset_all(0);
    uint8_t d[8] = { 0x06, 0x01, 0x2B, 0x10, 0x37, 0, 0, 0 };    /* 262 dV, raw 4139 -> +139 dA, soc 55 */
    Guvenlik_Kare(0x2F4, 0, d);
    CHECK(s_in.paket_dV == 262 && s_in.akim_dA == 139 && s_in.soc == 55, "ST1 akim=%d", s_in.akim_dA);
    uint16_t raw = 3750; d[2] = (uint8_t)raw; d[3] = (uint8_t)(raw >> 8);
    Guvenlik_Kare(0x2F5, 0, d);                                  /* F5 adresi de ayni */
    CHECK(s_in.akim_dA == -250, "desarj isareti: %d", s_in.akim_dA);
    uint8_t c[8] = { 0x0A, 0x0E, 5, 0xE0, 0x0B, 8, 0, 0 };      /* 3594 / 3040 mV */
    Guvenlik_Kare(0x4F4, 0, c);
    CHECK(s_in.hmax == 3594 && s_in.hmin == 3040, "hmax=%u hmin=%u", s_in.hmax, s_in.hmin);
    uint8_t t[8] = { 0x48, 6, 0x2F, 1, 0x3F, 0, 0, 0 };          /* 22 C / -3 C */
    Guvenlik_Kare(0x5F4, 0, t);
    CHECK(s_in.tmax_raw == 0x48 && s_in.tmin_raw == 0x2F, "temp");
    uint8_t a[8] = { 0x07, 0x48, 0x47, 0x50, 0xFF, 0xFF, 0, 0 };
    Guvenlik_Kare(0x18F228F5u, 1, a);
    CHECK(s_in.at_mask == 7 && s_in.at_raw[0] == 0x48 && s_in.at_raw[3] == 0xFF, "ALL_TEMP");
    uint8_t s[8] = { 0x3D, 0, 0, 0, 0, 0, 0, 0 };                /* protokol ornegi: sarj MOS 1, desarj MOS 0, fis takili */
    Guvenlik_Kare(0x18F528F4u, 1, s);
    CHECK(s_in.chg_mos == 1 && s_in.dchg_mos == 0 && s_in.plug == 1, "SwSta chg=%d dchg=%d plug=%d", s_in.chg_mos, s_in.dchg_mos, s_in.plug);
    uint32_t sayi = gvn_kare_sayisi;
    uint8_t x[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    gv_in_t onceki = s_in;
    Guvenlik_Kare(0x18F128F4u, 1, x);                            /* BATT_ST2: kullanilmiyor */
    Guvenlik_Kare(0x7F4, 0, x);                                  /* ALM_INFO: kullanilmiyor */
    CHECK(memcmp(&onceki, &s_in, sizeof(s_in)) == 0 && gvn_kare_sayisi == sayi + 2, "ilgisiz kareler durumu bozmamali");
}

static void t_test_kancasi(void)
{
    BASLIK("T17 test kancasi (debugger'dan ekran/buzzer denemesi)");
    for (int r = 1; r <= 8; r++) {
        reset_all(0); sim_s(8);
        gvn_test_maske = (uint8_t)(1u << (r - 1));
        sim_s(2);
        int beklenen_sayfa = (r == 1 || r == 2 || r == 5 || r == 6 || r == 7) ? 102 : (r == 4 || r == 8) ? 101 : 100;
        CHECK(sayfa() == beklenen_sayfa && g_page_cmds == 0, "kural %d: durum=%d beklenen=%d", r, sayfa(), beklenen_sayfa);
        CHECK(r == 3 ? VP_SERIT == GVN_SERIT_K3 : kod() == r, "kural %d: kod=%d serit=%d", r, kod(), VP_SERIT);
        if (beklenen_sayfa == 102)
            CHECK(VP_SERIT == GVN_SERIT_BILGI && mini_hepsi(GVN_MINI_ACIK), "kural %d: pop-up altinda bilgi cubugu", r);
        else
            CHECK(mini_hepsi(GVN_MINI_GIZLI), "kural %d: pop-up yokken rakamlar gizli", r);
        CHECK(log_say(0, 0) == 0, "kural %d: test kancasi log yazmamali", r);
        gvn_test_maske = 0; sim_s(2);
        CHECK(sayfa() == 100 && !Guvenlik_Buzzer_Aktif() && kod() == 0, "kural %d: temizlendi", r);
    }
}

static void t_saat_tasmasi(void)
{
    BASLIK("T18 HAL_GetTick 32-bit tasmasi (49 gun)");
    reset_all(0xFFFFFFFFu - 9000u);
    sim_s(8);
    k1_kur(3710, 0, 60); sim_ms(4800);
    CHECK(!aktif(1), "tasma civarinda 4.8 sn aktif degil");
    sim_ms(1500);
    CHECK(aktif(1) && g_clock < 100000u, "tasma sonrasi aktif, saat=%u", (unsigned)g_clock);
    CHECK(sayfa() == 102, "sayfa gecisi (acilis bekleme tasmada bozulmamali)");
    buz_baslat(); sim_s(2);
    CHECK(g_buz_on == g_buz_n, "surekli buzzer");
    k1_kur(3300, 1, 0); sim_s(5);
    CHECK(!aktif(1), "kapandi");
}

static void t_sd_satiri(void)
{
    BASLIK("T19 SD kuyrugu");
    reset_all(0); sim_s(8);
    B.hmax = 3330; B.hmin = 3020; sim_s(4);
    char buf[300];
    uint16_t n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(n > 0 && strstr(buf, ",K3,Hücre dengesizliği: 3. hücre 3.330 V ile 5. hücre 3.020 V arasında 310 mV fark"
                          " (sınır 300 mV)\n") != NULL && buf[n - 1] == '\n', "okunur satir: %s", n ? buf : "(yok)");
    CHECK(strncmp(buf, "2026.10.05 ", 11) == 0, "tarih: %.19s", buf);
    CHECK(Guvenlik_SD_Satiri(buf, sizeof(buf)) == 0, "kuyruk bos");
    for (int i = 0; i < 12; i++) { kayit_yaz(4, TIP_BASLADI, 0, (int16_t)i, 0, 1); }
    int say = 0; int ilk = -1;
    while ((n = Guvenlik_SD_Satiri(buf, sizeof(buf))) > 0) { if (ilk < 0) { const char *p = strstr(buf, "yüksek: "); ilk = p ? atoi(p + strlen("yüksek: ")) : -1; } say++; }
    CHECK(say == 8 && ilk == 4, "kuyruk 8'de en eskiyi dusurur: say=%d ilk=%d", say, ilk);
    kayit_yaz(2, TIP_BASLADI, 12, 2580, -38, 9);
    n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(strstr(buf, ",K2,Aşırı deşarj: 12. hücre 2.580 V ile 2.600 V sınırının altına düştü / akım -3.8 A\n") != NULL, "K2: %s", buf);
    kayit_yaz(1, TIP_BASLADI, 7, 3712, 60, 97);
    n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(strstr(buf, ",K1,Aşırı şarj: 7. hücre 3.712 V ile 3.700 V sınırını geçti / şarj kesikken akım +6.0 A\n") != NULL, "K1: %s", buf);
    kayit_yaz(1, TIP_BASLADI, 0, 3712, 60, 97);                  /* hucre no bilinmiyor */
    n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(strstr(buf, ",K1,Aşırı şarj: en yüksek hücre 3.712 V ile") != NULL, "K1 no yok: %s", buf);
    kayit_yaz(5, TIP_BASLADI, 0, 62, 50, 70);
    n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(strstr(buf, ",K5,Batarya sıcaklığı çok yüksek: 62 °C (sınır 60 °C)\n") != NULL, "K5: %s", buf);
    kayit_yaz(6, TIP_BASLADI, 0, -12, 5, 70);
    n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(strstr(buf, ",K6,Batarya sıcaklığı çok düşük: -12 °C (sınır -10 °C)\n") != NULL, "K6: %s", buf);
    kayit_yaz(8, TIP_BASLADI, 0, 0x40 | 0x04 | 0x80, 2, 50);
    n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(strstr(buf, ",K8,Sıcaklık sensörü hatası: geçerli sensör sayısı 2 / okunmayan: T3 / BMS sıcaklık değeri geçersiz\n") != NULL,
          "K8: %s", buf);
    kayit_yaz(8, TIP_BASLADI, 0, 0x20, 0, 50);
    n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(strstr(buf, ",K8,Sıcaklık sensörü hatası: BMS sıcaklık verisi göndermiyor\n") != NULL, "K8 veri yok: %s", buf);
    kayit_yaz(7, TIP_BASLADI, 0, 0, 0, 55);
    n = Guvenlik_SD_Satiri(buf, sizeof(buf));
    CHECK(strstr(buf, ",K7,BMS haberleşme hatası: 5 sn'den uzun süre veri gelmedi\n") != NULL, "K7: %s", buf);
    CHECK(strchr(buf, ',') && strchr(strchr(buf, ',') + 1, ',') && !strchr(strchr(strchr(buf, ',') + 1, ',') + 1, ','),
          "satirda tam 3 sutun: %s", buf);
    CHECK(strchr(strchr(buf, ',') + 1, '\n') && strlen(buf) < 300, "satir uzunlugu");
    {                                                             /* en uzun satir tampona sigmali */
        char kucuk[256];
        kayit_yaz(8, TIP_BITTI, 99999, 0xDF, 1, 100);
        n = Guvenlik_SD_Satiri(kucuk, sizeof(kucuk));
        CHECK(n > 0 && n < sizeof(kucuk) - 1 && kucuk[n - 1] == '\n', "en uzun K8 satiri sigdi: %u", n);
    }
    kayit_yaz(2, TIP_BITTI, 123, -250, 77, 9);
    n = Guvenlik_SD_Satiri(buf, 20);
    CHECK(n == 19, "kisa tampon: n=%u", n);
    /* BITTI saati geri alinir (birlestirme beklemesi): gece yarisi, ay, yil, artik yil */
    {
        gv_kayit_t k;
        memset(&k, 0, sizeof(k));
        k.yil = 26; k.ay = 1; k.gun = 1; k.saat = 0; k.dk = 0; k.sn = 30; zaman_geri(&k, 60);
        CHECK(k.yil == 25 && k.ay == 12 && k.gun == 31 && k.saat == 23 && k.dk == 59 && k.sn == 30,
              "yil donumu: %u.%u.%u %u:%u:%u", k.gun, k.ay, k.yil, k.saat, k.dk, k.sn);
        k.yil = 28; k.ay = 3; k.gun = 1; k.saat = 0; k.dk = 0; k.sn = 10; zaman_geri(&k, 20);
        CHECK(k.ay == 2 && k.gun == 29 && k.saat == 23 && k.dk == 59 && k.sn == 50, "artik yil 29 subat: %u.%u", k.gun, k.ay);
        k.yil = 26; k.ay = 3; k.gun = 1; k.saat = 0; k.dk = 0; k.sn = 10; zaman_geri(&k, 20);
        CHECK(k.ay == 2 && k.gun == 28, "normal yil 28 subat: %u.%u", k.gun, k.ay);
        k.yil = 26; k.ay = 10; k.gun = 9; k.saat = 14; k.dk = 31; k.sn = 5; zaman_geri(&k, 3725);
        CHECK(k.gun == 9 && k.saat == 13 && k.dk == 29 && k.sn == 0, "1 sa 2 dk 5 sn geri: %u:%u:%u", k.saat, k.dk, k.sn);
        k.ay = 0; zaman_geri(&k, 10);
        CHECK(k.ay == 0, "saat yoksa dokunulmaz");
    }
}

/* ---------------------------------------------------------------- UART6 kaynagi (Guvenlik_Olcum) */
static gvn_olcum_t U;
static uint32_t    g_u_son;
static void u_saglikli(void)
{
    memset(&U, 0, sizeof(U));
    U.paket_dV = 262; U.akim_dA = 0; U.soc = 60;
    U.hucre_max_mV = 3330; U.hucre_min_mV = 3320;
    U.sicak_max_C = U.sicak_min_C = 25; U.mos_C = 30;
    U.sarj_mos = 1; U.desarj_mos = 1; U.sarj_fisi = -1;
}
/* 20 ms adim; uart_acik iken 400 ms'de bir paket (2400 baud'da 96 bayt ~400 ms) */
static void u_sim_ms(uint32_t ms, int uart_acik)
{
    for (uint32_t i = 0; i < ms / 20u; i++) {
        adim();
        if (uart_acik && (uint32_t)(g_clock - g_u_son) >= 400u) { g_u_son = g_clock; Guvenlik_Olcum(&U); }
    }
}

static void t_uart(void)
{
    BASLIK("T20 UART6 kaynagi: kurallar CAN olmadan da calisir");
    reset_all(0);
    B.st1 = B.cv = B.ct = B.at = B.sw = 0;                        /* CAN hic yok */
    u_saglikli(); g_u_son = 0;
    u_sim_ms(10000, 1);
    CHECK(Guvenlik_Aktif_Maske() == 0 && Guvenlik_Kaynak() == 2, "UART saglikli: kural yok, kaynak 2: maske=%02X kaynak=%u",
          Guvenlik_Aktif_Maske(), Guvenlik_Kaynak());
    CHECK(VP_SERIT == GVN_SERIT_NORMAL && VP_POPUP == 0, "UART verisiyle serit NORMAL: serit=%d", VP_SERIT);
    U.hucre_max_mV = 3720; U.hucre_min_mV = 3600; U.akim_dA = 60; U.sarj_mos = 0;
    u_sim_ms(4600, 1);
    CHECK(!aktif(1), "4.6 sn: kural 1 henuz yok");
    u_sim_ms(1400, 1);
    CHECK(aktif(1) && kod() == 1, "UART'tan kural 1: maske=%02X", Guvenlik_Aktif_Maske());
    CHECK(VP_SERIT == GVN_SERIT_BILGI && mini_hepsi(GVN_MINI_ACIK), "UART verisiyle de alt cubukta canli rakamlar");
    u_saglikli(); u_sim_ms(5000, 1);
    CHECK(!aktif(1), "kural 1 kapanir");
    U.hucre_min_mV = 2580; U.hucre_max_mV = 2700; u_sim_ms(6000, 1);
    CHECK(aktif(2), "UART'tan kural 2");
    u_saglikli(); u_sim_ms(5000, 1);
    U.hucre_max_mV = 3400; U.hucre_min_mV = 3100; u_sim_ms(3000, 1);
    CHECK(aktif(3), "UART'tan kural 3 (fark 300 mV)");
    u_saglikli(); u_sim_ms(5000, 1);
    U.sicak_max_C = U.sicak_min_C = 57; u_sim_ms(3000, 1);
    CHECK(aktif(4) && !aktif(5), "57 C: kural 4, 5 degil");
    U.sicak_max_C = U.sicak_min_C = 62; u_sim_ms(3000, 1);
    CHECK(aktif(5), "62 C: kural 5");
    u_saglikli(); u_sim_ms(8000, 1);
    U.sicak_max_C = U.sicak_min_C = -12; u_sim_ms(3000, 1);
    CHECK(aktif(6), "-12 C: kural 6");
    u_saglikli(); u_sim_ms(8000, 1);
    U.sicak_max_C = U.sicak_min_C = GVN_OLC_NA_C; u_sim_ms(6000, 1);
    CHECK(aktif(8) && !aktif(4) && !aktif(6), "NA sicaklik: kural 8 (4/6 degil): maske=%02X", Guvenlik_Aktif_Maske());
    u_saglikli(); u_sim_ms(8000, 1);
    CHECK(Guvenlik_Aktif_Maske() == 0, "hepsi kapandi: %02X", Guvenlik_Aktif_Maske());
    CHECK(gvn_sensor_adet == 2 && gvn_sicak_ham[1] == 75 && gvn_sicak_ham[3] == 80, "UART 2 sensor (T1 batarya + T3 MOS): adet=%u",
          gvn_sensor_adet);
    U.mos_C = GVN_OLC_NA_C; u_sim_ms(6000, 1);                   /* UART: en az 2 sensor (kullanici karari) */
    CHECK(aktif(8) && gvn_sensor_adet == 1 && gvn_sensor_na == 0x44, "UART MOS sensoru yok: kural 8 (na=0x%02X)", gvn_sensor_na);
    u_saglikli(); u_sim_ms(8000, 1);
    CHECK(!aktif(8), "MOS geri geldi: kural 8 kapanir");
    U.sicak_max_C = U.sicak_min_C = GVN_OLC_YOK_C; u_sim_ms(6000, 1);
    CHECK(aktif(8), "UART'ta batarya sicakligi yok: 1 sensor -> kural 8");
    u_saglikli(); u_sim_ms(8000, 1);
    u_saglikli(); u_sim_ms(2000, 1);
    u_sim_ms(4000, 0);
    CHECK(!aktif(7), "UART 4 sn sustu: henuz kural 7 yok");
    u_sim_ms(1600, 0);
    CHECK(aktif(7) && Guvenlik_Kaynak() == 0, "UART da susunca kural 7: kaynak=%u", Guvenlik_Kaynak());
    u_sim_ms(3000, 1);
    CHECK(!aktif(7), "UART geri gelince kural 7 kalkar");
    CHECK(g_bad_frames == 0 && g_fl_viol == 0 && g_buz_mismatch == 0, "yan hatalar");
}

/* maks / min: BMS'in CELL_TEMP max/min'i + tum gecerli problar (sahada prob -30 C, kural 6 yoktu) */
static void t_maksmin(void)
{
    int16_t mx = 999, mn = 999;
    BASLIK("T22 maks / min sicaklik: BMS max/min + problar (ekran SICAKLIK 1 / 2)");
    reset_all(0); sim_s(8);
    CHECK(Guvenlik_Sicaklik(&mx, &mn) == 3 && mx == (GVN_MOS_DAHIL ? 30 : 25) && mn == 22, "normal: maks %d min %d", mx, mn);
#if GVN_SICAK_TUM_SENSOR
    B.at_raw[1] = 20; sim_s(4);                                   /* T2 -30 C, BMS min hala 22 */
    CHECK(aktif(6) && Guvenlik_Sicaklik(&mx, &mn) == 3 && mn == -30 && mx == (GVN_MOS_DAHIL ? 30 : 25),
          "prob -30 C: kural 6 + min -30 (BMS min'i dusmese de): maks %d min %d", mx, mn);
    B.at_raw[1] = 72; sim_s(6);
    CHECK(!aktif(6), "prob isindi: kural 6 kapanir");
    B.at_raw[0] = 112; sim_s(4);                                  /* T1 62 C, BMS max hala 25 */
    CHECK(aktif(5) && Guvenlik_Sicaklik(&mx, NULL) & 1 && mx == 62, "prob 62 C: kural 5, maks %d", mx);
    B.at_raw[0] = 75; sim_s(6);
#endif
#if !GVN_MOS_DAHIL
    B.at_raw[2] = 125; sim_s(4);                                  /* T3 = MOS 75 C */
    CHECK(!aktif(4) && !aktif(5) && Guvenlik_Sicaklik(&mx, &mn) == 3 && mx == 25, "MOS 75 C maks'a katilmaz: maks %d", mx);
#endif
    /* BMS konusmaya devam ediyor ama sicaklik kareleri kesildi: sessiz kalmamali */
    reset_all(0); sim_s(8);
    B.ct = B.at = 0; sim_s(4);
    mx = mn = 999;
    CHECK(!aktif(7) && Guvenlik_Sicaklik(&mx, &mn) == 0 && mx == 999, "sicaklik verisi bayat: ekrana yazilacak deger yok");
    sim_s(6);
    CHECK(aktif(8) && !aktif(7) && (gvn_sensor_na & 0x20), "sicaklik karesi gelmiyor: kural 8 (na=0x%02X)", gvn_sensor_na);
    CHECK(!aktif(4) && !aktif(5) && !aktif(6), "ve sicaklik alarmlari uydurulmaz");
    B.ct = B.at = 1; sim_s(8);
    CHECK(!aktif(8) && Guvenlik_Sicaklik(&mx, &mn) == 3, "kareler geri geldi: kural 8 kapanir");
    /* ekran: SICAKLIK 2 (0x1040) bilgi cubugunda da gorunur -> pop-up'ta (kural 6 haric) maks */
    reset_all(0); sim_s(8);
    CHECK(Guvenlik_Ekran_Sicaklik(&mx, &mn) == 3 && mn == 22, "pop-up yok: 2 = min (%d)", mn);
    B.tmax_raw = 112; sim_s(4);                                   /* 62 C: kural 5 pop-up */
    CHECK(aktif(5) && VP_POPUP != 0 && Guvenlik_Ekran_Sicaklik(&mx, &mn) == 3 && mx == 62 && mn == 62,
          "kural 5 pop-up: bilgi cubugu maks gosterir (%d / %d)", mx, mn);
    B.tmax_raw = 75; B.tmin_raw = 38; sim_s(12);                  /* -12 C: kural 6 pop-up */
    CHECK(aktif(6) && !aktif(5) && Guvenlik_Ekran_Sicaklik(&mx, &mn) == 3 && mn == -12, "kural 6 pop-up: min (%d)", mn);
    B.tmin_raw = 72; sim_s(12);
    CHECK(!aktif(6) && Guvenlik_Ekran_Sicaklik(&mx, &mn) == 3 && mn == 22, "pop-up kapandi: 2 = min (%d)", mn);
    B.st1 = B.cv = B.ct = B.at = B.sw = 0; sim_s(8);
    mx = mn = 999;
    CHECK(aktif(7) && Guvenlik_Sicaklik(&mx, &mn) == 0 && mx == 999 && mn == 999, "veri yok: gecersiz, cikislar degismez");
}

static void t_can_oncelik(void)
{
    BASLIK("T21 CAN onceligi: CAN canliyken UART yok sayilir, CAN susunca UART devralir");
    reset_all(0);                                                /* CAN saglikli */
    u_saglikli(); U.hucre_max_mV = 3720; U.hucre_min_mV = 3600; U.akim_dA = 60; U.sarj_mos = 0;
    g_u_son = 0;
    u_sim_ms(10000, 1);
    CHECK(!aktif(1) && Guvenlik_Kaynak() == 1, "CAN canli: UART'in kural 1 verisi YOK SAYILIR: maske=%02X kaynak=%u",
          Guvenlik_Aktif_Maske(), Guvenlik_Kaynak());
    B.st1 = B.cv = B.ct = B.at = B.sw = 0;                        /* CAN koptu */
    u_sim_ms(3500, 1);
    CHECK(Guvenlik_Kaynak() == 2, "CAN susunca UART devralir: kaynak=%u", Guvenlik_Kaynak());
    u_sim_ms(6000, 1);
    CHECK(aktif(1) && !aktif(7), "UART verisiyle kural 1, haberlesme hatasi YOK: maske=%02X", Guvenlik_Aktif_Maske());
    bms_saglikli(); u_sim_ms(2000, 1);
    CHECK(Guvenlik_Kaynak() == 1, "CAN geri gelince kaynak 1");
    u_sim_ms(6000, 1);
    CHECK(!aktif(1), "CAN saglikli: kural 1 kapanir (UART'in degeri etkisiz)");
    CHECK(g_bad_frames == 0 && g_fl_viol == 0, "yan hatalar");
}

/* log_coz.py'yi denemek icin gercekci bir flash dokumu uretir: test_guvenlik.exe ornek.bin */
static void ornek_dokum(const char *yol)
{
    FILE *f;
    reset_all(0); sim_s(10);
    k1_kur(3722, 0, 85); sim_s(1000);                            /* kural 1: ~16 dk surer, ignore edilir */
    k1_kur(3300, 1, 0);  sim_s(75);
    B.hmax = 3330; B.hmin = 3018; sim_s(30);                     /* kural 3 */
    B.hmin = 3320; sim_s(75);
    B.tmax_raw = 118; sim_s(300); B.tmax_raw = 76; sim_s(75);    /* kural 4 ve 5: 68 C */
    B.hmin = 2587; B.akim_dA = -120; sim_s(40);                  /* kural 2 */
    B.hmin = 3320; B.akim_dA = 0; sim_s(75);
    B.at_raw[1] = 0xFF; sim_s(600); B.at_raw[1] = 72; sim_s(75); /* kural 8 */
    B.st1 = B.cv = B.ct = B.at = B.sw = 0; sim_s(90);            /* kural 7 */
    bms_saglikli(); sim_s(75);
    f = fopen(yol, "wb");
    if (f) { fwrite(&g_flash[0x1F000], 1, 4096, f); fclose(f); printf("ornek dokum yazildi: %s\n", yol); }
}

int main(int argc, char **argv)
{
    if (argc > 1) { ornek_dokum(argv[1]); return 0; }
    printf("guvenlik.c PC testi   (R1_VEYA=%d, MOS_DAHIL=%d, SENSOR CAN>=%u UART>=%u)\n",
           GVN_R1_VEYA, GVN_MOS_DAHIL, GVN_SENSOR_CAN_ADET, GVN_SENSOR_UART_ADET);
    t_saglikli(); t_k7(); t_k7_ilk_ek(); t_k1(); t_k2(); t_k3(); t_k4k5(); t_k6(); t_k8();
    t_oncelik(); t_iletisim_kaybi(); t_log_devam(); t_log_birlestir();
    t_halka(); t_yeniden_baslat(); t_yabanci_veri(); t_decode();
    t_test_kancasi(); t_saat_tasmasi(); t_sd_satiri(); t_mos(); t_uart(); t_can_oncelik(); t_maksmin();
    printf("\n%s: %d kontrol, %d basarisiz\n", g_fail ? "BASARISIZ" : "TUM TESTLER GECTI", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
