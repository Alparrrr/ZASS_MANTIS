/* ============================================================================
 * ZASS MANTIS - BMS VERI KATMANI: UART6 cozumu, guvenlik besleme, SD satirlari
 * Aciklama ve paket yapisi: bms_veri.h
 * ========================================================================== */
#include "bms_veri.h"
#include "guvenlik.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* main.c'de tanimli */
extern bms_data_t my_bms_can;     /* CAN'dan cozulen   */
extern bms_data_t my_bms;         /* UART6'dan cozulen */
uint8_t CAN_IsReady(void);
uint8_t BMS_IsReady(void);

static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

/* ==========================================================================
 * CAN KARELERI (protokol V2.1; JK/ZASS cihaz adresi F4/F5 ayni sayilir)
 * ========================================================================== */
#define BMS_HUCRE_KARE  ((BMS_HUCRE_MAX + 3u) / 4u)     /* 7 kare: E0..E6 */
static uint32_t s_kare_t[BMS_HUCRE_KARE];                 /* hucre karesinin son gelis zamani */
static uint8_t  s_kare_var;                               /* bit k: kare k gorulmus           */

/* CAN oturumu (SD kaydi tum veri gelince baslar) */
#define OT_ST1   0x01u
#define OT_CV    0x02u
#define OT_CT    0x04u
#define OT_AT    0x08u
#define OT_SW    0x10u
#define OT_HUC   0x20u
#define OT_HEPSI 0x3Fu
static volatile uint8_t  s_ot_gor;                        /* hangi kare turleri geldi            */
static volatile uint32_t s_ot_bas_t, s_ot_son_t, s_ot_huc_t;

static void hucre_say(bms_data_t *b)
{
    uint32_t sum = 0;
    uint16_t k, n = 0, son = 0;
    for (k = 0; k < BMS_HUCRE_MAX; k++)
        if (b->cell_volts[k] > 0u) { sum += b->cell_volts[k]; n++; son = (uint16_t)(k + 1u); }
    if (n > 0u) b->ave_cell_volt = (uint16_t)(sum / n);
    b->cell_count = son;                                  /* sifir olmayan en son hucre */
}

uint8_t BMS_CAN_Coz(uint32_t id, uint8_t ext, const uint8_t *d, bms_data_t *b, uint32_t now)
{
    uint16_t mx, mn, h;
    uint8_t kare, i, j;

    if (d == NULL || b == NULL) return 0;
    if (s_ot_gor != 0u && (uint32_t)(now - s_ot_son_t) > BMS_OTURUM_KOPUK_MS) s_ot_gor = 0u;  /* yeni oturum */
    if (s_ot_gor == 0u) s_ot_bas_t = now;
    s_ot_son_t = now;
    if (!ext) {
        switch (id & 0x700u) {
        case 0x200u:                                      /* Battery Status_1: 0.1 V, 0.1 A (-400 A ofset), SOC */
            s_ot_gor |= OT_ST1;
            b->battery_volts   = (float)(d[0] | (d[1] << 8)) / 10.0f;
            b->battery_current = (int16_t)((int32_t)(d[2] | (d[3] << 8)) - 4000);
            b->soc             = d[4];
            return 1;
        case 0x400u:                                      /* hucre max / min (mV) */
            s_ot_gor |= OT_CV;
            mx = (uint16_t)(d[0] | (d[1] << 8));
            mn = (uint16_t)(d[3] | (d[4] << 8));
            b->cell_delta = (mx >= mn) ? (uint16_t)(mx - mn) : 0u;
            break;
        case 0x500u:                                      /* bayt0 = en yuksek hucre sicakligi (-50) */
            s_ot_gor |= OT_CT;
            b->batt_temp = (int16_t)((int16_t)d[0] - 50);
            break;
        default:
            break;
        }
        return 0;
    }
    switch (id & 0x1FFFFF00u) {
    case 0x18E02800u: case 0x18E12800u: case 0x18E22800u: case 0x18E32800u:
    case 0x18E42800u: case 0x18E52800u: case 0x18E62800u:   /* hucreler, kare basina 4 (mV) */
        kare = (uint8_t)(((id >> 16) & 0xFFu) - 0xE0u);
        if (!(s_ot_gor & OT_HUC)) { s_ot_huc_t = now; s_ot_gor |= OT_HUC; }
        for (i = 0; i < 4u; i++) {
            h = (uint16_t)(kare * 4u + i);
            if (h < BMS_HUCRE_MAX) b->cell_volts[h] = (uint16_t)(d[i * 2u] | (d[i * 2u + 1u] << 8));
        }
        s_kare_t[kare] = now;
        s_kare_var |= (uint8_t)(1u << kare);
        for (i = 0; i < BMS_HUCRE_KARE; i++) {            /* uzun suredir gelmeyen kare: hucreleri sil */
            if (i == kare || !(s_kare_var & (1u << i)) || (uint32_t)(now - s_kare_t[i]) <= BMS_HUCRE_ESKI_MS) continue;
            for (j = 0; j < 4u; j++) {
                h = (uint16_t)(i * 4u + j);
                if (h < BMS_HUCRE_MAX) b->cell_volts[h] = 0u;
            }
            s_kare_var &= (uint8_t)~(1u << i);
        }
        hucre_say(b);
        break;
    case 0x18F22800u:                                     /* ALL TEMP: bayt3 = T3 = MOS */
        s_ot_gor |= OT_AT;
        if (d[3] != 0xFFu) b->mos_temp = (int16_t)((int16_t)d[3] - 50);
        b->sicak_kaynak = 1u;                             /* SD'ye ham yazilir (sensor teshisi) */
        b->sicak_maske  = d[0];
        for (i = 0; i < 5u; i++) b->sicak_ham[i] = d[1u + i];
        break;
    case 0x18F32800u:                                     /* BMS ERR INFO */
        b->warning = (uint16_t)(d[0] | (d[1] << 8));
        if (d[2] & 0x03u) b->warning |= 0x8000u;          /* sarj/desarj MOS arizasi */
        break;
    case 0x18F52800u:                                     /* SWITCH STATUS */
        s_ot_gor |= OT_SW;
        b->charge_mos     = (uint16_t)(d[0] & 1u);
        b->discharge_mos  = (uint16_t)((d[0] >> 1) & 1u);
        b->balance_status = (uint16_t)((d[0] >> 2) & 1u);
        b->heat_mos       = (uint8_t)((d[0] >> 3) & 1u);
        b->chg_plug       = (uint8_t)((d[0] >> 4) & 1u);
        b->acc_sta        = (uint8_t)((d[0] >> 5) & 1u);
        break;
    default:
        break;
    }
    return 0;
}

uint8_t BMS_CAN_Tam(uint32_t now)
{
    uint8_t g = s_ot_gor;
    if (g == 0u || (uint32_t)(now - s_ot_son_t) > BMS_OTURUM_KOPUK_MS) return 0u;
    if ((g & OT_HEPSI) == OT_HEPSI && (uint32_t)(now - s_ot_huc_t) >= BMS_HUCRE_TUR_MS) return 1u;
    return (uint8_t)((g & OT_ST1) && (uint32_t)(now - s_ot_bas_t) >= BMS_TAM_ZAMAN_ASIMI_MS);
}

/* ==========================================================================
 * UART6 PAKETI
 * ========================================================================== */
static uint8_t ham_sicak(int16_t c)                      /* C -> C + 50, aralik disi 0xFF */
{
    return (c < -50 || c > 204) ? 0xFFu : (uint8_t)(c + 50);
}

uint8_t BMS_UART_Coz(const uint8_t *buf, uint16_t n, bms_data_t *b)
{
    bms_data_t y;
    const uint8_t *p;
    uint16_t i, k, son;

    if (buf == NULL || b == NULL || n < BMS_UART_PAKET) return 0;
    for (i = 0; (uint32_t)i + BMS_UART_PAKET <= n; i++) {
        if (buf[i] != 0xA5u || buf[i + 1u] != 0x5Au || buf[i + 2u] != 0x5Du || buf[i + 3u] != 0x82u) continue;
        if (buf[i + 4u] != 0x10u || buf[i + 5u] != 0x00u) continue;          /* VP 0x1000 */
        p = &buf[i + 6u];
        memset(&y, 0, sizeof(y));
        y.battery_volts   = (float)be16(p + 0) / 100.0f;
        y.battery_current = (int16_t)be16(p + 2);                           /* 0.1 A */
        y.soc             = be16(p + 6);
        y.cell_delta      = be16(p + 8);
        y.mos_temp        = (int16_t)be16(p + 10);
        y.batt_temp       = (int16_t)be16(p + 12);
        y.warning         = be16(p + 14);
        y.ave_cell_volt   = be16(p + 16);
        y.balance_status  = be16(p + 18);
        y.charge_mos      = be16(p + 20);
        y.discharge_mos   = be16(p + 22);
        son = 0;
        for (k = 0; k < BMS_HUCRE_MAX; k++) {
            y.cell_volts[k] = be16(p + 24u + 2u * k);
            if (y.cell_volts[k] != 0u) son = (uint16_t)(k + 1u);
        }
        y.cell_count = son;
        y.sicak_kaynak = 2u;                                                /* UART: 2 sicaklik */
        y.sicak_maske  = 0x05u;                                             /* T1 batarya, T3 MOS */
        y.sicak_ham[0] = ham_sicak(y.batt_temp);
        y.sicak_ham[2] = ham_sicak(y.mos_temp);
        y.sicak_ham[1] = y.sicak_ham[3] = y.sicak_ham[4] = 0xFFu;
        if (be16(p + 0) == 0u && son == 0u) return 0;                       /* bos / cop paket */
        *b = y;
        return 1;
    }
    return 0;
}

uint8_t BMS_Hucre_MaxMin(const bms_data_t *b, uint16_t *mx, uint16_t *mn)
{
    uint16_t k, n, a = 0, z = 0xFFFFu;
    if (b == NULL) return 0;
    n = (b->cell_count > BMS_HUCRE_MAX) ? (uint16_t)BMS_HUCRE_MAX : b->cell_count;
    for (k = 0; k < n; k++) {
        uint16_t v = b->cell_volts[k];
        if (v == 0u) continue;
        if (v > a) a = v;
        if (v < z) z = v;
    }
    if (a == 0u) return 0;
    if (mx) *mx = a;
    if (mn) *mn = z;
    return 1;
}

static int16_t sicak_olc(int16_t c)
{
    return (c < GVN_SICAK_GECERLI_ALT || c > GVN_SICAK_GECERLI_UST) ? (int16_t)GVN_OLC_NA_C : c;
}

void BMS_UART_Guvenlige(const bms_data_t *b)
{
    gvn_olcum_t o;
    uint16_t mx = 0, mn = 0;
    float v;
    if (b == NULL) return;
    memset(&o, 0, sizeof(o));
    v = b->battery_volts * 10.0f + 0.5f;
    o.paket_dV = (v <= 0.0f) ? 0u : (v >= 65535.0f) ? 65535u : (uint16_t)v;
    o.akim_dA  = b->battery_current;
    o.soc      = (uint8_t)((b->soc > 255u) ? 255u : b->soc);
    if (BMS_Hucre_MaxMin(b, &mx, &mn)) {
        uint16_t k, nh = (b->cell_count > BMS_HUCRE_MAX) ? (uint16_t)BMS_HUCRE_MAX : b->cell_count;
        o.hucre_max_mV = mx; o.hucre_min_mV = mn;
        for (k = 0; k < nh; k++) {                                 /* hangi hucre (1..) */
            if (o.hucre_max_no == 0u && b->cell_volts[k] == mx) o.hucre_max_no = (uint8_t)(k + 1u);
            if (o.hucre_min_no == 0u && b->cell_volts[k] == mn) o.hucre_min_no = (uint8_t)(k + 1u);
        }
    }
    o.sicak_max_C = o.sicak_min_C = sicak_olc(b->batt_temp);   /* pakette tek batarya sicakligi var */
    o.mos_C       = sicak_olc(b->mos_temp);
    o.sarj_mos    = (int8_t)(b->charge_mos != 0u);
    o.desarj_mos  = (int8_t)(b->discharge_mos != 0u);
    o.sarj_fisi   = -1;                                        /* UART paketinde yok */
    Guvenlik_Olcum(&o);
}

/* ==========================================================================
 * ETKIN KAYNAK
 * ========================================================================== */
uint8_t BMS_Aktif_Kopya(bms_data_t *out)
{
    uint8_t k = BMS_KAYNAK_YOK;
    uint32_t pm;
    if (CAN_IsReady())      k = BMS_KAYNAK_CAN;
    else if (BMS_IsReady()) k = BMS_KAYNAK_UART;
    if (out != NULL && k != BMS_KAYNAK_YOK) {
        pm = __get_PRIMASK();
        __disable_irq();                               /* yazanlar FDCAN / UART kesmesi */
        *out = (k == BMS_KAYNAK_CAN) ? my_bms_can : my_bms;
        __set_PRIMASK(pm);
    }
    return k;
}

const char *BMS_Kaynak_Adi(uint8_t kaynak)
{
    return (kaynak == BMS_KAYNAK_CAN) ? "CAN" : (kaynak == BMS_KAYNAK_UART) ? "UART" : "YOK";
}

/* ==========================================================================
 * SD SATIRLARI
 * ========================================================================== */
static uint8_t ekle(char *buf, uint16_t boyut, uint16_t *n, const char *fmt, ...)
{
    va_list ap;
    int r;
    if (*n >= boyut) return 0;
    va_start(ap, fmt);
    r = vsnprintf(buf + *n, (size_t)(boyut - *n), fmt, ap);
    va_end(ap);
    if (r < 0 || (uint32_t)*n + (uint32_t)r >= boyut) { *n = boyut; return 0; }
    *n = (uint16_t)(*n + (uint16_t)r);
    return 1;
}

#define SIC_SUTUN 0x1Bu                                   /* T1 T2 T4 T5 (T3 = MOS sutunu) */

static uint8_t sicak_ok(uint8_t ham)
{
    return (uint8_t)(ham != 0xFFu && (int16_t)ham - 50 >= GVN_SICAK_GECERLI_ALT &&
                     (int16_t)ham - 50 <= GVN_SICAK_GECERLI_UST);
}

uint8_t BMS_Sicak_Gecerli(const bms_data_t *b)
{
    uint8_t k, m = 0;
    if (b == NULL || b->sicak_kaynak == 0u) return 0u;
    for (k = 0; k < 5u; k++)
        if (((SIC_SUTUN >> k) & 1u) && sicak_ok(b->sicak_ham[k])) m |= (uint8_t)(1u << k);
    return m;
}

uint16_t BMS_CSV_Baslik(char *buf, uint16_t boyut, uint16_t hucre, uint8_t sic_set)
{
    uint16_t n = 0, k;
    if (buf == NULL || boyut == 0u) return 0;
    if (hucre > BMS_HUCRE_MAX) hucre = BMS_HUCRE_MAX;
    ekle(buf, boyut, &n, "Tarih Saat,Kaynak,Paket V,Akim A,SOC %%,MOS C,Maks C,Min C,Hucre Fark mV,"
                         "Ort Hucre mV,Hucre Max mV,Hucre Min mV,Sarj MOS,Desarj MOS,Sarj Fisi,"
                         "BMS Uyari,Guvenlik");
    for (k = 0; k < 5u; k++)
        if (((sic_set & SIC_SUTUN) >> k) & 1u) ekle(buf, boyut, &n, ",T%u C", (unsigned)(k + 1u));
    ekle(buf, boyut, &n, ",Hucre Sayisi");
    for (k = 1; k <= hucre; k++) ekle(buf, boyut, &n, ",H%u mV", (unsigned)k);
    if (!ekle(buf, boyut, &n, "\n")) { buf[0] = '\0'; return 0; }
    return n;
}

uint16_t BMS_CSV_Satir(char *buf, uint16_t boyut, const char *zaman, uint8_t kaynak,
                       const bms_data_t *b, uint8_t gvn_maske, uint8_t sic_set)
{
    uint16_t n = 0, k, hc, mx = 0, mn = 0;
    uint32_t cv;
    int32_t a;
    float v;

    if (buf == NULL || boyut == 0u || b == NULL) return 0;
    hc = (b->cell_count > BMS_HUCRE_MAX) ? (uint16_t)BMS_HUCRE_MAX : b->cell_count;
    v  = b->battery_volts * 100.0f + 0.5f;
    cv = (v <= 0.0f) ? 0u : (uint32_t)v;                           /* 10 mV */
    a  = b->battery_current;                                        /* 0.1 A */
    (void)BMS_Hucre_MaxMin(b, &mx, &mn);

    ekle(buf, boyut, &n, "%s,%s,%lu.%02lu,%s%ld.%ld,%u,%d,",
         (zaman != NULL) ? zaman : "", BMS_Kaynak_Adi(kaynak),
         (unsigned long)(cv / 100u), (unsigned long)(cv % 100u),
         (a < 0) ? "-" : "", (long)((a < 0 ? -a : a) / 10), (long)((a < 0 ? -a : a) % 10),
         (unsigned)b->soc, (int)b->mos_temp);
    if (b->sicak_mm_ok & 1u) ekle(buf, boyut, &n, "%d,", (int)b->sicak_maks); else ekle(buf, boyut, &n, "-,");
    if (b->sicak_mm_ok & 2u) ekle(buf, boyut, &n, "%d,", (int)b->sicak_min);  else ekle(buf, boyut, &n, "-,");
    ekle(buf, boyut, &n, "%u,%u,%u,%u,%u,%u,", (unsigned)b->cell_delta,
         (unsigned)b->ave_cell_volt, (unsigned)mx, (unsigned)mn,
         (unsigned)(b->charge_mos != 0u), (unsigned)(b->discharge_mos != 0u));
    if (kaynak == BMS_KAYNAK_CAN) ekle(buf, boyut, &n, "%u,", (unsigned)(b->chg_plug != 0u));
    else                          ekle(buf, boyut, &n, "-,");            /* UART paketinde fis bilgisi yok */
    ekle(buf, boyut, &n, "%04X,", (unsigned)b->warning);
    for (k = 1; k <= 8u; k++)                                       /* aktif kurallar: "K1 K7" */
        if ((gvn_maske >> (k - 1u)) & 1u)
            ekle(buf, boyut, &n, ((gvn_maske & ((1u << (k - 1u)) - 1u)) != 0u) ? " K%u" : "K%u", (unsigned)k);
    for (k = 0; k < 5u; k++) {                                      /* oturumdaki problar; okunmuyorsa "-" */
        if (!(((sic_set & SIC_SUTUN) >> k) & 1u)) continue;
        if (b->sicak_kaynak != 0u && sicak_ok(b->sicak_ham[k])) ekle(buf, boyut, &n, ",%d", (int)b->sicak_ham[k] - 50);
        else ekle(buf, boyut, &n, ",-");
    }
    ekle(buf, boyut, &n, ",%u", (unsigned)hc);
    for (k = 0; k < hc; k++) ekle(buf, boyut, &n, ",%u", (unsigned)b->cell_volts[k]);
    if (!ekle(buf, boyut, &n, "\n")) { buf[0] = '\0'; return 0; }
    return n;
}
