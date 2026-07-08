/* ============================================================================
 * ZASS - SOC + BAR + DURUM  (TEK DOSYA)  -  BKN'de calisan yontem (animasyonsuz)
 * ----------------------------------------------------------------------------
 * Yazilanlar:
 *   SOC degeri   -> 0x1000
 *   SOC rengi    -> 0x5003   (SP 0x5000 + 3)
 *   bar doluluk  -> 0x100C
 *   bar rengi    -> 0x5046   (SP 0x5040 + 6 = BKN'de calisan offset)
 *   durum cipi   -> 0x1100   (0=sarj 1=bekleme 2=desarj-mavi 3=desarj-kirmizi)
 *
 * "0 A'de sarj/desarj'da kalma" SEBEBI: durum cipini hic yazan yoksa varsayilan
 * indekste (genelde 0=sarj) takili kalir. Burada AYNI deadband ile yaziyoruz:
 *   |akim| < DEADBAND_A  ->  BEKLEME (cip 1).  Yani 0 A = kesin bekleme.
 *
 * Kullanim (donguden, ~100-200 ms):
 *     float akim = (float)my_bms.battery_current / 10.0f;   // gercek Amper
 *     SOC_Bar_Guncelle(my_bms.soc, akim);
 * ========================================================================== */

#include "main.h"

/* DWIN'in bagli oldugu UART (gerekirse degistir) */
extern UART_HandleTypeDef huart3;
#define DWIN_UART  (&huart3)

/* ---- adresler ---- */
#define VP_SOC     0x1000     /* SOC sayisi degeri          */
#define SP_SOC     0x5000     /* SOC rengi -> 0x5003        */
#define VP_BAR     0x100C     /* bar doluluk (0..100)       */
#define SP_BAR     0x5040     /* bar SP                     */
#define BAR_COLOR_OFS  6      /* bar rengi = 0x5046 (BKN'de calisan offset) */
#define VP_STATUS  0x1100     /* durum cipi (Icon Variable) */

/* Durum cip indeksleri - DGUS'taki ICL sirasi:
 * 0=bekleme 1=sarj 2=desarj-mavi 3=desarj-kirmizi */
#define CHIP_IDLE      0      /* BEKLEMEDE    (amber)   */
#define CHIP_CHARGE    1      /* ŞARJ OLUYOR  (yesil)   */
#define CHIP_DIS_BLUE  2      /* DEŞARJ >%20  (mavi)    */
#define CHIP_DIS_RED   3      /* DEŞARJ <=%20 (kirmizi) */

/* ---- renkler (RGB565) ---- */
#define RGB565(r,g,b)  ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b) >> 3)))
#define C_GREEN  RGB565( 44,200,120)
#define C_AMBER  RGB565(245,166, 35)
#define C_BLUE   RGB565( 54,144,245)
#define C_RED    RGB565(235, 72, 64)
#define DEADBAND_A   0.3f     /* +-0.3 A altinda BEKLEME (0 A = bekleme) */

/* ---- dusuk seviye: VP'ye 1 word yaz (5A A5 05 82 ADRH ADRL VH VL) ---- */
static void dwin_w(uint16_t vp, uint16_t v)
{
    uint8_t f[8];
    f[0] = 0x5A; f[1] = 0xA5; f[2] = 0x05; f[3] = 0x82;
    f[4] = (uint8_t)(vp >> 8); f[5] = (uint8_t)(vp & 0xFF);
    f[6] = (uint8_t)(v  >> 8); f[7] = (uint8_t)(v  & 0xFF);
    HAL_UART_Transmit(DWIN_UART, f, 8, 100);
}

/* Durum DEBOUNCE: yeni mod ekrana yazilmadan once DURUM_ESIK kez ust uste
 * okunmali. Boylece akim deadband sininda oynarken durum aninda zaplamaz.
 * Sure = DURUM_ESIK * (dongu periyodu). Ornek: 10 * 100ms = 1 sn. */
#define DURUM_ESIK   30
#define MOD_BEKLEME  0
#define MOD_SARJ     1
#define MOD_DESARJ   2

/* soc: 0..100   akim_A: gercek Amper (sarj +, desarj -) */
void SOC_Bar_Guncelle(uint16_t soc, float akim_A)
{
    static int mod_aktif = MOD_BEKLEME;   /* su an gosterilen mod (commit) */
    static int mod_aday  = -1;            /* bekleyen yeni mod */
    static int sayac     = 0;

    /* 1) ham mod (anlik) */
    int mod_ham;
    if      (akim_A >  DEADBAND_A) mod_ham = MOD_SARJ;
    else if (akim_A < -DEADBAND_A) mod_ham = MOD_DESARJ;
    else                           mod_ham = MOD_BEKLEME;

    /* 2) debounce: yeni mod ESIK kadar ardisik gelirse commit et */
    if (mod_ham == mod_aktif) {
        sayac = 0; mod_aday = -1;          /* degisim yok */
    } else {
        if (mod_ham == mod_aday) sayac++;
        else { mod_aday = mod_ham; sayac = 1; }
        if (sayac >= DURUM_ESIK) {
            mod_aktif = mod_ham;           /* artik degisti */
            sayac = 0; mod_aday = -1;
        }
    }

    /* 3) commit edilmis moda gore renk + cip (SOC degisimi anlik) */
    uint16_t renk, chip;
    if (mod_aktif == MOD_SARJ) {
        renk = C_GREEN; chip = CHIP_CHARGE;
    } else if (mod_aktif == MOD_DESARJ) {
        if (soc <= 20) { renk = C_RED;  chip = CHIP_DIS_RED; }
        else           { renk = C_BLUE; chip = CHIP_DIS_BLUE; }
    } else {
        renk = C_AMBER; chip = CHIP_IDLE;
    }

    dwin_w(VP_SOC, soc);                   /* SOC degeri  (0x1000) */
    dwin_w(SP_SOC + 3, renk);              /* SOC rengi   (0x5003) */
    dwin_w(VP_BAR, soc);                   /* bar doluluk (0x100C) */
    dwin_w(SP_BAR + BAR_COLOR_OFS, renk);  /* bar rengi   (0x5046) */
    dwin_w(VP_STATUS, chip);               /* durum cipi  (0x1100) */
}

/* ===================== SICAKLIK RENGI ============================
 * Skala: 20 C = BEYAZ -> 40 C = MAVI -> 60 C = KIRMIZI (kademesiz lerp).
 * Renk Data Variable Display SP+3'e yazilir (SOC'deki gibi).
 *   Sicaklik 1: SP 0x5060 -> renk 0x5063
 *   Sicaklik 2: SP 0x5070 -> renk 0x5073
 * DGUS sarti: her iki sicaklik kontrolunde SP ACIK olmali.
 * ================================================================ */
#define SP_TEMP1   0x5060
#define SP_TEMP2   0x5070

static int lerp(int a, int b, float k) { return (int)(a + (b - a) * k + 0.5f); }

static uint16_t temp_color(int16_t t)
{
    int r, g, b;
    if (t <= 20) {                 /* <=20: beyaz */
        r = 255; g = 255; b = 255;
    } else if (t <= 40) {          /* 20->40: beyaz -> mavi */
        float k = (t - 20) / 20.0f;
        r = lerp(255,  54, k); g = lerp(255, 144, k); b = lerp(255, 255, k);
    } else if (t <= 60) {          /* 40->60: mavi -> kirmizi */
        float k = (t - 40) / 20.0f;
        r = lerp( 54, 235, k); g = lerp(144,  72, k); b = lerp(255,  64, k);
    } else {                       /* >=60: kirmizi */
        r = 235; g = 72; b = 64;
    }
    return RGB565(r, g, b);
}

/* t1 = SICAKLIK 1 (MOS),  t2 = SICAKLIK 2 (Batarya).  Donguden cagir. */
void Sicaklik_Renk_Guncelle(int16_t t1, int16_t t2)
{
    dwin_w(SP_TEMP1 + 3, temp_color(t1));   /* 0x5063 */
    dwin_w(SP_TEMP2 + 3, temp_color(t2));   /* 0x5073 */
}

/* ===================== AKIM RENGI ===============================
 * |akim| <=120 A: BEYAZ -> 120..200 A: yavasca KIRMIZI -> >=200 A: KIRMIZI.
 * Renk Data Variable Display SP+3'e yazilir.  Akim SP 0x5080 -> renk 0x5083.
 * DGUS sarti: akim kontrolunde SP ACIK olmali.
 * ================================================================ */
/* ===================== AKIM RENGI ===============================
 * |akim| <=120 A: BEYAZ -> 120..200 A: yavasca KIRMIZI -> >=200 A: KIRMIZI.
 * Renk Data Variable Display SP+3'e yazilir.  Akim SP 0x5080 -> renk 0x5083.
 * DGUS sarti: akim kontrolunde SP ACIK olmali.
 * ================================================================ */
#define SP_CURR    0x5080
/* DESARJ tarafi: -100 A = BEYAZ baslar, -200 A = tam KIRMIZI. */
#define AKIM_BEYAZ_ESIK   -100.0f   /* >= bu deger -> beyaz (sarj dahil)  */
#define AKIM_KIRMIZI_ESIK -200.0f   /* <= bu deger -> kirmizi             */

static uint16_t akim_color(float a)
{
    if (a >= AKIM_BEYAZ_ESIK)   return RGB565(240, 242, 246);   /* beyaz   */
    if (a <= AKIM_KIRMIZI_ESIK) return RGB565(235,  72,  64);   /* kirmizi */
    /* a: -100 -> 0,  -200 -> 1 */
    float k = (AKIM_BEYAZ_ESIK - a) / (AKIM_BEYAZ_ESIK - AKIM_KIRMIZI_ESIK);
    int r = lerp(240, 235, k), g = lerp(242, 72, k), b = lerp(246, 64, k);
    return RGB565(r, g, b);
}

/* akim_A = gercek akim (Amper, isaret onemsiz). Donguden cagir. */
void Akim_Renk_Guncelle(float akim_A)
{
    dwin_w(SP_CURR + 3, akim_color(akim_A));    /* 0x5083 */
}
