/* ============================================================================
 * ZASS MANTIS - GUVENLIK UYARI MODULU  (guvenlik.h + guvenlik.c, TEK CIFT)
 * ----------------------------------------------------------------------------
 * JK/ZASS BMS'in CAN verisinden 8 guvenlik kuralini izler; DWIN ekranini,
 * buzzer'i ve kalici olay kaydini (garanti kaniti) yonetir.
 *
 * Veri iki kaynaktan gelebilir: ham CAN kareleri (Guvenlik_Kare) ya da UART6
 * BMS paketi (Guvenlik_Olcum, bms_veri.c). CAN canliyken CAN onceliklidir.
 * Entegrasyon -> GUVENLIK.md
 *
 *   NO  KURAL                                   EKRAN (hepsi ANA SAYFADA)   BUZZER
 *   1   asiri sarj + sarj kontaktoru yapisik    POP-UP (ana sayfa ustu)     surekli
 *   2   asiri desarj / desarj kont. yapisik     POP-UP                      surekli
 *   3   hucre farki >= 0.3 V                    serit, sonuk, 15 dk         yok
 *   4   sicaklik > 55 C                         serit                       5 sn'de bir
 *   5   sicaklik > 60 C                         POP-UP                      surekli
 *   6   sicaklik < -10 C                        POP-UP                      surekli
 *   7   haberlesme yok > 5 sn                   POP-UP                      5 sn'de bir
 *   8   sicaklik sensoru okumuyor (NA)          serit                       5 sn'de bir
 *
 * SAYFA DEGISTIRILMEZ. Ana sayfaya (100) sadece 2 ikon eklenir; ikisi de
 * "Transparence" modunda (siyah = seffaf), mevcut kontrollerin hicbiri oynamaz:
 *   serit  : sicaklik kartlarinin etiket satirindan acilan 296x22'lik yer
 *   pop-up : tum sayfa (320x240), kare 0 = tamamen siyah = gorunmez
 *
 * Birim kurali: hucre gerilimi mV, akim 0.1 A (dA, sarj +, desarj -), sicaklik C.
 * ========================================================================== */
#ifndef GUVENLIK_H
#define GUVENLIK_H

#include <stdint.h>
#include "stm32g0xx_hal.h"

/* ==========================================================================
 * 1) EKRAN (DGUS)  -  ekran/ klasorundeki uretici bu degerleri BURADAN okur
 * ========================================================================== */
/* VP adresleri (mevcut 0x1000..0x1110 ve eski alarm_fsm 0x13xx ile CAKISMAZ) */
#define GVN_VP_SERIT    0x1402   /* Icon Var : ICL 50  serit    296x22   kare 0..6   */
#define GVN_VP_POPUP    0x1404   /* Icon Var : ICL 51  pop-up   320x240  kare 0..10  */
/* ICL NUMARASI: panelin 16 MB flash'i 256 KB'lik 64 bloktur -> dosya no 0..63, ICL 16..63.
 * 64 ve ustu panelde YOK SAYILIR (ikon hic cizilmez, hata da vermez). 256 KB'tan buyuk dosya
 * ardisik bloklari da kaplar: 51.icl (~513 KB) = 51, 52, 53. Dolu: 32.icl 32..40, 55.icl 55;
 * eski V2 taslagi (ACIL-DURUM) 56..60'i ayirmisti: 50/51 hicbiriyle cakismaz.                 */

/* ICL 50 (serit) kareleri */
#define GVN_SERIT_BOS     0      /* hic yazi yok (acilis, BMS verisi yok)            */
#define GVN_SERIT_NORMAL  1      /* "Batarya durumu normal" (sonuk)                  */
#define GVN_SERIT_K4      2      /* sicaklik yuksek                                  */
#define GVN_SERIT_K8      3      /* sensor hatasi                                    */
#define GVN_SERIT_K3      4      /* hucre direnc/denge uyarisi (sonuk, 15 dk)        */
#define GVN_SERIT_BILGI   5      /* pop-up acikken: SOC % | V | A | C etiketleri       */
#define GVN_SERIT_VERIYOK 6      /* pop-up acik ama BMS verisi yok (kural 7)          */
/* ICL 51 (pop-up) : kare 0 = YOK (tamamen siyah, seffaf).
 *   kare = 1 + sira*2 + faz   (faz 0/1 yanip sonme)
 *   sira 0 = kural 1   1 = kural 2   2 = kural 5   3 = kural 6   4 = kural 7       */
#define GVN_POPUP_YOK   0
#define GVN_BLINK_MS    500u

/* --- BILGI CUBUGU: kritik pop-up acikken en altta CANLI SOC / voltaj / akim / sicaklik ---
 * Rakamlar DGUS'ta 4 yeni Data Variable: MEVCUT VP'leri okurlar (0x1000, 0x1060, 0x1080,
 * 0x1040; degerleri main.c zaten yaziyor), ama her birinin YENI SP'si var. Modul sadece
 * renklerini (SP+3) yazar: pop-up yokken serit kartinin rengi = gorunmez, pop-up varken beyaz.
 * Mevcut kontrollerin SP'lerine (0x5000..0x5080) dokunulmaz.                         */
#define GVN_BILGI_CUBUGU     1
#define GVN_SP_MINI_SOC      0x5200
#define GVN_SP_MINI_V        0x5220
#define GVN_SP_MINI_A        0x5240
#define GVN_SP_MINI_T        0x5260
#define GVN_MINI_RENK_OFS    3          /* Data Variable SP'sinde renk = SP+3 (main.c'deki gibi) */
#define GVN_MINI_ACIK        0xF79E     /* RGB565 (240,242,246) beyaz                        */
#define GVN_MINI_GIZLI       0x18E5     /* serit kartina (28,31,38) en yakin RGB565 (yedek)    */
/* Renk kamuflaji panelde tam tutmuyor (gizli rakam golge gibi seciliyordu): gizliyken rakamin
 * Y'si (SP+2) ekran disina alinir, kritik pop-up'ta GVN_MINI_Y'ye (DGUS'ta girilen Y) doner. */
#ifndef GVN_MINI_KONUMLA_GIZLE
#define GVN_MINI_KONUMLA_GIZLE 1
#endif
#define GVN_MINI_Y_OFS       2          /* Data Variable SP'sinde Y = SP+2                    */
#define GVN_MINI_Y           209        /* gorunurken                                         */
#define GVN_MINI_Y_GIZLI     300        /* 240'in altinda: panel cizmez                       */

/* --- TANI: ekran yolunu DGUS ikonlarindan BAGIMSIZ sinamak icin ---------------------------
 * 1 yapinca modul SOC sayisinin (VP 0x1000, mevcut ve calisan kontrol) USTUNE sunu yazar:
 *   11        modul calisiyor ve DWIN'e yaziyor (acilistan itibaren)
 *   90 + kare pop-up karesi; haberlesme hatasi = 99 / 100 (0,5 sn'de bir degisir)
 * Sayilar goruluyorsa firmware-UART-DWIN zinciri tamam, sorun DGUS tarafindadir
 * (ICL 64/65 sorunu bu taniyla bulundu). Normalde 0: SOC'a dokunmaz.                       */
#ifndef GVN_EKRAN_YOKLAMA
#define GVN_EKRAN_YOKLAMA    0
#endif

/* Ayni anda birden cok uyari varsa ekran bu surede bir digerine gecer.       */
#define GVN_DONGU_MS    4000u

/* ==========================================================================
 * 2) BUZZER   (mevcut kodda PA10 ve PB2 birlikte suruluyor, aktif HIGH)
 * ========================================================================== */
#define GVN_BUZ_A_PORT   GPIOA
#define GVN_BUZ_A_PIN    GPIO_PIN_10
#define GVN_BUZ_B_PORT   GPIOB
#define GVN_BUZ_B_PIN    GPIO_PIN_2
#define GVN_BUZ_BIP_MS       400u    /* "5 sn'de bir" deseninde bip suresi      */
#define GVN_BUZ_PERIYOT_MS  5000u

/* ==========================================================================
 * 3) ESIKLER  (tablodaki degerler; histerezis = kapanma esigi)
 * ========================================================================== */
/* --- 1: ASIRI SARJ + SARJ KONTAKTORU YAPISIK -------------------------------
 *   en yuksek hucre > 3.7 V  VE  sarj kontaktoru KESIK (BMS acti)  VE
 *   akim > +5 A, hepsi 5 sn kesintisiz.
 *   GVN_R1_VEYA = 1 -> "VE" yerine "VEYA" (hucre tek basina da yeter).        */
#define GVN_R1_HUCRE_MV       3700
#define GVN_R1_HUCRE_BIRAK    3650
#define GVN_R1_AKIM_dA        50
#define GVN_R1_AKIM_BIRAK_dA  30
#define GVN_R1_SURE_MS        5000u
#ifndef GVN_R1_VEYA
#define GVN_R1_VEYA           0
#endif

/* --- 2: ASIRI DESARJ  /  DESARJ KONTAKTORU YAPISIK -------------------------
 *   en dusuk hucre < 2.6 V (5 sn)   VEYA
 *   desarj kontaktoru KESIK ve akim < -5 A (5 sn)                             */
#define GVN_R2_HUCRE_MV       2600
#define GVN_R2_HUCRE_BIRAK    2700
#define GVN_R2_AKIM_dA        (-50)
#define GVN_R2_AKIM_BIRAK_dA  (-30)
#define GVN_R2_SURE_MS        5000u

/* --- 3: HUCRE DENGESIZLIGI -------------------------------------------------
 *   (en yuksek - en dusuk hucre) >= 0.3 V  2 sn  -> seritte 15 dk sonuk bildirim.
 *   15 dk dolunca bildirim kalkar; durum iyilesip tekrar bozulmadan
 *   (veya kontak kapanip acilmadan) ayni uyari TEKRAR GOSTERILMEZ.            */
#define GVN_R3_DELTA_MV       300
#define GVN_R3_DELTA_BIRAK_MV 250
#define GVN_R3_SURE_MS        2000u
#define GVN_R3_GOSTER_MS      (15u * 60u * 1000u)

/* --- 4 / 5 / 6: SICAKLIK  (sure tabloda yok: 2 sn secildi, anlik sapmasin) --
 *   4: batarya sicakligi > 55 C     5: > 60 C     6: en dusuk hucre < -10 C   */
#define GVN_R4_SICAK_C        55
#define GVN_R4_BIRAK_C        53
#define GVN_R5_SICAK_C        60
#define GVN_R5_BIRAK_C        57
#define GVN_R6_SOGUK_C        (-10)
#define GVN_R6_BIRAK_C        (-8)
#define GVN_R456_SURE_MS      2000u
/* Maks / min sicaklik (kural 4, 5, 6 ve ekran) = BMS'in CELL_TEMP max/min'i + ALL_TEMP'teki
 * tum gecerli batarya problari (T1 T2 T4 T5). 0 = sadece BMS'in max/min'i (eski davranis). */
#ifndef GVN_SICAK_TUM_SENSOR
#define GVN_SICAK_TUM_SENSOR  1
#endif
/* 1 yaparsan BMS'in MOS sicakligi (T3) da maks/min'e katilir (varsayilan: sadece hucre). */
#ifndef GVN_MOS_DAHIL
#define GVN_MOS_DAHIL         0
#endif

/* --- 7: HABERLESME ---------------------------------------------------------
 *   BATT_ST1 (0x2F4, 20 ms) 5 sn'den uzun suredir gelmiyor.                   */
#define GVN_R7_SURE_MS        5000u
#define GVN_R7_TOPARLA_MS     2000u     /* veri geri geldikten sonra bu kadar bekle */
/* Acilista BMS ilk kareyi gondermeden once bu kadar EK sure taninir (varsayilan 0:
 * tablo gibi, acilistan 5 sn). Her acilista kisa yanlis alarm goruyorsan artir. */
#ifndef GVN_R7_ILK_EK_MS
#define GVN_R7_ILK_EK_MS      0u
#endif

/* --- 8: SICAKLIK SENSORU EKSIK -------------------------------------------
 *   Kaynagin gondermesi gereken sicaklik sensoru sayisi (kullanici karari 2026-10-09):
 *     CAN  (ALL_TEMP 0x18F228F4, T1..T5): en az GVN_SENSOR_CAN_ADET gecerli sensor
 *     UART (paket: batarya + MOS sicakligi; T1 = batarya, T3 = MOS): en az GVN_SENSOR_UART_ADET
 *   Gecerli = deger GVN_SICAK_GECERLI_ALT..UST araliginda (protokol: 0xFF = sensor yok).
 *   TempMaskCode SAYILMAZ: sahada (2026-10-09, 16S CAN) T1/T2 maske/yuva uyusmazligi
 *   suphesi; maske yalnizca SD'ye ve gvn_sicak_ham[0]'a yazilir.
 *   Daha azsa 5 sn sonra kural 8 ("SENSOR HATASI").                              */
#ifndef GVN_SENSOR_CAN_ADET
#define GVN_SENSOR_CAN_ADET   3u
#endif
#ifndef GVN_SENSOR_UART_ADET
#define GVN_SENSOR_UART_ADET  2u
#endif
#define GVN_R8_SURE_MS        5000u
/* Bu aralik disindaki sicaklik okumasi "sensor yok" sayilir. */
#define GVN_SICAK_GECERLI_ALT (-45)
#define GVN_SICAK_GECERLI_UST 125

/* Bir BMS karesi bu sureden uzun suredir gelmediyse o veri "BILINMIYOR" sayilir
 * (kural durumu dondurulur, sayaclar sifirlanir).                             */
#define GVN_TAZE_MS           3000u
/* Kosul kalktiktan sonra uyari bu sure daha acik kalir (titremesin).         */
#define GVN_BIRAK_MS          3000u

/* ==========================================================================
 * 4) KAYIT (flash olay gunlugu)
 * ==========================================================================
 * Flash'in SON 2 SAYFASI (128 KB'ta 0x0801F000 ve 0x0801F800), halka olarak.
 * Sayfa basina 127 kayit; yeni sayfa dolunca digeri silinir (en az 127 olay hep
 * saklanir). Firmware bu sayfalara ulasirsa kayit kendini kapatir.
 * Ayni olay 60 sn icinde tekrar baslarsa YENI kayit acilmaz (birlestirilir).   */
#define GVN_LOG_FLASH         1
#define GVN_LOG_BIRLESTIR_MS  60000u
/* 1 (kullanici karari 2026-10-09): olay BASLADIGINDA tek kayit - ne oldu, ne zaman, hangi hucre.
 * "devam ediyor" / "bitti" kaydi YAZILMAZ. 0 = eski ayrintili kayit (BASLADI / DEVAM / BITTI). */
#ifndef GVN_LOG_SADECE_OLAY
#define GVN_LOG_SADECE_OLAY   1
#endif
/* Surecek olaylar icin ara kayit (DEVAM): 1 dk, 5 dk, 15 dk, 1 sa, sonra saatte bir.
 * Maske bit0 = kural 1 ... (varsayilan 1,2,5,6: garantiyi ilgilendirenler)    */
#define GVN_LOG_DEVAM_MASKE   0x33

/* Debugger'dan ekran/buzzer testi:  gvn_test_maske = 0x01 (kural 1) ... 0x80 (kural 8)
 * birden cok bit = birden cok uyari. 0 = normal. SAHAYA CIKARKEN 0 YAP.       */
#define GVN_TEST_KANCASI      1
/* gvn_test_maske'nin ACILIS degeri = debugger'siz ekran testi. Ornek 0x40: haberlesme
 * hatasi ekrani (kural 7) BMS ne derse desin acilistan itibaren gosterilir (buzzer dahil,
 * flash'a kayit YAZILMAZ). Normal calisma 0x00.  SAHAYA CIKARKEN 0x00.         */
#ifndef GVN_TEST_MASKE_ILK
#define GVN_TEST_MASKE_ILK    0x00u
#endif

/* ==========================================================================
 * 5) API
 * ========================================================================== */
/* dwin_uart = DWIN'in bagli oldugu UART (huart3), rtc = &hrtc. Scheduler'dan ONCE. */
void     Guvenlik_Init(UART_HandleTypeDef *dwin_uart, RTC_HandleTypeDef *rtc);

/* BMS'ten gelen HER CAN karesini ver (FDCAN rx callback'inden, ID'nin son byte'i
 * F4/F5 olanlar). Cok hafif: sadece kopyalar, kesme icinde cagrilabilir.      */
void     Guvenlik_Kare(uint32_t id, uint32_t ext, const uint8_t *d);

/* CAN DISI KAYNAK (UART6 BMS paketi vb.): olcumleri dogrudan ver. CAN canliyken
 * (son BATT_ST1 GVN_TAZE_MS icinde) yok sayilir: CAN onceliklidir, CAN susunca bu
 * kaynak devralir. Bilinmeyen alani "yok" birak; o alana bagli kural bu kaynaktan
 * tetiklenmez. Kesmeden de gorevden de cagrilabilir.                          */
#define GVN_OLC_YOK_C   (-127)   /* sicaklik bu kaynakta YOK (alan gonderilmiyor)   */
#define GVN_OLC_NA_C    (-126)   /* sensor okumuyor (NA) -> kural 8                 */
typedef struct {
    uint16_t paket_dV;           /* 0.1 V                                           */
    int16_t  akim_dA;            /* 0.1 A, sarj +, desarj -                         */
    uint8_t  soc;                /* %                                               */
    uint16_t hucre_max_mV;       /* 0 = bilinmiyor (ikisi birlikte verilir)         */
    uint16_t hucre_min_mV;
    uint8_t  hucre_max_no;       /* en yuksek / en dusuk hucrenin sirasi (1..), 0 = bilinmiyor */
    uint8_t  hucre_min_no;
    int16_t  sicak_max_C;        /* hucre sicakligi C; YOK / NA isaretleri yukarida  */
    int16_t  sicak_min_C;
    int16_t  mos_C;              /* kural 8 sensor sayimi; sicaklik kurallarina sadece GVN_MOS_DAHIL 1 iken */
    int8_t   sarj_mos;           /* 1 iletimde, 0 kesik (BMS acti), -1 bilinmiyor    */
    int8_t   desarj_mos;
    int8_t   sarj_fisi;          /* 1 takili, 0 degil, -1 bilinmiyor                 */
} gvn_olcum_t;
void     Guvenlik_Olcum(const gvn_olcum_t *o);

/* Kurallari su an besleyen kaynak: 0 = taze veri yok, 1 = CAN, 2 = Guvenlik_Olcum. */
uint8_t  Guvenlik_Kaynak(void);

/* ~100 ms'de bir, DWIN'e yazan gorevden (StartDefaultTask dongusunun sonu).   */
void     Guvenlik_Task(void);

/* 1 = buzzer pinini modul yonetiyor; eski buzzer kodu o sirada SUSMALI.       */
uint8_t  Guvenlik_Buzzer_Aktif(void);

/* Su an ekranda aktif olan kurallar: bit0 = kural 1 ... bit7 = kural 8.
 * SD log satirina sutun olarak eklenebilir.                                   */
uint8_t  Guvenlik_Aktif_Maske(void);

/* Hangi BMS kareleri canli (son 3 sn): bit0 ST1  bit1 CELL_VOLT  bit2 CELL_TEMP
 * bit3 ALL_TEMP  bit4 SwSta.  Bir kural "hic tetiklenmiyor" ise ilk bakilacak yer. */
uint8_t  Guvenlik_Hat_Maske(void);

/* Bekleyen olay satirini CSV olarak yazar (SD'ye eklemek icin). Donen deger =
 * yazilan byte, 0 = bekleyen yok. Satir: "YYYY.MM.DD HH:MM:SS,KURAL,TIP,SOC,A,B,SURE\n" */
uint16_t Guvenlik_SD_Satiri(char *buf, uint16_t boyut);
/* Kuyrukta bekleyen olay satiri sayisi (SD dosyasini acmadan once bakmak icin). */
uint8_t  Guvenlik_SD_Bekleyen(void);
/* Ekran icin son hesaplanan maks / min sicaklik (C). Donus: bit0 maks gecerli, bit1 min gecerli;
 * gecersiz olan cikis degistirilmez (cagiran yedek degeri birakir). */
uint8_t  Guvenlik_Sicaklik(int16_t *maks, int16_t *min);
/* Ekrana yazilacak sicakliklar: s1 (SICAKLIK 1, 0x1020) = maks; s2 (SICAKLIK 2, 0x1040) = min.
 * 0x1040'i pop-up'in altindaki bilgi cubugu da gosterir: pop-up acikken (kural 6 haric) s2 =
 * maks (asiri sicaklikta bilgi cubugu en sicagi gostersin; SICAKLIK 2 karti pop-up altinda). */
uint8_t  Guvenlik_Ekran_Sicaklik(int16_t *s1, int16_t *s2);

extern volatile uint8_t gvn_test_maske;

#endif /* GUVENLIK_H */
