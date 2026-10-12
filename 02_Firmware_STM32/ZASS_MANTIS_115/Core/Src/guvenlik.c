/* ============================================================================
 * ZASS MANTIS - GUVENLIK UYARI MODULU (8 kural)
 * Ayarlar, kural tablosu ve API: guvenlik.h        Entegrasyon: GUVENLIK.md
 * ========================================================================== */
#include "guvenlik.h"
#include <string.h>
#include <stdio.h>

/* ==========================================================================
 * PORT KATMANI - donanima / linker'a dokunan her sey burada.
 * PC testi (test/) GVN_PORT_OVERRIDE tanimlayip bunlari kendi ornekleriyle degistirir.
 * ========================================================================== */
#ifndef GVN_PORT_OVERRIDE
extern uint32_t _sidata, _sdata, _edata;      /* ST linker script'i tanimlar   */
#define GVN_FW_SON()         ((uint32_t)&_sidata + ((uint32_t)&_edata - (uint32_t)&_sdata))
#define GVN_FLASH_KB()       ((uint32_t)(*(const volatile uint16_t *)FLASHSIZE_BASE))
#define GVN_FLASH_OKU(a, d, n) memcpy((d), (const void *)(a), (n))
#endif

/* ==========================================================================
 * KURAL / OLAY SABITLERI
 * ========================================================================== */
enum { K1 = 1, K2, K3, K4, K5, K6, K7, K8, K_N = 8 };

enum { BZ_YOK = 0, BZ_PERIYOT = 1, BZ_SUREKLI = 2 };

/* kural -> buzzer deseni (indeks = kural no) */
static const uint8_t BUZ_DESENI[K_N + 1] = {
    BZ_YOK,                                   /* 0 kullanilmaz */
    BZ_SUREKLI, BZ_SUREKLI, BZ_YOK, BZ_PERIYOT,   /* 1 2 3 4 */
    BZ_SUREKLI, BZ_SUREKLI, BZ_PERIYOT, BZ_PERIYOT /* 5 6 7 8 */
};

/* pop-up sirasi ve ICL 51 'sira' numarasi */
static const uint8_t KRITIK_SIRA[] = { K1, K2, K5, K6, K7 };
static const uint8_t KRITIK_SLOT[K_N + 1] = { 0, 0, 1, 0, 0, 2, 3, 4, 0 };
static const uint8_t UYARI_SIRA[]  = { K4, K8 };

/* kayit tipleri */
enum { TIP_BASLADI = 1, TIP_DEVAM = 2, TIP_BITTI = 3 };

/* BMS kare tipleri (s_in.seen bitleri) */
#define S_ST1  0x01u
#define S_CV   0x02u
#define S_CT   0x04u
#define S_AT   0x08u
#define S_SW   0x10u

#define YOK_C  (-127)           /* sicaklik gecersiz */

/* ==========================================================================
 * GIRIS (kesme icinde yazilir, gorevde kopyalanarak okunur)
 * ========================================================================== */
typedef struct {
    uint32_t t_st1, t_cv, t_ct, t_at, t_sw;   /* son kare zamanlari (HAL_GetTick) */
    uint8_t  seen;                            /* hangi kareler hic geldi          */
    int16_t  akim_dA;                         /* sarj +, desarj -                 */
    uint16_t paket_dV;
    uint8_t  soc;
    uint16_t hmax, hmin;                      /* mV                               */
    uint8_t  hmax_no, hmin_no;                /* hangi hucre (1..), 0 = bilinmiyor */
    uint8_t  tmax_raw, tmin_raw;              /* ham: C + 50, 0xFF = yok          */
    uint8_t  at_mask, at_raw[5];
    uint8_t  at_kaynak;                       /* sensorleri kim yazdi: 1 CAN, 2 UART */
    uint8_t  chg_mos, dchg_mos, plug;         /* SwSta: 1 = kapali (iletiyor)     */
} gv_in_t;

typedef struct { uint8_t st1, cv, ct, at, sw; } taze_t;

/* kuraldan bagimsiz turetilmis olcumler */
typedef struct {
    int16_t hmax, hmin, delta, akim;
    uint8_t hmax_no, hmin_no;
    int16_t tmax, tmin;                       /* YOK_C = gecersiz */
    uint16_t na_maske;                        /* bit6 = sensor sayisi eksik (bit0..4 = bos yuvalar T1..T5),
                                                 bit5 = sicaklik karesi gelmiyor, bit7 = BMS max/min gecersiz */
    uint8_t  sensor_adet;                     /* gecerli sicaklik sensoru sayisi */
    uint8_t cv_ok, tmax_ok, tmin_ok;          /* ilgili olcum gecerli mi */
} turev_t;

/* ==========================================================================
 * DURUM
 * ========================================================================== */
typedef struct {
    uint32_t t_a, t_b;                        /* kurma / birakma sayaci baslangici */
    uint8_t  say_a, say_b, aktif;
} kural_t;

typedef struct {
    uint32_t t_basla, t_bitis, sonraki_sn;
    int16_t  a, b;                            /* olay boyunca tepe olcumler         */
    uint8_t  acik, bitis_bekle;
} olay_t;

static UART_HandleTypeDef *s_uart;
static RTC_HandleTypeDef  *s_rtc;
static uint8_t   s_hazir;
static uint32_t  s_boot_ms;

static gv_in_t   s_in;
static uint32_t  s_can_st1_t;                 /* son CAN BATT_ST1 zamani (CAN onceligi) */
static uint8_t   s_can_gordu;
static uint8_t   s_kaynak_son;                /* s_in'e en son yazan: 1 CAN, 2 UART      */
static kural_t   s_k[K_N + 1];
static olay_t    s_o[K_N + 1];
static uint32_t  s_k3_t0;                     /* kural 3 bildiriminin baslangici */
static uint8_t   s_aktif_maske;
static volatile int16_t s_ekr_tmax, s_ekr_tmin;  /* ekran: son hesaplanan maks / min sicaklik */
static volatile uint8_t s_ekr_ok;                /* bit0 maks gecerli, bit1 min gecerli      */
static volatile uint8_t s_ekr_popup;             /* pop-up'ta gosterilen kural (0 = yok)     */

volatile uint8_t  gvn_test_maske = GVN_TEST_MASKE_ILK;
volatile uint32_t gvn_kare_sayisi;            /* debug: hook kac kare gordu */
/* debug (Live Expressions): kural 8 neden aktif? 0x40 = sensor sayisi eksik (bit0..4 = bos
 * yuvalar T1..T5), 0x20 = sicaklik karesi gelmiyor, 0x80 = CELL_TEMP max/min gecersiz. gvn_sensor_adet = gecerli sensor sayisi
 * (CAN'de >= 3, UART'ta >= 2 olmali). gvn_at_maske = BMS'in TempMaskCode'u. */
volatile uint8_t  gvn_sensor_na;
volatile uint8_t  gvn_sensor_adet;
volatile uint8_t  gvn_at_maske;
/* debug: BMS'ten gelen HAM sicakliklar (C + 50; 0xFF = sensor yok; 70 = 20 C):
 * [0] maske, [1..5] T1..T5, [6] max, [7] min. UART'ta T1 = batarya, T2 = MOS. */
volatile uint8_t  gvn_sicak_ham[8];

/* ==========================================================================
 * DWIN YAZMA (degistiyse yaz; 2 sn'de bir hepsini tazele)
 * ========================================================================== */
static void dw_raw(uint16_t vp, uint16_t v)
{
    uint8_t b[8];
    b[0] = 0x5A; b[1] = 0xA5; b[2] = 0x05; b[3] = 0x82;
    b[4] = (uint8_t)(vp >> 8); b[5] = (uint8_t)(vp & 0xFF);
    b[6] = (uint8_t)(v >> 8);  b[7] = (uint8_t)(v & 0xFF);
    if (s_uart) HAL_UART_Transmit(s_uart, b, 8, 100);
}

#define DW_N 14
static uint16_t s_dw_vp[DW_N], s_dw_v[DW_N];
static uint8_t  s_dw_n;
static uint32_t s_dw_tazele_t;

static void dw(uint16_t vp, uint16_t v)
{
    for (uint8_t i = 0; i < s_dw_n; i++) {
        if (s_dw_vp[i] != vp) continue;
        if (s_dw_v[i] == v) return;
        s_dw_v[i] = v;
        dw_raw(vp, v);
        return;
    }
    if (s_dw_n < DW_N) { s_dw_vp[s_dw_n] = vp; s_dw_v[s_dw_n] = v; s_dw_n++; }
    dw_raw(vp, v);
}

/* ==========================================================================
 * BUZZER
 * ========================================================================== */
static uint8_t  s_buz_sahip, s_buz_desen;
static uint32_t s_buz_t0;

static void buz_yaz(uint8_t on)
{
    GPIO_PinState d = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(GVN_BUZ_A_PORT, GVN_BUZ_A_PIN, d);
    HAL_GPIO_WritePin(GVN_BUZ_B_PORT, GVN_BUZ_B_PIN, d);
}

static void buz_guncelle(uint8_t desen, uint32_t now)
{
    if (desen == BZ_YOK) {
        if (s_buz_sahip) { buz_yaz(0); s_buz_sahip = 0; }
        s_buz_desen = BZ_YOK;
        return;
    }
    if (desen != s_buz_desen) { s_buz_desen = desen; s_buz_t0 = now; }   /* ilk bip hemen */
    s_buz_sahip = 1;
    if (desen == BZ_SUREKLI)
        buz_yaz(1);
    else
        buz_yaz(((uint32_t)(now - s_buz_t0) % GVN_BUZ_PERIYOT_MS) < GVN_BUZ_BIP_MS);
}

/* ==========================================================================
 * KALICI KAYIT: flash halkasi (son 2 sayfa) + SD kuyrugu
 * Kayit 16 B:  imza kural tip soc | yil ay gun saat dk sn | a(i16) b(i16) c(u16)
 *   a,b = olay boyunca tepe olcumler (kurala gore, bkz. olay_olc)
 *   c   = BASLADI'da hucre no (K1: en yuksek, K2: en dusuk, K3: en yuksek<<8 | en dusuk),
 *         DEVAM/BITTI'da olayin suresi (sn)
 * ========================================================================== */
typedef struct __attribute__((packed)) {
    uint8_t  imza, kural, tip, soc;
    uint8_t  yil, ay, gun, saat, dk, sn;
    int16_t  a, b;
    uint16_t c;
} gv_kayit_t;

typedef struct __attribute__((packed)) {
    uint8_t  imza, g, v, ver;
    uint32_t seq;
    uint8_t  pad[8];
} gv_baslik_t;

typedef char gv_boyut_kontrol1[(sizeof(gv_kayit_t) == 16) ? 1 : -1];
typedef char gv_boyut_kontrol2[(sizeof(gv_baslik_t) == 16) ? 1 : -1];

#define KAYIT_IMZA   0xA5u
#define BASLIK_IMZA  0x5Au
#define LOG_SLOT     (FLASH_PAGE_SIZE / 16u)        /* slot 0 = sayfa basligi    */

#if GVN_LOG_FLASH
static uint8_t  s_fl_ok;
static uint32_t s_fl_adr[2];
static uint8_t  s_fl_aktif;
static uint16_t s_fl_n;                              /* siradaki bos slot (1..)   */
static uint32_t s_fl_seq;
#endif

#define SDQ 8u
static gv_kayit_t s_sdq[SDQ];
static uint8_t    s_sd_bas, s_sd_n;

#if GVN_LOG_FLASH
static HAL_StatusTypeDef fl_sil(uint32_t adr)
{
    FLASH_EraseInitTypeDef e;
    uint32_t hata = 0, ofs = adr - FLASH_BASE;
    memset(&e, 0, sizeof(e));
    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.NbPages   = 1;
    e.Banks     = FLASH_BANK_1;
#if defined(FLASH_DBANK_SUPPORT)
    /* Bank sayisini HAL'in kendi makrosu belirler (satis tipi + DUAL_BANK option byte).
     * Cift bank'ta sayfa numarasi BANK ICINDE sayilir: ust yarida Banks=2, ofset yariya gore. */
    if (FLASH_BANK_NB == 2U && ofs >= FLASH_BANK_SIZE) {
        e.Banks = FLASH_BANK_2;
        ofs -= FLASH_BANK_SIZE;
    }
#endif
    e.Page = ofs / FLASH_PAGE_SIZE;
    if (HAL_FLASH_Unlock() != HAL_OK) return HAL_ERROR;
    HAL_StatusTypeDef r = HAL_FLASHEx_Erase(&e, &hata);
    HAL_FLASH_Lock();
    return r;
}

static uint8_t fl_program(uint32_t adr, const void *src)
{
    uint64_t d0, d1;
    uint8_t  ok = 0, chk[16];
    memcpy(&d0, src, 8);
    memcpy(&d1, (const uint8_t *)src + 8, 8);
    if (HAL_FLASH_Unlock() == HAL_OK) {
        ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, adr,     d0) == HAL_OK) &&
             (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, adr + 8, d1) == HAL_OK);
        HAL_FLASH_Lock();
    }
    if (ok) {                                         /* geri oku, dogrula */
        GVN_FLASH_OKU(adr, chk, 16);
        ok = (memcmp(chk, src, 16) == 0);
    }
    return ok;
}

static uint8_t fl_bos_mu(uint32_t adr)
{
    uint8_t b[16];
    for (uint32_t o = 0; o < FLASH_PAGE_SIZE; o += 16u) {
        GVN_FLASH_OKU(adr + o, b, 16);
        for (uint8_t i = 0; i < 16; i++) if (b[i] != 0xFF) return 0;
    }
    return 1;
}

/* sayfayi sil (gerekirse) ve basligini yaz. Basariliysa aktif sayfa yapar. */
static void fl_sayfa_hazirla(uint8_t p, uint32_t seq)
{
    gv_baslik_t h;
    if (!fl_bos_mu(s_fl_adr[p]) && fl_sil(s_fl_adr[p]) != HAL_OK) { s_fl_ok = 0; return; }
    memset(&h, 0, sizeof(h));
    h.imza = BASLIK_IMZA; h.g = 'G'; h.v = 'V'; h.ver = 1; h.seq = seq;
    if (!fl_program(s_fl_adr[p], &h)) { s_fl_ok = 0; return; }
    s_fl_aktif = p; s_fl_n = 1; s_fl_seq = seq;
}

static uint8_t fl_baslik_oku(uint8_t p, gv_baslik_t *h)
{
    GVN_FLASH_OKU(s_fl_adr[p], h, 16);
    return (h->imza == BASLIK_IMZA && h->g == 'G' && h->v == 'V');
}

static void fl_tara(void)
{
    gv_baslik_t h[2];
    uint8_t g0, g1, p;
    uint32_t kb = GVN_FLASH_KB();
    s_fl_ok = 0;
    if (kb < 64u || kb > 1024u) return;
    uint32_t son = FLASH_BASE + kb * 1024UL;
    s_fl_adr[0] = son - 2u * FLASH_PAGE_SIZE;
    s_fl_adr[1] = son - 1u * FLASH_PAGE_SIZE;
    if (GVN_FW_SON() > s_fl_adr[0]) return;           /* firmware buraya ulasti: kayit kapali */
    s_fl_ok = 1;

    g0 = fl_baslik_oku(0, &h[0]);
    g1 = fl_baslik_oku(1, &h[1]);
    if (!g0 && !g1) { fl_sayfa_hazirla(0, 1u); return; }   /* temiz ya da yabanci veri */
    if (g0 && g1) p = ((int32_t)(h[1].seq - h[0].seq) > 0) ? 1u : 0u;
    else          p = g1 ? 1u : 0u;
    s_fl_aktif = p;
    s_fl_seq   = h[p].seq;
    s_fl_n     = LOG_SLOT;                            /* ilk bos slotu bul */
    for (uint16_t i = 1; i < LOG_SLOT; i++) {
        uint8_t b[16], bos = 1;
        GVN_FLASH_OKU(s_fl_adr[p] + (uint32_t)i * 16u, b, 16);
        for (uint8_t j = 0; j < 16; j++) if (b[j] != 0xFF) { bos = 0; break; }
        if (bos) { s_fl_n = i; break; }
    }
}

static void fl_ekle(const gv_kayit_t *k)
{
    if (!s_fl_ok) return;
    if (s_fl_n >= LOG_SLOT) {                         /* sayfa dolu: digerini ac */
        fl_sayfa_hazirla((uint8_t)(1u - s_fl_aktif), s_fl_seq + 1u);
        if (!s_fl_ok) return;
    }
    if (!fl_program(s_fl_adr[s_fl_aktif] + (uint32_t)s_fl_n * 16u, k)) { s_fl_ok = 0; return; }
    s_fl_n++;
}
#endif /* GVN_LOG_FLASH */

static void rtc_al(gv_kayit_t *k)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    uint32_t pm;
    uint8_t ok;
    k->yil = k->ay = k->gun = k->saat = k->dk = k->sn = 0;
    if (!s_rtc) return;
    /* GetTime golge kaydi kilitler, GetDate acar: baska gorev (SD) araya girmesin */
    pm = __get_PRIMASK();
    __disable_irq();
    ok = (uint8_t)(HAL_RTC_GetTime(s_rtc, &t, RTC_FORMAT_BIN) == HAL_OK);
    if (HAL_RTC_GetDate(s_rtc, &d, RTC_FORMAT_BIN) != HAL_OK) ok = 0;
    __set_PRIMASK(pm);
    if (!ok) return;
    k->yil = d.Year; k->ay = d.Month; k->gun = d.Date;
    k->saat = t.Hours; k->dk = t.Minutes; k->sn = t.Seconds;
}

/* kaydin saatini geri_sn kadar geri al (RTC yili 00..99 = 2000..2099; 4'e bolunen yil artik) */
static uint8_t gv_artik(uint32_t y) { return (uint8_t)((y % 4u) == 0u); }
static uint32_t gv_ay_gunu(uint32_t y, uint32_t m)
{
    static const uint8_t G[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (uint32_t)G[m - 1u] + ((m == 2u && gv_artik(y)) ? 1u : 0u);
}
static void zaman_geri(gv_kayit_t *k, uint32_t geri_sn)
{
    uint32_t gun = 0, t, y, m;
    if (geri_sn == 0u || k->ay < 1u || k->ay > 12u || k->gun < 1u) return;
    for (y = 0; y < k->yil; y++) gun += gv_artik(y) ? 366u : 365u;
    for (m = 1; m < k->ay; m++) gun += gv_ay_gunu(k->yil, m);
    gun += (uint32_t)k->gun - 1u;
    t = gun * 86400u + (uint32_t)k->saat * 3600u + (uint32_t)k->dk * 60u + k->sn;
    if (geri_sn > t) return;
    t -= geri_sn;
    gun = t / 86400u; t %= 86400u;
    k->saat = (uint8_t)(t / 3600u); k->dk = (uint8_t)((t / 60u) % 60u); k->sn = (uint8_t)(t % 60u);
    for (y = 0; gun >= (gv_artik(y) ? 366u : 365u); y++) gun -= gv_artik(y) ? 366u : 365u;
    for (m = 1; gun >= gv_ay_gunu(y, m); m++) gun -= gv_ay_gunu(y, m);
    k->yil = (uint8_t)y; k->ay = (uint8_t)m; k->gun = (uint8_t)(gun + 1u);
}

static void kayit_yaz_geri(uint8_t kural, uint8_t tip, uint32_t sure_sn, int16_t a, int16_t b, uint8_t soc,
                           uint32_t geri_sn)
{
    gv_kayit_t k;
    memset(&k, 0, sizeof(k));
    k.imza = KAYIT_IMZA; k.kural = kural; k.tip = tip; k.soc = soc;
    rtc_al(&k);
    zaman_geri(&k, geri_sn);
    k.a = a; k.b = b;
    k.c = (sure_sn > 0xFFFFu) ? 0xFFFFu : (uint16_t)sure_sn;
#if GVN_LOG_FLASH
    fl_ekle(&k);
#endif
    {                                                  /* SD kuyrugu (dolarsa en eski duser) */
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        if (s_sd_n >= SDQ) { s_sd_bas = (uint8_t)((s_sd_bas + 1u) % SDQ); s_sd_n--; }
        s_sdq[(s_sd_bas + s_sd_n) % SDQ] = k;
        s_sd_n++;
        __set_PRIMASK(pm);
    }
}

static void kayit_yaz(uint8_t kural, uint8_t tip, uint32_t sure_sn, int16_t a, int16_t b, uint8_t soc)
{
    kayit_yaz_geri(kural, tip, sure_sn, a, b, soc, 0u);
}

uint8_t Guvenlik_SD_Bekleyen(void)
{
    return s_sd_n;                                     /* tek bayt: atomik okunur */
}

/* ---- SD olay satiri: "Tarih Saat,Kod,Olay" - olay olunca NE oldu, NE ZAMAN, hangi hucre.
 * UTF-8 (dosyada BOM var, Excel Turkce gosterir); metinde virgul YOK, ondalik ayirac nokta. */
static void volt_metin(char *b, size_t n, int16_t mv)          /* 3712 -> "3.712 V" */
{
    int32_t v = mv, m = (v < 0) ? -v : v;
    (void)snprintf(b, n, "%s%ld.%03ld V", (v < 0) ? "-" : "", (long)(m / 1000), (long)(m % 1000));
}

static void akim_metin(char *b, size_t n, int16_t dA)          /* 0.1 A -> "+6.0 A" */
{
    int32_t v = dA, m = (v < 0) ? -v : v;
    (void)snprintf(b, n, "%s%ld.%ld A", (v < 0) ? "-" : "+", (long)(m / 10), (long)(m % 10));
}

static void hucre_metin(char *b, size_t n, unsigned no, const char *yoksa)
{
    if (no) (void)snprintf(b, n, "%u. hücre", no); else (void)snprintf(b, n, "%s", yoksa);
}

static void olay_metin(char *b, size_t n, const gv_kayit_t *k)
{
    char v1[16], v2[16], h1[24], h2[24];
    size_t u;
    switch (k->kural) {
    case K1:
        hucre_metin(h1, sizeof(h1), k->c, "en yüksek hücre");
        volt_metin(v1, sizeof(v1), k->a); volt_metin(v2, sizeof(v2), GVN_R1_HUCRE_MV); akim_metin(h2, sizeof(h2), k->b);
        (void)snprintf(b, n, "Aşırı şarj: %s %s ile %s sınırını geçti / şarj kesikken akım %s", h1, v1, v2, h2);
        break;
    case K2:
        hucre_metin(h1, sizeof(h1), k->c, "en düşük hücre");
        volt_metin(v1, sizeof(v1), k->a); volt_metin(v2, sizeof(v2), GVN_R2_HUCRE_MV); akim_metin(h2, sizeof(h2), k->b);
        (void)snprintf(b, n, "Aşırı deşarj: %s %s ile %s sınırının altına düştü / akım %s", h1, v1, v2, h2);
        break;
    case K3:
        hucre_metin(h1, sizeof(h1), (unsigned)(k->c >> 8), "en yüksek hücre");
        hucre_metin(h2, sizeof(h2), (unsigned)(k->c & 0xFFu), "en düşük hücre");
        volt_metin(v1, sizeof(v1), k->b); volt_metin(v2, sizeof(v2), (int16_t)(k->b - k->a));
        (void)snprintf(b, n, "Hücre dengesizliği: %s %s ile %s %s arasında %d mV fark (sınır %d mV)",
                       h1, v1, h2, v2, (int)k->a, (int)GVN_R3_DELTA_MV);
        break;
    case K4: (void)snprintf(b, n, "Batarya sıcaklığı yüksek: %d °C (sınır %d °C)", (int)k->a, (int)GVN_R4_SICAK_C); break;
    case K5: (void)snprintf(b, n, "Batarya sıcaklığı çok yüksek: %d °C (sınır %d °C)", (int)k->a, (int)GVN_R5_SICAK_C); break;
    case K6: (void)snprintf(b, n, "Batarya sıcaklığı çok düşük: %d °C (sınır %d °C)", (int)k->a, (int)GVN_R6_SOGUK_C); break;
    case K7: (void)snprintf(b, n, "BMS haberleşme hatası: %lu sn'den uzun süre veri gelmedi",
                            (unsigned long)(GVN_R7_SURE_MS / 1000u)); break;
    case K8:
        if (k->a & 0x20) { (void)snprintf(b, n, "Sıcaklık sensörü hatası: BMS sıcaklık verisi göndermiyor"); break; }
        (void)snprintf(b, n, "Sıcaklık sensörü hatası: geçerli sensör sayısı %d", (int)k->b);
        if (k->a & 0x1F) {
            u = strlen(b);
            (void)snprintf(b + u, (u < n) ? n - u : 0u, " / okunmayan:");
            for (uint8_t i = 0; i < 5u; i++) {
                u = strlen(b);
                if (k->a & (1 << i)) (void)snprintf(b + u, (u < n) ? n - u : 0u, " T%u", (unsigned)(i + 1u));
            }
        }
        if (k->a & 0x80) { u = strlen(b); (void)snprintf(b + u, (u < n) ? n - u : 0u, " / BMS sıcaklık değeri geçersiz"); }
        break;
    default: (void)snprintf(b, n, "Bilinmeyen olay %u", (unsigned)k->kural); break;
    }
#if !GVN_LOG_SADECE_OLAY
    if (k->tip != TIP_BASLADI) {                       /* ayrintili kip: devam / bitti + sure */
        u = strlen(b);
        (void)snprintf(b + u, (u < n) ? n - u : 0u, " - %s (%lu sn)", (k->tip == TIP_BITTI) ? "bitti" : "devam ediyor",
                       (unsigned long)k->c);
    }
#endif
}

uint16_t Guvenlik_SD_Satiri(char *buf, uint16_t boyut)
{
    char metin[176];
    gv_kayit_t k;
    int n;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    if (s_sd_n == 0) { __set_PRIMASK(pm); return 0; }
    k = s_sdq[s_sd_bas];
    s_sd_bas = (uint8_t)((s_sd_bas + 1u) % SDQ);
    s_sd_n--;
    __set_PRIMASK(pm);
    olay_metin(metin, sizeof(metin), &k);
    n = snprintf(buf, boyut, "20%02u.%02u.%02u %02u:%02u:%02u,K%u,%s\n",
                 (unsigned)k.yil, (unsigned)k.ay, (unsigned)k.gun,
                 (unsigned)k.saat, (unsigned)k.dk, (unsigned)k.sn, (unsigned)k.kural, metin);
    if (n < 0) return 0;
    return (n >= (int)boyut) ? (uint16_t)(boyut - 1u) : (uint16_t)n;
}

/* ==========================================================================
 * OLAY TAKIBI (log): BASLADI -> [DEVAM...] -> BITTI
 * Ekran kosulu kalkinca BITTI hemen yazilmaz, 60 sn beklenir; bu surede ayni
 * olay tekrar baslarsa BIRLESTIRILIR (flash'i bogmaz, tek olay olarak kalir).
 * ========================================================================== */
/* donus: 1 = olcum gecerli (tepe takibine katilabilir) */
static uint8_t olay_olc(uint8_t r, const turev_t *t, int16_t *a, int16_t *b)
{
    switch (r) {
    case K1: *a = t->hmax;  *b = t->akim; return t->cv_ok;
    case K2: *a = t->hmin;  *b = t->akim; return t->cv_ok;
    case K3: *a = t->delta; *b = t->hmax; return t->cv_ok;
    case K4: case K5: *a = t->tmax; *b = t->tmin; return t->tmax_ok;
    case K6: *a = t->tmin;  *b = t->tmax; return t->tmin_ok;
    case K8: *a = (int16_t)t->na_maske; *b = (int16_t)t->sensor_adet; return 1u;
    default: *a = 0; *b = 0; return 1u;
    }
}

/* tepe yonu: 1 = buyuk olan tepe, 0 = kucuk olan tepe, 2 = maske (OR) */
static uint8_t olay_yon(uint8_t r)
{
    return (r == K2 || r == K6) ? 0u : (r == K8) ? 2u : 1u;
}

static void olay_basla(uint8_t r, uint32_t now, const turev_t *t, uint8_t soc)
{
    olay_t *o = &s_o[r];
    if (o->acik && o->bitis_bekle) { o->bitis_bekle = 0; return; }      /* birlestir */
    o->acik = 1; o->bitis_bekle = 0;
    o->t_basla = now; o->sonraki_sn = 60u;
    olay_olc(r, t, &o->a, &o->b);
    kayit_yaz(r, TIP_BASLADI,
              (r == K1) ? t->hmax_no : (r == K2) ? t->hmin_no :
              (r == K3) ? (uint32_t)(((uint32_t)t->hmax_no << 8) | t->hmin_no) : 0u,
              o->a, o->b, soc);
}

static void olay_bitir(uint8_t r, uint32_t now)
{
    olay_t *o = &s_o[r];
    if (!o->acik) return;
    o->bitis_bekle = 1; o->t_bitis = now;
}

static void olay_zaman(uint8_t r, uint32_t now, const turev_t *t, uint8_t soc)
{
    olay_t *o = &s_o[r];
    int16_t a, b;
    if (!o->acik) return;
    if (s_k[r].aktif) {
        uint8_t y = olay_yon(r);
        if (olay_olc(r, t, &a, &b)) {
            if ((y == 1u && a > o->a) || (y == 0u && a < o->a)) { o->a = a; o->b = b; }
            else if (y == 2u) o->a = (int16_t)(o->a | a);
        }
        if (!GVN_LOG_SADECE_OLAY && (GVN_LOG_DEVAM_MASKE & (1u << (r - 1u)))) {
            uint32_t sure = (uint32_t)(now - o->t_basla) / 1000u;
            if (sure >= o->sonraki_sn) {
                kayit_yaz(r, TIP_DEVAM, sure, o->a, o->b, soc);
                o->sonraki_sn = (o->sonraki_sn < 60u)   ? 60u   :
                                (o->sonraki_sn < 300u)  ? 300u  :
                                (o->sonraki_sn < 900u)  ? 900u  :
                                (o->sonraki_sn < 3600u) ? 3600u : o->sonraki_sn + 3600u;
                while (o->sonraki_sn <= sure) o->sonraki_sn += 3600u;
            }
        }
    } else if (o->bitis_bekle && (uint32_t)(now - o->t_bitis) >= GVN_LOG_BIRLESTIR_MS) {
#if !GVN_LOG_SADECE_OLAY
        kayit_yaz_geri(r, TIP_BITTI, (uint32_t)(o->t_bitis - o->t_basla) / 1000u, o->a, o->b, soc,
                       (uint32_t)(now - o->t_bitis) / 1000u);    /* saat = olayin gercekten bittigi an */
#endif
        o->acik = 0; o->bitis_bekle = 0;
    }
}

/* ==========================================================================
 * KURAL MOTORU: kur / birak gecikmeli, uc durumlu (dogru / yanlis / bilinmiyor)
 * ========================================================================== */
/* +1 = aktif oldu, -1 = pasif oldu, 0 = degisim yok */
static int8_t kural_adim(kural_t *k, int8_t sart, uint32_t kur_ms, uint32_t birak_ms, uint32_t now)
{
    if (sart < 0) { k->say_a = 0; k->say_b = 0; return 0; }   /* veri bayat: durum korunur */
    if (sart) {
        k->say_b = 0;
        if (k->aktif) return 0;
        if (!k->say_a) { k->say_a = 1; k->t_a = now; }
        if ((uint32_t)(now - k->t_a) >= kur_ms) { k->aktif = 1; k->say_a = 0; return 1; }
    } else {
        k->say_a = 0;
        if (!k->aktif) return 0;
        if (!k->say_b) { k->say_b = 1; k->t_b = now; }
        if ((uint32_t)(now - k->t_b) >= birak_ms) { k->aktif = 0; k->say_b = 0; return -1; }
    }
    return 0;
}

static int8_t ve3(int8_t a, int8_t b)
{
    if (a == 0 || b == 0) return 0;
    if (a < 0 || b < 0)   return -1;
    return 1;
}
static int8_t veya3(int8_t a, int8_t b)
{
    if (a == 1 || b == 1) return 1;
    if (a < 0 || b < 0)   return -1;
    return 0;
}

static uint8_t sicak_gecerli(uint8_t raw)
{
    return (uint8_t)(raw >= (uint8_t)(GVN_SICAK_GECERLI_ALT + 50) &&
                     raw <= (uint8_t)(GVN_SICAK_GECERLI_UST + 50));
}

static uint8_t cv_gecerli(const gv_in_t *v)
{
    return (uint8_t)(v->hmin > 0u && v->hmax >= v->hmin && v->hmax <= 5000u);
}

/* BMS ham verisinden kurallarin kullandigi olcumleri cikar */
static void turet(const gv_in_t *v, const taze_t *z, turev_t *t)
{
    t->hmax = t->hmin = t->delta = 0;
    t->hmax_no = t->hmin_no = 0;
    t->akim = v->akim_dA;
    t->tmax = t->tmin = YOK_C;
    t->na_maske = 0;
    t->cv_ok = t->tmax_ok = t->tmin_ok = 0;
    if (z->cv && cv_gecerli(v)) {
        t->hmax = (int16_t)v->hmax; t->hmin = (int16_t)v->hmin;
        t->delta = (int16_t)(v->hmax - v->hmin);
        t->hmax_no = v->hmax_no; t->hmin_no = v->hmin_no;
        t->cv_ok = 1;
    }
    if (z->ct) {
        if (sicak_gecerli(v->tmax_raw)) { t->tmax = (int16_t)v->tmax_raw - 50; t->tmax_ok = 1; }
        if (sicak_gecerli(v->tmin_raw)) { t->tmin = (int16_t)v->tmin_raw - 50; t->tmin_ok = 1; }
    }
#if GVN_SICAK_TUM_SENSOR
    /* Problarin kendisi de katilir: sahada (2026-10-09) prob -30 C'ye sogutuldugu halde
     * BMS'in CELL_TEMP min'i dusmedi, kural 6 tetiklenmedi. T3 = MOS, GVN_MOS_DAHIL ile. */
    if (z->at) {
        for (uint8_t i = 0; i < 5u; i++) {
            int16_t m;
            if ((i == 2u && !GVN_MOS_DAHIL) || !sicak_gecerli(v->at_raw[i])) continue;
            m = (int16_t)v->at_raw[i] - 50;
            if (!t->tmax_ok || m > t->tmax) { t->tmax = m; t->tmax_ok = 1; }
            if (!t->tmin_ok || m < t->tmin) { t->tmin = m; t->tmin_ok = 1; }
        }
    }
#elif GVN_MOS_DAHIL
    if (z->at && sicak_gecerli(v->at_raw[2])) {
        int16_t m = (int16_t)v->at_raw[2] - 50;
        if (!t->tmax_ok || m > t->tmax) { t->tmax = m; t->tmax_ok = 1; }
    }
#endif
    t->sensor_adet = 0;
    if (z->at) {
        uint8_t gecerli = 0, gerekli;
        for (uint8_t i = 0; i < 5u; i++) {
            uint8_t bit = (uint8_t)(1u << i);
            if (sicak_gecerli(v->at_raw[i])) { gecerli |= bit; t->sensor_adet++; }
        }
        gerekli = (v->at_kaynak == 2u) ? (uint8_t)GVN_SENSOR_UART_ADET : (uint8_t)GVN_SENSOR_CAN_ADET;
        if (t->sensor_adet < gerekli)               /* eksik sensor: hangi yuvalar bos + bit6 */
            t->na_maske |= (uint16_t)(0x40u | ((uint8_t)~gecerli & ((v->at_kaynak == 2u) ? 0x05u : 0x07u)));
    }
    if (z->ct && (!sicak_gecerli(v->tmax_raw) || !sicak_gecerli(v->tmin_raw)))
        t->na_maske |= 0x80u;                       /* BMS'in max/min'i gecersiz */
    if (z->st1 && !z->ct && !z->at)
        t->na_maske |= 0x20u;                       /* BMS konusuyor ama sicaklik karesi gelmiyor */
}

/* ---- kurallar: donus 1 dogru / 0 yanlis / -1 bilinmiyor ---- */
static int8_t kosul_k1(const gv_in_t *v, const taze_t *z, uint8_t aktif)
{
    int16_t es_h = aktif ? GVN_R1_HUCRE_BIRAK : GVN_R1_HUCRE_MV;
    int16_t es_a = aktif ? GVN_R1_AKIM_BIRAK_dA : GVN_R1_AKIM_dA;
    int8_t  hv  = (z->cv && cv_gecerli(v)) ? (int8_t)(v->hmax > (uint16_t)es_h) : (int8_t)-1;
    int8_t  kes = (z->st1 && z->sw) ? (int8_t)(v->chg_mos == 0u && v->akim_dA > es_a) : (int8_t)-1;
#if GVN_R1_VEYA
    return veya3(hv, kes);
#else
    return ve3(hv, kes);
#endif
}

static int8_t kosul_k2(const gv_in_t *v, const taze_t *z, uint8_t aktif)
{
    int16_t es_h = aktif ? GVN_R2_HUCRE_BIRAK : GVN_R2_HUCRE_MV;
    int16_t es_a = aktif ? GVN_R2_AKIM_BIRAK_dA : GVN_R2_AKIM_dA;
    int8_t  dv  = (z->cv && cv_gecerli(v)) ? (int8_t)(v->hmin < (uint16_t)es_h) : (int8_t)-1;
    int8_t  kes = (z->st1 && z->sw) ? (int8_t)(v->dchg_mos == 0u && v->akim_dA < es_a) : (int8_t)-1;
    return veya3(dv, kes);
}

static int8_t kosul_k3(const turev_t *t, const taze_t *z, const gv_in_t *v, uint8_t aktif)
{
    if (!(z->cv && cv_gecerli(v))) return -1;
    return (int8_t)(t->delta >= (aktif ? GVN_R3_DELTA_BIRAK_MV : GVN_R3_DELTA_MV));
}

static int8_t kosul_sicak(const turev_t *t, uint8_t aktif, int16_t es, int16_t birak)
{
    if (t->tmax == YOK_C) return -1;
    return (int8_t)(t->tmax > (aktif ? birak : es));
}

static int8_t kosul_k6(const turev_t *t, uint8_t aktif)
{
    if (t->tmin == YOK_C) return -1;
    return (int8_t)(t->tmin < (aktif ? GVN_R6_BIRAK_C : GVN_R6_SOGUK_C));
}

static int8_t kosul_k8(const turev_t *t, const taze_t *z)
{
    if (!z->ct && !z->at) return z->st1 ? (int8_t)1 : (int8_t)-1;   /* sicaklik verisi yok */
    return (int8_t)(t->na_maske != 0u);
}

/* kurali ilerlet + log kancalari */
static void uygula(uint8_t r, int8_t sart, uint32_t kur_ms, uint32_t birak_ms,
                   uint32_t now, const turev_t *t, uint8_t soc)
{
    int8_t d = kural_adim(&s_k[r], sart, kur_ms, birak_ms, now);
    if (d > 0) {
        if (r == K3) s_k3_t0 = now;
        olay_basla(r, now, t, soc);
    } else if (d < 0) {
        olay_bitir(r, now);
    }
}

static void zorla_kapat(uint8_t r, uint32_t now)
{
    kural_t *k = &s_k[r];
    if (k->aktif) olay_bitir(r, now);
    k->aktif = 0; k->say_a = 0; k->say_b = 0;
}

/* ==========================================================================
 * ANA GOREV
 * ========================================================================== */
void Guvenlik_Task(void)
{
    gv_in_t  v;
    taze_t   z;
    turev_t  t;
    uint32_t now, yas, son, pm, esik7;
    uint8_t  gor[K_N + 1];
    uint8_t  r, desen, nk, nu, goster;
    uint16_t serit, popup, mini;
    uint8_t  kr[sizeof(KRITIK_SIRA)], uy[sizeof(UYARI_SIRA)];

    if (!s_hazir) return;
    now = HAL_GetTick();

    pm = __get_PRIMASK();                              /* tutarli kopya */
    __disable_irq();
    v = s_in;
    __set_PRIMASK(pm);

    z.st1 = (uint8_t)((v.seen & S_ST1) && (uint32_t)(now - v.t_st1) <= GVN_TAZE_MS);
    z.cv  = (uint8_t)((v.seen & S_CV)  && (uint32_t)(now - v.t_cv)  <= GVN_TAZE_MS);
    z.ct  = (uint8_t)((v.seen & S_CT)  && (uint32_t)(now - v.t_ct)  <= GVN_TAZE_MS);
    z.at  = (uint8_t)((v.seen & S_AT)  && (uint32_t)(now - v.t_at)  <= GVN_TAZE_MS);
    z.sw  = (uint8_t)((v.seen & S_SW)  && (uint32_t)(now - v.t_sw)  <= GVN_TAZE_MS);
    turet(&v, &z, &t);
    s_ekr_tmax = t.tmax; s_ekr_tmin = t.tmin;
    s_ekr_ok   = (uint8_t)((t.tmax_ok ? 1u : 0u) | (t.tmin_ok ? 2u : 0u));
    gvn_sensor_na = (uint8_t)t.na_maske;
    gvn_sensor_adet = t.sensor_adet;
    gvn_at_maske  = v.at_mask;
    gvn_sicak_ham[0] = v.at_mask;
    for (r = 0; r < 5u; r++) gvn_sicak_ham[1u + r] = v.at_raw[r];
    gvn_sicak_ham[6] = v.tmax_raw;
    gvn_sicak_ham[7] = v.tmin_raw;

    if ((uint32_t)(now - s_dw_tazele_t) >= 2000u) { s_dw_tazele_t = now; s_dw_n = 0; }

    /* --- 7: haberlesme (kendi yasi uzerinden; en yuksek oncelikli veri kaybi) --- */
    son = (v.seen & S_ST1) ? v.t_st1 : s_boot_ms;
    yas = (uint32_t)(now - son);
    esik7 = GVN_R7_SURE_MS + ((v.seen & S_ST1) ? 0u : GVN_R7_ILK_EK_MS);
    uygula(K7, (int8_t)(s_k[K7].aktif ? (yas > 1000u) : (yas > esik7)),
           0u, GVN_R7_TOPARLA_MS, now, &t, v.soc);

    if (s_k[K7].aktif) {
        /* veri bayat: veriye dayanan kurallar anlamsiz, kapat */
        zorla_kapat(K1, now); zorla_kapat(K2, now); zorla_kapat(K3, now);
        zorla_kapat(K4, now); zorla_kapat(K5, now); zorla_kapat(K6, now);
        zorla_kapat(K8, now);
    } else {
        uygula(K1, kosul_k1(&v, &z, s_k[K1].aktif), GVN_R1_SURE_MS, GVN_BIRAK_MS, now, &t, v.soc);
        uygula(K2, kosul_k2(&v, &z, s_k[K2].aktif), GVN_R2_SURE_MS, GVN_BIRAK_MS, now, &t, v.soc);
        uygula(K3, kosul_k3(&t, &z, &v, s_k[K3].aktif), GVN_R3_SURE_MS, GVN_BIRAK_MS, now, &t, v.soc);
        uygula(K4, kosul_sicak(&t, s_k[K4].aktif, GVN_R4_SICAK_C, GVN_R4_BIRAK_C),
               GVN_R456_SURE_MS, GVN_BIRAK_MS, now, &t, v.soc);
        uygula(K5, kosul_sicak(&t, s_k[K5].aktif, GVN_R5_SICAK_C, GVN_R5_BIRAK_C),
               GVN_R456_SURE_MS, GVN_BIRAK_MS, now, &t, v.soc);
        uygula(K6, kosul_k6(&t, s_k[K6].aktif), GVN_R456_SURE_MS, GVN_BIRAK_MS, now, &t, v.soc);
        uygula(K8, kosul_k8(&t, &z), GVN_R8_SURE_MS, GVN_R8_SURE_MS, now, &t, v.soc);
    }

    s_aktif_maske = 0;
    for (r = 1; r <= K_N; r++) {
        olay_zaman(r, now, &t, v.soc);
        if (s_k[r].aktif) s_aktif_maske |= (uint8_t)(1u << (r - 1u));
    }

    /* --- ekranda gosterilecekler (test kancasi dahil) --- */
    for (r = 1; r <= K_N; r++) {
        gor[r] = s_k[r].aktif;
#if GVN_TEST_KANCASI
        if ((gvn_test_maske >> (r - 1u)) & 1u) gor[r] = 1;
#endif
    }

    nk = 0; nu = 0;
    for (uint8_t i = 0; i < sizeof(KRITIK_SIRA); i++) if (gor[KRITIK_SIRA[i]]) kr[nk++] = KRITIK_SIRA[i];
    for (uint8_t i = 0; i < sizeof(UYARI_SIRA);  i++) if (gor[UYARI_SIRA[i]])  uy[nu++] = UYARI_SIRA[i];

    /* --- pop-up (kritik): ana sayfanin ustune biner, kare 0 = seffaf --- */
    popup = GVN_POPUP_YOK;
    if (nk) {
        goster = kr[((uint32_t)(now - s_boot_ms) / GVN_DONGU_MS) % nk];
        popup  = (uint16_t)(1u + KRITIK_SLOT[goster] * 2u + ((now / GVN_BLINK_MS) & 1u));
    }
    s_ekr_popup = nk ? goster : 0u;

    /* --- serit (sicaklik kartlarinin ustu): uyari > kural 3 > normal --- */
    if (nu) {
        goster = uy[((uint32_t)(now - s_boot_ms) / GVN_DONGU_MS) % nu];
        serit  = (goster == K4) ? GVN_SERIT_K4 : GVN_SERIT_K8;
    } else if (gor[K3] && ((uint32_t)(now - s_k3_t0) < GVN_R3_GOSTER_MS || !s_k[K3].aktif)) {
        serit  = GVN_SERIT_K3;                     /* (test kancasinda 15 dk siniri yok) */
    } else if ((v.seen & S_ST1) && s_aktif_maske == 0u && !nk) {
        serit  = GVN_SERIT_NORMAL;                 /* sadece HICBIR kural aktif degilken */
    } else {
        serit  = GVN_SERIT_BOS;                    /* BMS verisi yok, ya da kural 3 suresi doldu */
    }

    /* --- bilgi cubugu: pop-up acikken serit yerine canli SOC/V/A/C ---
     * Veri yoksa (kural 7) bayat rakam gostermek yaniltir: rakamlar gizli kalir.       */
    mini = GVN_MINI_GIZLI;
#if GVN_BILGI_CUBUGU
    if (nk) {
        if (z.st1 && !s_k[K7].aktif) { serit = GVN_SERIT_BILGI; mini = GVN_MINI_ACIK; }
        else                           serit = GVN_SERIT_VERIYOK;
    }
    dw(GVN_SP_MINI_SOC + GVN_MINI_RENK_OFS, mini);
    dw(GVN_SP_MINI_V   + GVN_MINI_RENK_OFS, mini);
    dw(GVN_SP_MINI_A   + GVN_MINI_RENK_OFS, mini);
    dw(GVN_SP_MINI_T   + GVN_MINI_RENK_OFS, mini);
#if GVN_MINI_KONUMLA_GIZLE
    {
        uint16_t y = (mini == GVN_MINI_ACIK) ? (uint16_t)GVN_MINI_Y : (uint16_t)GVN_MINI_Y_GIZLI;
        dw(GVN_SP_MINI_SOC + GVN_MINI_Y_OFS, y);
        dw(GVN_SP_MINI_V   + GVN_MINI_Y_OFS, y);
        dw(GVN_SP_MINI_A   + GVN_MINI_Y_OFS, y);
        dw(GVN_SP_MINI_T   + GVN_MINI_Y_OFS, y);
    }
#endif
#endif
    dw(GVN_VP_SERIT, serit);
    dw(GVN_VP_POPUP, popup);
#if GVN_EKRAN_YOKLAMA
    dw_raw(0x1000, (uint16_t)(popup ? 90u + popup : 11u));   /* gecici tani: mevcut SOC kontrolunun ustune */
#endif

    /* --- buzzer: en baskin desen --- */
    desen = BZ_YOK;
    for (r = 1; r <= K_N; r++)
        if (gor[r] && BUZ_DESENI[r] > desen) desen = BUZ_DESENI[r];
    buz_guncelle(desen, now);
}

uint8_t Guvenlik_Buzzer_Aktif(void) { return s_buz_sahip; }
uint8_t Guvenlik_Aktif_Maske(void)  { return s_aktif_maske; }

uint8_t Guvenlik_Hat_Maske(void)
{
    uint32_t now = HAL_GetTick(), pm = __get_PRIMASK();
    gv_in_t v;
    uint8_t m = 0;
    __disable_irq();
    v = s_in;
    __set_PRIMASK(pm);
    if ((v.seen & S_ST1) && (uint32_t)(now - v.t_st1) <= GVN_TAZE_MS) m |= 0x01u;
    if ((v.seen & S_CV)  && (uint32_t)(now - v.t_cv)  <= GVN_TAZE_MS) m |= 0x02u;
    if ((v.seen & S_CT)  && (uint32_t)(now - v.t_ct)  <= GVN_TAZE_MS) m |= 0x04u;
    if ((v.seen & S_AT)  && (uint32_t)(now - v.t_at)  <= GVN_TAZE_MS) m |= 0x08u;
    if ((v.seen & S_SW)  && (uint32_t)(now - v.t_sw)  <= GVN_TAZE_MS) m |= 0x10u;
    return m;
}

/* ==========================================================================
 * CAN KARE HOOK'U (kesme baglaminda): sadece cozer ve zaman damgasi basar
 * Kare ID'leri protokol V2.1; JK/ZASS cihaz adresi 0 (F4) ve 1 (F5) ayni sayilir.
 * ========================================================================== */
void Guvenlik_Kare(uint32_t id, uint32_t ext, const uint8_t *d)
{
    uint32_t t = HAL_GetTick();
    gvn_kare_sayisi++;
    if (!ext) {
        switch (id & 0x700u) {
        case 0x200u:                                  /* BATT_ST1 0x02F4 */
            s_in.paket_dV = (uint16_t)(d[0] | (d[1] << 8));
            s_in.akim_dA  = (int16_t)((int32_t)(d[2] | (d[3] << 8)) - 4000);   /* 0.1 A, ofset -400 A */
            s_in.soc      = d[4];
            s_in.t_st1 = t; s_in.seen |= S_ST1;
            s_can_st1_t = t; s_can_gordu = 1; s_kaynak_son = 1;
            break;
        case 0x400u:                                  /* CELL_VOLT 0x04F4 */
            s_in.hmax = (uint16_t)(d[0] | (d[1] << 8));
            s_in.hmin = (uint16_t)(d[3] | (d[4] << 8));
            s_in.hmax_no = d[2]; s_in.hmin_no = d[5];      /* protokol: en yuksek / en dusuk hucre no */
            s_in.t_cv = t; s_in.seen |= S_CV;
            break;
        case 0x500u:                                  /* CELL_TEMP 0x05F4 */
            s_in.tmax_raw = d[0];
            s_in.tmin_raw = d[2];
            s_in.t_ct = t; s_in.seen |= S_CT;
            break;
        default:
            break;
        }
    } else {
        switch (id & 0x1FFFFF00u) {
        case 0x18F22800u:                             /* ALL_TEMP */
            s_in.at_mask = d[0];
            for (uint8_t i = 0; i < 5u; i++) s_in.at_raw[i] = d[1u + i];
            s_in.at_kaynak = 1u;
            s_in.t_at = t; s_in.seen |= S_AT;
            break;
        case 0x18F52800u:                             /* BmsSwSta */
            s_in.chg_mos  = (uint8_t)(d[0] & 1u);
            s_in.dchg_mos = (uint8_t)((d[0] >> 1) & 1u);
            s_in.plug     = (uint8_t)((d[0] >> 4) & 1u);
            s_in.t_sw = t; s_in.seen |= S_SW;
            break;
        default:
            break;
        }
    }
}

/* ==========================================================================
 * CAN DISI KAYNAK (UART6 BMS paketi vb.): olcum kumesi CAN kareleriyle ayni
 * gruplara yazilir, kurallar kaynagi ayirt etmez. CAN canliyken yok sayilir.
 * ========================================================================== */
static uint8_t olc_sicak_raw(int16_t c)
{
    if (c == GVN_OLC_YOK_C || c == GVN_OLC_NA_C || c < -50 || c > 204) return 0xFFu;
    return (uint8_t)(c + 50);
}

void Guvenlik_Olcum(const gvn_olcum_t *o)
{
    uint32_t t = HAL_GetTick(), pm;
    int16_t tmax, tmin;
    if (o == NULL) return;
    tmax = o->sicak_max_C; tmin = o->sicak_min_C;      /* MOS (T3) turet()'te GVN_MOS_DAHIL ile */
    pm = __get_PRIMASK();
    __disable_irq();                                   /* FDCAN kesmesi de s_in'e yazar */
    if (s_can_gordu && (uint32_t)(t - s_can_st1_t) <= GVN_TAZE_MS) { __set_PRIMASK(pm); return; }
    s_in.paket_dV = o->paket_dV;
    s_in.akim_dA  = o->akim_dA;
    s_in.soc      = o->soc;
    s_in.t_st1 = t; s_in.seen |= S_ST1;
    if (o->hucre_max_mV != 0u && o->hucre_min_mV != 0u) {
        s_in.hmax = o->hucre_max_mV; s_in.hmin = o->hucre_min_mV;
        s_in.hmax_no = o->hucre_max_no; s_in.hmin_no = o->hucre_min_no;
        s_in.t_cv = t; s_in.seen |= S_CV;
    }
    if (tmax != GVN_OLC_YOK_C || tmin != GVN_OLC_YOK_C) {    /* NA okuma = 0xFF -> kural 8 */
        s_in.tmax_raw = olc_sicak_raw(tmax);
        s_in.tmin_raw = olc_sicak_raw(tmin);
        s_in.t_ct = t; s_in.seen |= S_CT;
    }
    /* sensor sayimi (kural 8): UART paketinde 2 sicaklik var, T1 = batarya, T3 = MOS (CAN gibi) */
    s_in.at_mask   = 0x05u;
    s_in.at_raw[0] = olc_sicak_raw(o->sicak_max_C);
    s_in.at_raw[2] = olc_sicak_raw(o->mos_C);
    s_in.at_raw[1] = s_in.at_raw[3] = s_in.at_raw[4] = 0xFFu;
    s_in.at_kaynak = 2u;
    s_in.t_at = t; s_in.seen |= S_AT;
    if (o->sarj_mos >= 0 && o->desarj_mos >= 0) {
        s_in.chg_mos  = (uint8_t)(o->sarj_mos != 0);
        s_in.dchg_mos = (uint8_t)(o->desarj_mos != 0);
        s_in.plug     = (uint8_t)(o->sarj_fisi > 0);
        s_in.t_sw = t; s_in.seen |= S_SW;
    }
    s_kaynak_son = 2;
    __set_PRIMASK(pm);
}

uint8_t Guvenlik_Sicaklik(int16_t *maks, int16_t *min)
{
    uint8_t ok = s_ekr_ok;
    if (maks != NULL && (ok & 1u)) *maks = s_ekr_tmax;
    if (min  != NULL && (ok & 2u)) *min  = s_ekr_tmin;
    return ok;
}

uint8_t Guvenlik_Ekran_Sicaklik(int16_t *s1, int16_t *s2)
{
    uint8_t ok = s_ekr_ok, p = s_ekr_popup, iki_maks = (uint8_t)(p != 0u && p != K6);
    uint8_t r = 0;
    if (s1 != NULL && (ok & 1u)) { *s1 = s_ekr_tmax; r |= 1u; }
    if (s2 != NULL) {
        if (iki_maks) { if (ok & 1u) { *s2 = s_ekr_tmax; r |= 2u; } }
        else if (ok & 2u) { *s2 = s_ekr_tmin; r |= 2u; }
    }
    return r;
}

uint8_t Guvenlik_Kaynak(void)
{
    uint32_t now = HAL_GetTick(), pm = __get_PRIMASK(), t;
    uint8_t seen, k;
    __disable_irq();
    seen = s_in.seen; t = s_in.t_st1; k = s_kaynak_son;
    __set_PRIMASK(pm);
    return ((seen & S_ST1) && (uint32_t)(now - t) <= GVN_TAZE_MS) ? k : 0u;
}

void Guvenlik_Init(UART_HandleTypeDef *dwin_uart, RTC_HandleTypeDef *rtc)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();                                   /* FDCAN kesmesi s_in'e yazabilir */
    memset(&s_in, 0, sizeof(s_in));
    s_can_st1_t = 0; s_can_gordu = 0; s_kaynak_son = 0; s_ekr_ok = 0; s_ekr_popup = 0;
    __set_PRIMASK(pm);
    memset(s_k, 0, sizeof(s_k));
    memset(s_o, 0, sizeof(s_o));
    s_dw_n = 0; s_sd_n = 0; s_sd_bas = 0;
    s_buz_sahip = 0; s_buz_desen = BZ_YOK;
    s_aktif_maske = 0; s_k3_t0 = 0;
    s_uart = dwin_uart;
    s_rtc  = rtc;
    s_boot_ms = HAL_GetTick();
    s_dw_tazele_t = s_boot_ms;
#if GVN_LOG_FLASH
    fl_tara();
#endif
    s_hazir = 1;
}
