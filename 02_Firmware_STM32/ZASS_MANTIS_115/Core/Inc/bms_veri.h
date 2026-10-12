/* ============================================================================
 * ZASS MANTIS - BMS VERI KATMANI  (bms_veri.h + bms_veri.c)
 * ----------------------------------------------------------------------------
 * BMS verisi iki yoldan gelir; hangisi gelirse gelsin ayni bms_data_t'ye cozulur:
 *   CAN   : FDCAN1, main.c CAN_Mesajini_Coz()  -> my_bms_can  (+ Guvenlik_Kare)
 *   UART6 : 2400 baud, DWIN tipi paket         -> my_bms      (+ Guvenlik_Olcum)
 * CAN onceliklidir: ekran, guvenlik kurallari ve SD kaydi ayni kurali izler
 * (CAN tazeyse CAN, degilse UART6).
 *
 * UART6 paketi (96 bayt):  A5 5A 5D 82 10 00 | 90 bayt veri, kelimeler BIG-ENDIAN
 *   +0 paket V x100   +2 akim 0.1 A (isaretli, sarj +)   +4 (kullanilmiyor)
 *   +6 SOC %   +8 hucre farki mV   +10 MOS C   +12 batarya C   +14 uyari bitleri
 *   +16 ort. hucre mV   +18 balans   +20 sarj MOS   +22 desarj MOS
 *   +24.. hucre mV (pakette 33 kelimelik yer var; ilk BMS_HUCRE_MAX okunur)
 * CAN hucre kareleri 0x18E0xxF4..0x18E6xxF4: kare basina 4 hucre (mV, LE). E0..E5 = 24 hucre
 * (protokol V2.1); 25. hucre E6 karesinin ilk 2 baytinda VARSAYILDI (ayni duzen).
 * Hucre sayisi = sifir olmayan EN SON hucrenin sirasi (aradaki 0 mV da sayilir,
 * sutunlar kaymasin diye).
 * ========================================================================== */
#ifndef BMS_VERI_H
#define BMS_VERI_H

#include "main.h"

#define BMS_UART_PAKET   96u      /* baslik 6 + veri 90                       */

enum { BMS_KAYNAK_YOK = 0, BMS_KAYNAK_CAN = 1, BMS_KAYNAK_UART = 2 };

/* CAN karesini cozer (main.c CAN_Mesajini_Coz cagirir; kesme baglami). 1 = BATT_ST1
 * karesiydi (CAN canli). now = HAL_GetTick(): bir hucre karesi BMS_HUCRE_ESKI_MS boyunca
 * gelmezse o hucreler sifirlanir (25S BMS'in yerine 16S takilirsa eski hucre kalmasin). */
#define BMS_HUCRE_ESKI_MS  5000u
uint8_t     BMS_CAN_Coz(uint32_t id, uint8_t ext, const uint8_t *d, bms_data_t *b, uint32_t now);

/* CAN oturumu TAM mi? SD kaydi CAN'den ancak tum kare turleri (BATT_ST1, hucre max/min,
 * CELL_TEMP, ALL_TEMP, SwSta, hucre kareleri) en az bir kez geldikten ve hucre kareleri bir
 * tur (BMS_HUCRE_TUR_MS) tamamladiktan sonra baslar: acilista eksik hucre sayisi / bos sicaklik
 * yazilmaz. Bir kare turu hic gelmiyorsa BMS_TAM_ZAMAN_ASIMI_MS sonra yine de kaydedilir.
 * CAN BMS_OTURUM_KOPUK_MS susarsa oturum sifirlanir (yeni BMS'te yeniden beklenir).        */
#define BMS_HUCRE_TUR_MS         1200u
#define BMS_TAM_ZAMAN_ASIMI_MS   5000u
#define BMS_OTURUM_KOPUK_MS      3000u
uint8_t     BMS_CAN_Tam(uint32_t now);

/* UART6 tamponunda paketi arar ve cozer. 1 = paket bulundu, b dolduruldu.
 * Bos/cop paket (0 V ve hic hucre yok) reddedilir; b'ye dokunulmaz.          */
uint8_t     BMS_UART_Coz(const uint8_t *buf, uint16_t n, bms_data_t *b);

/* UART6'dan cozulen veriyi guvenlik kurallarina verir (CAN canliyken modul yok sayar). */
void        BMS_UART_Guvenlige(const bms_data_t *b);

/* Hucrelerden max/min (0 mV atlanir). 0 = gecerli hucre yok.                 */
uint8_t     BMS_Hucre_MaxMin(const bms_data_t *b, uint16_t *mx, uint16_t *mn);

/* Etkin kaynak (BMS_KAYNAK_*) + tutarli kopya (kesmeler birkac us kapali).    */
uint8_t     BMS_Aktif_Kopya(bms_data_t *out);
const char *BMS_Kaynak_Adi(uint8_t kaynak);

/* SD satirlari - sadece tamsayi bicimleme (newlib float/malloc kullanilmaz).
 * Donus: yazilan bayt (sonundaki '\n' dahil), 0 = sigmadi.                    */
/* sic_set: sutunu yazilacak batarya problari (bit0 T1, bit1 T2, bit3 T4, bit4 T5; T3 = MOS
 * zaten "MOS C" sutununda). Oturumda hic gecerli okunmamis yuva (ornegin BMS'in bos yuvaya
 * gonderdigi 0 = -50 C) yazilmaz. Gecerli sicaklik: GVN_SICAK_GECERLI_ALT..UST.            */
uint16_t    BMS_CSV_Baslik(char *buf, uint16_t boyut, uint16_t hucre, uint8_t sic_set);
uint16_t    BMS_CSV_Satir(char *buf, uint16_t boyut, const char *zaman, uint8_t kaynak,
                          const bms_data_t *b, uint8_t gvn_maske, uint8_t sic_set);
/* b'deki gecerli batarya problari (sic_set bit duzeni) */
uint8_t     BMS_Sicak_Gecerli(const bms_data_t *b);

#endif /* BMS_VERI_H */
