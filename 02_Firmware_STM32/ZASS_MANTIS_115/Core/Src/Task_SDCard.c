/*
 * Task_SDCard.c  -  SD kart kaydi (myTask02 her saniye SD_Log_Process() cagirir)
 *
 *   20YY/MM_20YY/DD_MM_20YY_LOG.CSV  BMS verisi: zaman, kaynak (CAN / UART), paket,
 *                                    sicakliklar, MOS'lar, aktif kurallar, TUM hucreler
 *                                    (sutun sayisi hucre sayisina gore; degisirse yeni baslik)
 *   20YY/MM_20YY/OLAYLAR_MM_20YY.CSV olaylar (guvenlik uyarisi, saat ayari, cihaz acilisi), her biri
 *                                    tek satir: Tarih Saat, Kod, Olay (Turkce cumle, UTF-8 BOM)
 *   SAAT.TXT (kok)                   "2026-10-07 15:30:00" -> RTC kurulur, adi SAAT_OK.TXT olur
 *
 * Kart algilama: soketin CD ucu PA3'te (giris, ic pull-up; kart takiliyken GND). Kart
 * cikinca karta HIC yazmadan birakilir (olaylar kuyrukta bekler), takilinca hemen baglanir.
 * Kart sonradan takilabilir ya da cikarilabilir: hata olursa dosyalar kapanir, kart
 * SD_TEKRAR_SN sonra yeniden baglanir. Veri dosyasi acik tutulur ve SD_SYNC_SN'de bir
 * f_sync yapilir (her satirda ac-kapa karti asindirir; elektrik kesilirse en fazla
 * SD_SYNC_SN kadar veri kaybolur). Tum bicimleme tamsayi: newlib float/malloc yok.
 *
 *  Created on: Feb 10, 2026
 *      Author: ALPER
 */
#include "main.h"
#include "cmsis_os.h"
#include "app_fatfs.h"
#include "ff_gen_drv.h"
#include "Task_SDCard.h"
#include "bms_veri.h"
#include "saat.h"
#include "guvenlik.h"
#include <string.h>
#include <stdio.h>

#define SD_KAYIT_PERIYOT_SN   1u    /* veri satiri araligi (kural aktif degilken)   */
#define SD_ALARM_PERIYOT_SN   1u    /* herhangi bir guvenlik kurali aktifken        */
#define SD_SYNC_SN           10u
#define SD_TEKRAR_SN         10u    /* kart yok / hata: yeniden deneme              */

/* Kart algilama (CD) - PA3. Kart takiliyken okunan seviye SD_CD_TAKILI. Ters calisirsa
 * (kart takiliyken sd_cd_ham = 1) GPIO_PIN_SET yap; CD teli yoksa SD_CD_KULLAN 0.       */
#ifndef SD_CD_KULLAN
#define SD_CD_KULLAN          1
#endif
#define SD_CD_PORT            GPIOA
#define SD_CD_PIN             GPIO_PIN_3
#ifndef SD_CD_TAKILI
#define SD_CD_TAKILI          GPIO_PIN_RESET
#endif
/* Kart takildiktan sonra CD bu kadar ardisik okumada (1 sn arayla) "takili" kalmadan
 * baglanma denenmez: kart yuvaya tam oturup beslemesi kalksin (calisirken takinca donma). */
#define SD_CD_OTURMA          2u

/* Acilis nedeni (her acilista OLAY'a bir satir): RCC->CSR + HardFault yedek yazmaclari */
#define HATA_IMZA_MASKE       0xFFFF0000u
#define HATA_IMZA             0xFA170000u
#define HATA_IMZA_PSP         0xFA170001u

extern Disk_drvTypeDef disk;        /* ff_gen_drv.c */

static FATFS      s_fs;
static FIL        s_veri, s_olay;
static uint8_t    s_takili, s_veri_acik;
static uint8_t    s_cd_hazir, s_cd_onceki;
static uint8_t    s_gun = 0xFF;                 /* acik veri dosyasinin gunu        */
static uint16_t   s_baslik_hucre;               /* son basliktaki hucre sayisi      */
static uint8_t    s_baslik_kaynak, s_son_kaynak;
static uint8_t    s_sic_set, s_baslik_sic;          /* oturumda gorulen batarya problari */
static uint32_t   s_son_sync, s_sonraki_deneme, s_son_satir;
static char       s_yol[48];
static char       s_satir[400];
static bms_data_t s_b;

volatile uint8_t  sd_durum;          /* Live Expressions: 0 kart yok, 1 bagli, 2 yaziyor, 3 hata */
volatile uint32_t sd_satir_sayisi;   /* yazilan veri satiri                                     */
volatile uint32_t sd_hata_sayisi;
volatile uint8_t  sd_cd_ham;         /* PA3 ham seviye: kart takiliyken 0 olmali (SD_CD_TAKILI) */
volatile uint8_t  sd_kart_var;       /* CD'ye gore kart takili mi                               */
volatile uint32_t sd_acilis_csr;     /* acilistaki RCC->CSR (reset nedeni bayraklari)            */
volatile uint32_t sd_hata_pc;        /* onceki calismada HardFault olduysa PC (0 = olmadi)       */
volatile uint32_t sd_hata_lr;
static uint8_t    s_acilis_alindi, s_acilis_yazildi;
static uint32_t   s_hata_imza;
static uint8_t    yaz(FIL *f, const char *m, uint16_t n);
static uint8_t    olay_dosyasi_ac(const saat_t *s);

/* ---------------------------------------------------------------- yardimcilar */
static void sd_birak(void)
{
    if (s_veri_acik) (void)f_close(&s_veri);
    s_veri_acik = 0;
    s_gun = 0xFF;
    (void)f_mount(NULL, "", 0);
    s_takili = 0;
    sd_durum = 3;
    sd_hata_sayisi++;
    s_sonraki_deneme = HAL_GetTick() + SD_TEKRAR_SN * 1000u;
}

/* CD ucunu oku (ilk cagrida PA3'u giris + pull-up yapar) */
static uint8_t kart_var(void)
{
#if SD_CD_KULLAN
    GPIO_PinState p;
    if (!s_cd_hazir) {
        GPIO_InitTypeDef g;
        memset(&g, 0, sizeof(g));
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin  = SD_CD_PIN;
        g.Mode = GPIO_MODE_INPUT;
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(SD_CD_PORT, &g);
        s_cd_hazir = 1;
    }
    p = HAL_GPIO_ReadPin(SD_CD_PORT, SD_CD_PIN);
    sd_cd_ham = (uint8_t)p;
    return (uint8_t)(p == SD_CD_TAKILI);
#else
    return 1u;
#endif
}

/* acilis nedenini bir kez topla (RCC bayraklari temizlenir, HardFault kaydi silinir) */
static void acilis_al(void)
{
    if (s_acilis_alindi) return;
    s_acilis_alindi = 1;
    sd_acilis_csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    s_hata_imza = Saat_Yedek_Oku(RTC_BKP_DR2);
    if ((s_hata_imza & HATA_IMZA_MASKE) == HATA_IMZA) {
        sd_hata_pc = Saat_Yedek_Oku(RTC_BKP_DR3);
        sd_hata_lr = Saat_Yedek_Oku(RTC_BKP_DR4);
        Saat_Yedek_Yaz(RTC_BKP_DR2, 0u);
    } else {
        s_hata_imza = 0u;
    }
}

static const char *acilis_nedeni(void)
{
    uint32_t c = sd_acilis_csr;
    if (s_hata_imza != 0u)      return "yazılım hatası (HardFault) - cihaz kendini yeniden başlattı";
    if (c & RCC_CSR_IWDGRSTF)   return "watchdog reseti";
    if (c & RCC_CSR_WWDGRSTF)   return "pencere watchdog reseti";
    if (c & RCC_CSR_SFTRSTF)    return "yazılım reseti";
    if (c & RCC_CSR_LPWRRSTF)   return "düşük güç reseti";
    if (c & RCC_CSR_PWRRSTF)    return "güç açıldı ya da besleme düştü";
    if (c & RCC_CSR_PINRSTF)    return "reset düğmesi / programlayıcı";
    return "açılış nedeni bilinmiyor";
}

/* her acilista OLAY dosyasina bir satir (kart sonradan takilsa da) */
static void acilis_yaz(const saat_t *s)
{
    char z[24];
    uint16_t n;
    if (s_acilis_yazildi) return;
    Saat_Metin(s, z, sizeof(z));
    if (s_hata_imza == HATA_IMZA_PSP)
        n = (uint16_t)snprintf(s_satir, sizeof(s_satir), "%s,SISTEM,Cihaz açıldı: %s"
                               " / hata adresi PC=0x%08lX LR=0x%08lX\n",
                               z, acilis_nedeni(), (unsigned long)sd_hata_pc, (unsigned long)sd_hata_lr);
    else
        n = (uint16_t)snprintf(s_satir, sizeof(s_satir), "%s,SISTEM,Cihaz açıldı: %s\n", z, acilis_nedeni());
    if (olay_dosyasi_ac(s)) {
        if (yaz(&s_olay, s_satir, (n < sizeof(s_satir)) ? n : (uint16_t)(sizeof(s_satir) - 1u))) s_acilis_yazildi = 1;
        (void)f_close(&s_olay);
    }
}

/* kart cikarildi: karta yazmaya calismadan birak (f_mount(NULL) acik dosya kilitlerini temizler) */
static void kart_cikti(void)
{
    s_veri_acik = 0;
    s_gun = 0xFF;
    (void)f_mount(NULL, "", 0);
    s_takili = 0;
}

static uint8_t yaz(FIL *f, const char *m, uint16_t n)
{
    UINT bw = 0;
    if (n == 0u) return 1;
    return (uint8_t)(f_write(f, m, n, &bw) == FR_OK && bw == n);
}

static void klasorler(const saat_t *s)
{
    char y[24];
    (void)snprintf(y, sizeof(y), "%04u", (unsigned)s->yil);
    (void)f_mkdir(y);                                   /* varsa FR_EXIST: sorun degil */
    (void)snprintf(y, sizeof(y), "%04u/%02u_%04u", (unsigned)s->yil, (unsigned)s->ay, (unsigned)s->yil);
    (void)f_mkdir(y);
}

/* olay dosyasina bir satir ekle (dosya ac-yaz-kapat; olaylar seyrek) */
static uint8_t olay_dosyasi_ac(const saat_t *s)
{
    /* UTF-8 BOM: Excel Turkce karakterleri (ç ş ğ ı ö ü) dogru gostersin */
    static const char BASLIK[] =
        "\xEF\xBB\xBFTarih Saat,Kod,Olay\n";
    klasorler(s);
    (void)snprintf(s_yol, sizeof(s_yol), "%04u/%02u_%04u/OLAYLAR_%02u_%04u.CSV", (unsigned)s->yil,
                   (unsigned)s->ay, (unsigned)s->yil, (unsigned)s->ay, (unsigned)s->yil);
    if (f_open(&s_olay, s_yol, FA_OPEN_APPEND | FA_WRITE) != FR_OK) return 0;
    if (f_size(&s_olay) == 0u && !yaz(&s_olay, BASLIK, (uint16_t)(sizeof(BASLIK) - 1u))) {
        (void)f_close(&s_olay);
        return 0;
    }
    return 1;
}

/* SAAT.TXT varsa RTC'yi kur, dosyayi SAAT_OK.TXT yap (hataliysa SAAT_HATALI.TXT) */
static void saat_dosyasi(void)
{
    char m[64], eski_m[24], yeni_m[24];
    UINT br = 0;
    saat_t yeni, eski;
    uint16_t n;
    if (f_open(&s_olay, "SAAT.TXT", FA_READ) != FR_OK) return;
    memset(m, 0, sizeof(m));
    (void)f_read(&s_olay, m, sizeof(m) - 1u, &br);
    (void)f_close(&s_olay);
    if (!Saat_Metin_Coz(m, &yeni)) {
        (void)f_unlink("SAAT_HATALI.TXT");
        (void)f_rename("SAAT.TXT", "SAAT_HATALI.TXT");
        return;
    }
    if (!Saat_Oku(&eski)) memset(&eski, 0, sizeof(eski));
    if (!Saat_Ayarla(&yeni)) return;
    (void)f_unlink("SAAT_OK.TXT");
    (void)f_rename("SAAT.TXT", "SAAT_OK.TXT");
    Saat_Metin(&eski, eski_m, sizeof(eski_m));
    Saat_Metin(&yeni, yeni_m, sizeof(yeni_m));
    n = (uint16_t)snprintf(s_satir, sizeof(s_satir), "%s,SAAT,Saat SAAT.TXT ile ayarlandı: önceki saat %s"
                           " / saat kaynağı %s\n", yeni_m, eski_m, Saat_Kaynak_Adi());
    if (olay_dosyasi_ac(&yeni)) {
        (void)yaz(&s_olay, s_satir, (n < sizeof(s_satir)) ? n : (uint16_t)(sizeof(s_satir) - 1u));
        (void)f_close(&s_olay);
    }
}

static uint8_t olaylari_yaz(const saat_t *s)
{
    uint16_t n;
    if (Guvenlik_SD_Bekleyen() == 0u) return 1;
    if (!olay_dosyasi_ac(s)) return 0;                  /* olaylar kuyrukta kalir */
    while ((n = Guvenlik_SD_Satiri(s_satir, sizeof(s_satir))) > 0u) {
        if (!yaz(&s_olay, s_satir, n)) { (void)f_close(&s_olay); return 0; }
    }
    return (uint8_t)(f_close(&s_olay) == FR_OK);
}

static uint8_t veri_dosyasi(const saat_t *s)
{
    if (s_veri_acik && s->gun == s_gun) return 1;
    if (s_veri_acik) { (void)f_close(&s_veri); s_veri_acik = 0; }   /* gun degisti */
    klasorler(s);
    (void)snprintf(s_yol, sizeof(s_yol), "%04u/%02u_%04u/%02u_%02u_%04u_LOG.CSV",
                   (unsigned)s->yil, (unsigned)s->ay, (unsigned)s->yil,
                   (unsigned)s->gun, (unsigned)s->ay, (unsigned)s->yil);
    if (f_open(&s_veri, s_yol, FA_OPEN_APPEND | FA_WRITE) != FR_OK) return 0;
    s_veri_acik = 1;
    s_gun = s->gun;
    s_baslik_hucre = 0xFFFFu;                           /* her oturum basinda baslik */
    s_baslik_kaynak = 0xFFu;
    s_son_kaynak = 0xFFu;
    s_sic_set = 0u;
    return 1;
}

/* ---------------------------------------------------------------- ana is */
void SD_Log_Process(void)
{
    uint32_t now = HAL_GetTick(), periyot;
    saat_t s;
    char zaman[24];
    uint8_t kaynak, maske;
    uint16_t n, hucre;

    acilis_al();
    sd_kart_var = kart_var();
    if (!sd_kart_var) {
        if (s_takili) kart_cikti();
        s_cd_onceki = 0;
        sd_durum = 0;
        return;                                          /* olaylar Guvenlik kuyrugunda bekler */
    }
    if (s_cd_onceki < SD_CD_OTURMA) {                    /* yeni takildi: oturmasini bekle */
        s_cd_onceki++;
        if (s_cd_onceki < SD_CD_OTURMA) { sd_durum = 0; return; }
        s_sonraki_deneme = now;                          /* oturdu: hemen bagla */
    }

    if (!s_takili) {
        if ((int32_t)(now - s_sonraki_deneme) < 0) return;
        disk.is_initialized[0] = 0;          /* ST diskio karti bir kez baslatir: yeniden dene */
        if (f_mount(&s_fs, "", 1) != FR_OK) {
            (void)f_mount(NULL, "", 0);
            sd_durum = 0;
            s_sonraki_deneme = now + SD_TEKRAR_SN * 1000u;
            return;
        }
        s_takili = 1;
        sd_durum = 1;
        s_son_sync = now;
        saat_dosyasi();
    }

    if (!Saat_Oku(&s) || !Saat_Gecerli(&s)) { s.yil = 2000; s.ay = 1; s.gun = 1; s.saat = s.dk = s.sn = 0; }
    Saat_Metin(&s, zaman, sizeof(zaman));

    acilis_yaz(&s);
    if (!olaylari_yaz(&s)) { sd_birak(); return; }

    maske   = Guvenlik_Aktif_Maske();
    periyot = (maske ? SD_ALARM_PERIYOT_SN : SD_KAYIT_PERIYOT_SN) * 1000u;
    if ((uint32_t)(now - s_son_satir) + 50u >= periyot) {
        s_son_satir = now;
        if (!veri_dosyasi(&s)) { sd_birak(); return; }
        kaynak = BMS_Aktif_Kopya(&s_b);
        if (kaynak == BMS_KAYNAK_CAN && !BMS_CAN_Tam(now)) {
            /* acilis / yeni BMS: tum kare turleri gelmeden satir yazilmaz (eksik hucre, bos sicaklik) */
        } else if (kaynak == BMS_KAYNAK_YOK) {
            if (s_son_kaynak != BMS_KAYNAK_YOK) {        /* veri kesildi: bir kez isaretle */
                n = (uint16_t)snprintf(s_satir, sizeof(s_satir), "%s,YOK\n", zaman);
                if (!yaz(&s_veri, s_satir, n)) { sd_birak(); return; }
            }
            s_sic_set = 0u;                              /* veri kesildi: yeni oturumda probler yeniden */
        } else {
            hucre = (s_b.cell_count > BMS_HUCRE_MAX) ? (uint16_t)BMS_HUCRE_MAX : s_b.cell_count;
            if (kaynak != s_baslik_kaynak) s_sic_set = 0u;
            s_sic_set |= BMS_Sicak_Gecerli(&s_b);        /* bir kez gorulen prob sutunu kalir ("-") */
            s_b.sicak_mm_ok = Guvenlik_Sicaklik(&s_b.sicak_maks, &s_b.sicak_min);
            if (hucre != s_baslik_hucre || kaynak != s_baslik_kaynak || s_sic_set != s_baslik_sic) {
                n = (uint16_t)snprintf(s_satir, sizeof(s_satir), "Saat kaynagi %s, BMS kaynagi %s, %u hucre\n",
                                       Saat_Kaynak_Adi(), BMS_Kaynak_Adi(kaynak), (unsigned)hucre);
                if (!yaz(&s_veri, s_satir, n)) { sd_birak(); return; }
                n = BMS_CSV_Baslik(s_satir, sizeof(s_satir), hucre, s_sic_set);
                if (!yaz(&s_veri, s_satir, n)) { sd_birak(); return; }
                s_baslik_hucre = hucre;
                s_baslik_kaynak = kaynak;
                s_baslik_sic = s_sic_set;
            }
            n = BMS_CSV_Satir(s_satir, sizeof(s_satir), zaman, kaynak, &s_b, maske, s_sic_set);
            if (!yaz(&s_veri, s_satir, n)) { sd_birak(); return; }
            sd_satir_sayisi++;
        }
        s_son_kaynak = kaynak;
        sd_durum = 2;
    }

    if ((uint32_t)(now - s_son_sync) >= SD_SYNC_SN * 1000u) {
        s_son_sync = now;
        if (s_veri_acik && f_sync(&s_veri) != FR_OK) sd_birak();
    }
}
