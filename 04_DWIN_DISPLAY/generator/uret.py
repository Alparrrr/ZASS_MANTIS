# -*- coding: utf-8 -*-
"""
ZASS MANTIS - EKRANLAR (SADE SURUM)

Toplam 2 zorunlu + 1 opsiyonel yeni sayfa:

  100  ANA SAYFA        mevcut, DOKUNULMAZ
  101  ANA SAYFA + HATA sicakliklar tek satira daraldi, altta mesaj seridi,
                        header sagina ariza kodu. SOC/voltaj/akim AYNI YERDE.
  102  ACIL DURUM       tek sayfa; kritik / geri sayim / kilit / manevra
                        durumlarini alt serit ile ayirir.
  103  SERVIS           OPSIYONEL - diagnostik + son 3 hata kodu.

URETIR
  sayfalar/101..103.bmp      -> DWIN_SET
  sayfalar/onizleme/*.png    -> ornek degerlerle gorunum
  mesajlar/bmp/00..20.bmp    -> ICL 56  (Icon Var 0x1302)  296x38
  altserit/bmp/0..6.bmp      -> ICL 57  (Icon Var 0x1305)  296x54
  kritik/bmp/0..1.bmp        -> ICL 58  (Icon Var 0x1304)  320x72
  kilavuz/*.png              -> DGUS kontrol yerlesim haritasi

55.icl'ye DOKUNULMUYOR - mevcut durum cipi aynen kalir.

Calistir:  python uret.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

BASE = os.path.dirname(os.path.abspath(__file__))
W, H, SS = 320, 240, 4
F = "C:/Windows/Fonts/"

# ---- home_card.py ile AYNI palet ----
BG      = (14, 16, 20)
CARD    = (28, 31, 38)
CARD_HI = (40, 44, 53)
CARD_E  = (52, 56, 66)
INK     = (237, 239, 243)
MUTED   = (138, 144, 156)
ORANGE  = (240, 138, 34)
AMBER   = (255, 186, 64)
TRACK   = (44, 48, 57)
RED     = (255, 116, 106)

# ---- acil sayfasi kirmizi tema ----
BG_K    = (30, 11, 10)
CARD_K  = (52, 19, 17)
CARD_EK = (96, 34, 30)
HI_K    = (80, 28, 25)
SOFT_K  = (200, 140, 134)
PALE_K  = (255, 200, 194)
RED_HOT = (255, 92, 80)


def fnt(n, s): return ImageFont.truetype(F + n, int(s * SS))
def B(s): return fnt("segoeuib.ttf", s)
def R(s): return fnt("segoeui.ttf", s)


def yeni(bg=BG):
    im = Image.new("RGB", (W * SS, H * SS), bg)
    return im, ImageDraw.Draw(im)


def txt(d, xy, s, f, fill, anchor="la"):
    d.text((xy[0] * SS, xy[1] * SS), s, font=f, fill=fill, anchor=anchor)


def rrect(d, box, r, fill=None, outline=None, w=1):
    d.rounded_rectangle([c * SS for c in box], radius=r * SS,
                        fill=fill, outline=outline, width=max(1, w * SS))


def kart(d, box, r=10, fill=CARD, kenar=CARD_E, vurgu=CARD_HI):
    rrect(d, box, r, fill=fill, outline=kenar, w=1)
    x0, y0, x1, _ = box
    d.line([((x0 + 8) * SS, (y0 + 1) * SS), ((x1 - 8) * SS, (y0 + 1) * SS)],
           fill=vurgu, width=SS)


def baslik(d, kenar=CARD_E):
    txt(d, (14, 12), "ZASS", B(15), INK, "lm")
    zw = d.textlength("ZASS", font=B(15)) / SS
    txt(d, (14 + zw + 6, 12), "LITHIUM", B(15), ORANGE, "lm")
    d.line([(12 * SS, 26 * SS), ((W - 12) * SS, 26 * SS)], fill=kenar, width=SS)


def kucult(d, s, f, maks, yap, alt=8):
    while d.textlength(s, font=f) / SS > maks and f.size > alt * SS:
        f = yap((f.size - SS) / SS)
    return f


def kaydet(im, klasor, ad, w, h):
    im = im.resize((w, h), Image.LANCZOS)
    for a, e in (("png", ".png"), ("bmp", ".bmp")):
        dd = os.path.join(BASE, klasor, a)
        os.makedirs(dd, exist_ok=True)
        im.save(os.path.join(dd, ad + e))
    return im


# ==========================================================================
#  ICL 56 - MESAJ SERITLERI   296x38   (indeks = alarm_fsm.h MSG_*)
# ==========================================================================
MW, MH = 296, 38
GREEN   = (72, 226, 150)
C_SEV = {0: MUTED, 1: AMBER, 2: RED, 3: RED_HOT, 4: GREEN}

MESAJLAR = [
    ("",                               "",                                        0),  # 0
    ("BATARYA AZALDI",                 "Şarj istasyonuna yönelin",                1),  # 1
    ("SICAKLIK YÜKSELİYOR",            "Yükü azaltın",                            1),  # 2
    ("HÜCRELER DENGESİZ",              "Servis kaydı oluşturuldu",                1),  # 3
    ("YÜK YÜKSEK",                     "Limite yaklaşıyorsunuz",                  1),  # 4
    ("AŞIRI YÜKLENME",                 "Limit aşıldı — güç korumaya alındı",      2),  # 5
    ("KISA DEVRE KORUMASI",            "Aracı güvenli alana park edin",           2),  # 6
    ("BATARYA BOŞ",                    "Güç kesildi — şarj edin",                 2),  # 7
    ("AŞIRI ŞARJ",                     "Şarj cihazını çıkarın",                   2),  # 8
    ("AŞIRI SICAKLIK",                 "Sistem soğumaya alındı",                  2),  # 9
    ("SICAKLIK ÇOK DÜŞÜK",             "Isınana kadar bekleyin",                  2),  # 10
    ("BAĞLANTI / GÜÇ HATASI",          "Anlık voltaj düşümü — kayıt açıldı",      2),  # 11
    ("BATARYA İLETİŞİMİ YOK",          "Servisi arayın",                          2),  # 12
    ("GÜÇ KESİLEMİYOR",                "Batarya anahtarı akımı durduramıyor",     3),  # 13
    ("BATARYA ANAHTARI ARIZALI",       "Güç kesme devresi yanıt vermiyor",        3),  # 14
    ("AKIM ÖLÇÜMÜ GÜVENİLMEZ",         "Servisi arayın",                          2),  # 15
    ("ŞARJ KESİLEMİYOR!",              "ŞARJ CİHAZINI HEMEN ÇIKARIN",             3),  # 16
    ("ACİL DURUM MODU AKTİF",          "Aracı park edip servisi bekleyin",        3),  # 17
    ("30 DAKİKALIK SÜRE DOLDU",        "Araç kilitlendi — servisi arayın",        3),  # 18
    ("BATARYA ÇOK AZALDI",             "Hemen şarj edin",                         1),  # 19
    ("MANEVRA İZNİ AKTİF",             "Aracı güvenli yere çekin",                3),  # 20
    ("BATARYA %5 ALTINDA",             "En yakın şarj istasyonuna ulaşın",        3),  # 21
    ("ŞARJ TAMAMLANDI",                "Kabloyu sökebilirsiniz",                  4),  # 22
    ("ŞARJ OLUYOR",                    "Tahmini süre yukarıda gösteriliyor",      4),  # 23
]


def mesaj_serit(s1, s2, sev):
    im = Image.new("RGB", (MW * SS, MH * SS), BG)            # kose disi = sayfa zemini
    if not s1:
        return im                                            # 0 = tamamen bos
    d = ImageDraw.Draw(im)
    col = C_SEV[sev]
    rrect(d, (0, 0, MW - 1, MH - 1), 8, fill=CARD, outline=CARD_E, w=1)
    d.line([(8 * SS, 1 * SS), ((MW - 8) * SS, 1 * SS)], fill=CARD_HI, width=SS)
    # sol seviye seridi - kartin yuvarlak kosesine tasmasin
    d.rounded_rectangle([1 * SS, 4 * SS, 4 * SS, (MH - 4) * SS], radius=2 * SS, fill=col)
    x0 = 12
    f1 = kucult(d, s1, B(13), MW - x0 - 8, B, 9)
    f2 = kucult(d, s2, R(10), MW - x0 - 8, R, 8)
    txt(d, (x0, 12), s1, f1, col,   "lm")
    txt(d, (x0, 27), s2, f2, MUTED, "lm")
    return im


# ==========================================================================
#  ICL 57 - ALT DURUM SERIDI   296x54   (acil sayfasinin alt bandi)
#    0..3 = "kontagi 3 kez ac-kapat" + kac nokta doldu
#    4    = acil durum modu aktif
#    5    = sistem kilitlendi
#    6    = manevra izni
# ==========================================================================
AW, AH = 296, 54


# NE YAPILACAGI seridi - operatore verilen tek net talimat
ALT = [("ARACI GÜVENLİ ALANA PARK EDİN", "Yetkili servisi arayın",        RED_HOT),  # 0
       ("1 SAAT İÇİNDE ŞARJA TAKIN",     "Aksi halde araç yolda kalır",   AMBER),    # 1
       ("ŞARJ CİHAZINI HEMEN ÇIKARIN",   "Batarya aşırı şarj oluyor",     RED_HOT)]  # 2


def alt_serit(k):
    yesil = (k == 3)
    zem   = (10, 26, 19) if yesil else BG_K
    kartf = (18, 44, 32) if yesil else CARD_K
    kenar = (36, 96, 68) if yesil else CARD_EK
    vurgu = (30, 74, 54) if yesil else HI_K
    im = Image.new("RGB", (AW * SS, AH * SS), zem)           # kose disi = sayfa zemini
    d = ImageDraw.Draw(im)
    rrect(d, (0, 0, AW - 1, AH - 1), 8, fill=kartf, outline=kenar, w=1)
    d.line([(8 * SS, 1 * SS), ((AW - 8) * SS, 1 * SS)], fill=vurgu, width=SS)
    s1, s2, col = ALT[k]
    txt(d, (AW / 2, 20), s1, B(15), col, "mm")
    txt(d, (AW / 2, 39), s2, R(10), (140, 190, 165) if yesil else SOFT_K, "mm")
    return im


# ==========================================================================
#  ICL 58 - KRITIK BANT   320x72   (yanip soner)
# ==========================================================================
KW, KH = 320, 72


# kare 0,1 = KRITIK ARIZA (sonuk / parlak)   kare 2,3 = SARJ BITMEK UZERE
BANT = [("KRİTİK ARIZA",      "ARIZA GİDERİLENE KADAR KULLANMAYIN", (170, 26, 22)),
        ("ŞARJ BİTMEK ÜZERE", "BATARYA KRİTİK SEVİYEDE",            (196, 104, 12))]


def kritik_bant(kare):
    tur, faz = kare // 2, kare % 2
    s1, s2, parlak = BANT[tur]
    zemin = parlak if faz else tuple(int(c * 0.30) for c in parlak)
    yazi  = (255, 255, 255) if faz else tuple(int(c * 0.55 + 90) for c in parlak)
    alt   = (255, 216, 210) if faz else tuple(int(c * 0.40 + 60) for c in parlak)
    im = Image.new("RGB", (KW * SS, KH * SS), zemin)
    d = ImageDraw.Draw(im)
    txt(d, (KW / 2, 27), s1, B(21), yazi, "mm")
    txt(d, (KW / 2, 52), s2, B(11), alt, "mm")
    return im


# ==========================================================================
#  SAYFA 101 - ANA SAYFA + HATA
#  Ust 2/3 sayfa 100 ile BIREBIR AYNI konumda (gecis sicramaz).
#  Degisen: sicakliklar tek satira daraldi, altta mesaj seridi,
#           header saginda ariza kodu (durum cipi yerine).
# ==========================================================================
def sayfa_101():
    im, d = yeni()
    baslik(d)
    txt(d, (250, 13), "ARIZA", R(9), MUTED, "rm")

    # --- SOC karti: sayfa 100 ile ayni geometri ---
    kart(d, (12, 34, 196, 150))
    txt(d, (28, 44), "STATE OF CHARGE", R(11), MUTED, "la")
    txt(d, (184, 66), "%", B(26), MUTED, "ra")
    rrect(d, (26, 124, 176, 140), 5, fill=TRACK, outline=CARD_E, w=1)
    rrect(d, (178, 128, 181, 136), 1, fill=CARD_E)

    # --- VOLTAJ / AKIM: sayfa 100 ile ayni ---
    for box, et in (((204, 34, 308, 91), "VOLTAJ"), ((204, 95, 308, 150), "AKIM")):
        kart(d, box)
        txt(d, (box[0] + 12, box[1] + 10), et, R(10), MUTED, "la")
        txt(d, (box[2] - 12, box[1] + 40), "V" if et == "VOLTAJ" else "A",
            R(12), MUTED, "rm")

    # --- SICAKLIK: iki kart yerine TEK dar satir (72 px -> 34 px) ---
    kart(d, (12, 156, 308, 190))
    txt(d, (26, 173), "MOS", R(10), MUTED, "lm")
    txt(d, (152, 173), "°C", R(10), ORANGE, "rm")
    d.line([(160 * SS, 162 * SS), (160 * SS, 184 * SS)], fill=CARD_E, width=SS)
    txt(d, (176, 173), "BATARYA", R(10), MUTED, "lm")
    txt(d, (298, 173), "°C", R(10), ORANGE, "rm")

    k = [("DATA",  0x1301, 256,   4,  50, 20, "ariza kodu, 3 hane, SAGA hizali"),
         ("DATA",  0x1000,  26,  56, 126, 58, "SOC (mevcut VP), 3 hane, SAGA"),
         ("BAR",   0x100C,  28, 126, 146, 12, "SOC bari (mevcut VP), 0-100"),
         ("FLOAT", 0x1060, 216,  60,  66, 30, "voltaj (mevcut VP), 1 ondalik, SAGA"),
         ("FLOAT", 0x1080, 216, 121,  66, 30, "akim (mevcut VP), 1 ondalik, SAGA"),
         ("DATA",  0x1020,  86, 162,  52, 22, "MOS sicakligi (mevcut VP), SAGA"),
         ("DATA",  0x1040, 232, 162,  52, 22, "batarya sicakligi (mevcut VP), SAGA"),
         ("ICON",  0x1302,  12, 196, 296, 38, "mesaj seridi, ICL 56, kare 0-23")]
    return im, k


# ==========================================================================
#  SAYFA 102 - ACIL DURUM (tek sayfa: kritik / geri sayim / kilit / manevra)
# ==========================================================================
def sayfa_102():
    im, d = yeni(BG_K)
    kart(d, (12, 122, 308, 168), fill=CARD_K, kenar=CARD_EK, vurgu=HI_K)
    txt(d, (26, 134), "ARIZA KODU", R(9), SOFT_K, "la")
    txt(d, (294, 134), "KALAN SÜRE", R(9), SOFT_K, "ra")
    txt(d, (245, 153), ":", B(20), PALE_K, "mm")
    k = [("ICON",  0x1304,   0,   0, 320, 72, "ust bant, ICL 58, kare 0-3 (0-1 ariza, 2-3 sarj az)"),
         ("ICON",  0x1302,  12,  78, 296, 38, "mesaj seridi, ICL 56, kare 0-23"),
         ("DATA",  0x1301,  26, 142,  76, 22, "ariza kodu, 3 hane, SOLA hizali"),
         ("DATA",  0x1310, 196, 140,  44, 26, "dakika, 2 hane, SIFIR ACIK, SAGA"),
         ("DATA",  0x1311, 250, 140,  44, 26, "saniye, 2 hane, SIFIR ACIK, SOLA"),
         ("ICON",  0x1305,  12, 176, 296, 54, "ne yapilacak seridi, ICL 57, kare 0-2")]
    return im, k


# ==========================================================================
#  SAYFA 103 - SERVIS (OPSIYONEL)
# ==========================================================================
SV = [("HÜCRE MIN", "mV", 0x1360), ("HÜCRE MAX", "mV", 0x1361),
      ("DELTA",     "mV", 0x1362), ("BMSERR",    "",   0x1365)]


def sayfa_103():
    """Sadece servisin gercekten ihtiyac duydugu 4 deger + son 3 hata.
    Paket V / akim / sicaklik zaten sayfa 101'de var, burada tekrar edilmiyor."""
    im, d = yeni()
    baslik(d)
    txt(d, (W - 14, 12), "SERVİS", B(11), ORANGE, "rm")
    k = []
    for i, (et, br, vp) in enumerate(SV):
        sut, sat = i % 2, i // 2
        x0 = 12 + sut * 152
        x1 = x0 + 144
        y = 36 + sat * 32
        kart(d, (x0, y, x1, y + 28), r=6)
        txt(d, (x0 + 10, y + 14), et, R(9), MUTED, "lm")
        if br:
            txt(d, (x1 - 8, y + 14), br, R(9), ORANGE, "rm")
        gen = 56
        bx = x1 - 8 - (18 if br else 0) - gen
        k.append(("DATA", vp, bx, y + 5, gen, 18, "%s, SAGA hizali" % et))

    txt(d, (26, 112), "SON HATALAR", R(9), MUTED, "la")
    for r in range(3):
        y = 124 + r * 34
        kart(d, (12, y, 308, y + 30), r=6)
        txt(d, (28, y + 15), "%d." % (r + 1), R(10), MUTED, "lm")
        txt(d, (120, y + 15), "KOD", R(9), MUTED, "lm")
        txt(d, (222, y + 15), "SAAT", R(9), MUTED, "lm")
        txt(d, (272, y + 15), ":", B(12), MUTED, "mm")
        vp = 0x1340 + r * 4
        k += [("DATA", vp + 0, 156, y + 5, 50, 20, "hata %d KOD, 3 hane, SAGA" % (r + 1)),
              ("DATA", vp + 2, 240, y + 5, 30, 20, "hata %d SAAT, 2 hane sifirli, SAGA" % (r + 1)),
              ("DATA", vp + 3, 276, y + 5, 30, 20, "hata %d DAKIKA, 2 hane sifirli, SOLA" % (r + 1))]
    return im, k



# ==========================================================================
#  ICL 59 - KONTAKTOR DURUM ROZETI   72x24   (0 = KAPALI, 1 = ACIK)
# ==========================================================================
RW, RH = 72, 24


def rozet(acik):
    im = Image.new("RGB", (RW * SS, RH * SS), CARD)
    d = ImageDraw.Draw(im)
    col = (72, 226, 150) if acik else (120, 126, 138)
    zem = (18, 44, 32) if acik else (34, 37, 45)
    rrect(d, (0, 0, RW - 1, RH - 1), 11, fill=zem, outline=col, w=1)
    d.ellipse([9 * SS, 9 * SS, 15 * SS, 15 * SS], fill=col)
    txt(d, (RW / 2 + 6, RH / 2), "AÇIK" if acik else "KAPALI", B(10), col, "mm")
    return im


# ==========================================================================
#  SAYFA 104 - KONTAKTOR KONTROL   (SADECE CAN + DOKUNMATIK PANEL)
# ==========================================================================
KK = [("ŞARJ",   0x1330, 0x00B1), ("DEŞARJ", 0x1331, 0x00B2),
      ("BALANS", 0x1332, 0x00B3)]


def sayfa_104():
    im, d = yeni()
    baslik(d)
    txt(d, (W - 14, 12), "KONTAKTÖR KONTROL", B(10), ORANGE, "rm")
    k = []
    for i, (et, vp, tus) in enumerate(KK):
        y = 34 + i * 42
        kart(d, (12, y, 308, y + 38))
        txt(d, (28, y + 19), et, B(13), INK, "lm")
        # dokunmatik butonun altina cizilen gorsel
        rrect(d, (206, y + 6, 296, y + 32), 6, fill=(46, 50, 60), outline=CARD_E, w=1)
        txt(d, (251, y + 19), "DEĞİŞTİR", B(11), INK, "mm")
        k += [("ICON",  vp, 116, y + 7, RW, RH, "%s durumu, ICL 59, 0=KAPALI 1=ACIK" % et),
              ("TOUCH", tus, 206, y + 6, 90, 26, "%s ac/kapa butonu" % et)]

    # ACIL KES - tam genislik
    y = 162
    rrect(d, (12, y, 308, y + 36), 8, fill=(96, 26, 22), outline=(150, 44, 38), w=1)
    txt(d, (W / 2, y + 18), "ACİL KES  —  ŞARJ + DEŞARJ KAPAT", B(13), (255, 210, 205), "mm")
    k.append(("TOUCH", 0x00B4, 12, y, 296, 36, "acil kes butonu"))

    kart(d, (12, 206, 308, 234), r=6)
    txt(d, (26, 220), "Araç hareket halindeyken değiştirilemez", R(9), MUTED, "lm")
    k += [("ICON",  0x1333, 240, 210, 60, 20, "uyari ikonu, ICL 60, 0=hazir 1=aracı durdur"),
          ("TOUCH", 0x00A3, 250, 206,  58, 28, "cikis butonu")]
    return im, k




# ==========================================================================
#  ICL 60 - SARJ BATARYA ANIMASYONU   176x74
#  21 doluluk kademesi (%5) x 4 parlama fazi = 84 kare.
#  MCU: index = (SOC / 5) * 4 + faz     ->  faz 200 ms'de bir ilerler.
#  Dolum uzerinde soldan saga akan bir parlaklik bandi = "sarj oluyor".
# ==========================================================================
BW, BH = 176, 74
BAT_KADEME, BAT_FAZ = 21, 4          # %5 adim (0..20) x 4 parlama fazi = 84 kare


def sarj_batarya(kademe, faz):
    """kademe 0..20 (=%0..%100), faz 0..3. Dolum uzerinde soldan saga akan
    parlaklik bandi = 'sarj oluyor'. Bant SADECE dolumun icinde gorunur."""
    from PIL import ImageChops
    im = Image.new("RGB", (BW * SS, BH * SS), BG)
    d = ImageDraw.Draw(im)

    rrect(d, (0, 4, 152, BH - 5), 9, fill=(20, 23, 29), outline=CARD_E, w=2)
    rrect(d, (154, 26, 164, 48), 3, fill=CARD_E)          # + ucu

    ic = (7, 11, 145, BH - 12)
    x0, y0, x1, y1 = ic
    rrect(d, ic, 5, fill=(12, 14, 18), outline=(38, 42, 50), w=1)

    oran = kademe / float(BAT_KADEME - 1)
    dolu = (x1 - x0) * oran

    if dolu >= 3:
        rrect(d, (x0, y0, x0 + dolu, y1), 4, fill=(38, 150, 96))
        rrect(d, (x0, y0, x0 + dolu, y0 + (y1 - y0) * 0.45), 4, fill=(56, 186, 120))

        # akan bant: ayri katmana ciz, dolum maskesiyle kirp, topla (screen etkisi)
        bant = 26.0
        bx   = x0 - bant + (dolu + bant) * (faz / float(BAT_FAZ))
        kat  = Image.new("RGB", im.size, (0, 0, 0))
        kd   = ImageDraw.Draw(kat)
        for i in range(6):                                 # yumusak kenar
            k = 1.0 - abs(i - 2.5) / 3.0
            w = bant * (0.20 + 0.80 * (1 - k))
            kd.polygon([((bx - w / 2 + 8) * SS, y0 * SS), ((bx + w / 2 + 8) * SS, y0 * SS),
                        ((bx + w / 2 - 8) * SS, y1 * SS), ((bx - w / 2 - 8) * SS, y1 * SS)],
                       fill=tuple(int(c * k * 0.40) for c in (130, 255, 200)))
        maske = Image.new("L", im.size, 0)
        ImageDraw.Draw(maske).rounded_rectangle(
            [x0 * SS, y0 * SS, (x0 + dolu) * SS, y1 * SS], radius=4 * SS, fill=255)
        kat.paste((0, 0, 0), (0, 0), ImageChops.invert(maske))
        im = ImageChops.add(im, kat)
        d = ImageDraw.Draw(im)

    # simsek: dolum icindeyse beyaz, disindaysa yesil
    cx = x0 + 16 if oran >= 0.12 else x0 + dolu + 18
    cy = (y0 + y1) / 2
    sim = [(cx + 3, cy - 15), (cx - 7, cy + 2), (cx - 1, cy + 2),
           (cx - 4, cy + 15), (cx + 7, cy - 3), (cx + 1, cy - 3)]
    d.polygon([(px * SS, py * SS) for px, py in sim],
              fill=(255, 255, 255) if oran >= 0.12 else (72, 226, 150))
    return im


# ==========================================================================
#  SAYFA 105 - SARJ EKRANI
#  101 ile AYNI geometri; sadece VOLTAJ karti TAHMINI SURE'ye donuyor.
#  Boylece 100 <-> 101 <-> 105 gecislerinde SOC/bar/sicaklik hic kipirdamiyor.
# ==========================================================================
def sayfa_105():
    """Animasyonlu sarj ekrani. Arka plan / kart dili sayfa 100 ile ayni."""
    im, d = yeni(BG)
    baslik(d)
    txt(d, (W - 14, 12), "ŞARJ", B(11), GREEN, "rm")

    txt(d, (300, 88), "%", B(22), GREEN, "rm")        # buyuk SOC'un birimi

    kart(d, (12, 110, 156, 162))
    txt(d, (26, 121), "ŞARJ AKIMI", R(10), MUTED, "la")
    txt(d, (144, 149), "A", R(12), MUTED, "rm")

    kart(d, (164, 110, 308, 162))
    txt(d, (178, 121), "TAHMİNİ SÜRE", R(10), MUTED, "la")
    txt(d, (243, 146), ":", B(17), MUTED, "mm")
    txt(d, (296, 157), "sa : dk", R(8), MUTED, "rm")

    kart(d, (12, 166, 308, 188), r=5)
    txt(d, (26, 177), "MOS", R(9), MUTED, "lm")
    txt(d, (140, 177), "°C", R(9), ORANGE, "rm")
    d.line([(150 * SS, 170 * SS), (150 * SS, 184 * SS)], fill=CARD_E, width=SS)
    txt(d, (162, 177), "BATARYA", R(9), MUTED, "lm")
    txt(d, (296, 177), "°C", R(9), ORANGE, "rm")

    k = [("ICON",  0x1322,  12,  30, BW, BH, "batarya animasyonu, ICL 60, kare 0-83"),
         ("DATA",  0x1000, 196,  52, 82, 52, "SOC (mevcut VP), 3 hane, SAGA"),
         ("FLOAT", 0x1080,  26, 132, 100, 26, "sarj akimi (mevcut VP), 1 ondalik, SAGA"),
         ("DATA",  0x1320, 196, 133,  42, 24, "tahmini SAAT, 2 hane, SAGA"),
         ("DATA",  0x1321, 248, 133,  42, 24, "tahmini DAKIKA, 2 hane sifirli, SOLA"),
         ("DATA",  0x1020,  76, 168,  50, 18, "MOS sicakligi (mevcut VP), SAGA"),
         ("DATA",  0x1040, 226, 168,  50, 18, "batarya sicakligi (mevcut VP), SAGA"),
         ("ICON",  0x1302,  12, 196, MW, MH, "mesaj seridi, ICL 56, kare 0-23")]
    return im, k


SAYFALAR = {101: ("ANA+HATA", sayfa_101), 102: ("ACIL", sayfa_102),
            103: ("SERVIS (ops.)", sayfa_103), 104: ("KONTAKTOR", sayfa_104),
            105: ("SARJ", sayfa_105)}

TIP_RENK = {"DATA": (72, 226, 150), "FLOAT": (54, 200, 245),
            "ICON": (255, 186, 64), "BAR": (255, 116, 106),
            "TOUCH": (200, 120, 255)}


def kilavuz_ciz(no, im_bg, kontroller):
    Z = 2
    im = im_bg.resize((W * Z, H * Z), Image.LANCZOS).convert("RGB")
    d = ImageDraw.Draw(im)
    ff = ImageFont.truetype(F + "segoeuib.ttf", 9 * Z)
    for tip, vp, x, y, w, h, _ in kontroller:
        c = TIP_RENK[tip]
        d.rectangle([x * Z, y * Z, (x + w) * Z - 1, (y + h) * Z - 1], outline=c, width=Z)
        et = "%04X" % vp
        tw = d.textlength(et, font=ff)
        ty = max(0, y * Z - 11 * Z)
        d.rectangle([x * Z, ty, x * Z + tw + 6 * Z, ty + 11 * Z], fill=c)
        d.text((x * Z + 3 * Z, ty + 1 * Z), et, font=ff, fill=(0, 0, 0))
    dd = os.path.join(BASE, "kilavuz")
    os.makedirs(dd, exist_ok=True)
    im.save(os.path.join(dd, "%d.png" % no))


ORNEK = {0x1301: "21", 0x1000: "58", 0x100C: 58, 0x1060: "24.9", 0x1080: "0.0",
         0x1020: "38", 0x1040: "31", 0x1310: "29", 0x1311: "47",
         0x1360: "3247", 0x1361: "3312", 0x1362: "65", 0x1365: "0000",
         0x1366: "0002", 0x1369: "16", 0x136A: "37", 0x1370: "26.42",
         0x1330: 1, 0x1331: 1, 0x1332: 0, 0x1320: "1", 0x1321: "24", "_kd": 12, "_fz": 2,
         0x1310: "59", 0x1311: "12",
         0x1340: "41", 0x1341: "04", 0x1342: "14", 0x1343: "32",
         0x1344: "29", 0x1345: "04", 0x1346: "11", 0x1347: "05",
         0x1348: "21", 0x1349: "03", 0x134A: "17", 0x134B: "48"}
ONIZ_MSG = {101: 5, 102: 21, 105: 23}
ONIZ_ALT  = {102: 1}
ONIZ_BANT = {102: 3}        # 3 = SARJ BITMEK UZERE (parlak faz)


def doldur(im4, kontroller, deger, msg=None, blink=1, alt=0):
    im = im4.copy()
    d = ImageDraw.Draw(im)
    for tip, vp, x, y, w, h, acik in kontroller:
        if tip == "ICON":
            if vp == 0x1302 and msg is not None:
                im.paste(mesaj_serit(*MESAJLAR[msg]), (x * SS, y * SS))
            elif vp == 0x1304:
                im.paste(kritik_bant(blink), (x * SS, y * SS))
            elif vp == 0x1305:
                im.paste(alt_serit(alt), (x * SS, y * SS))
            elif vp in (0x1330, 0x1331, 0x1332):
                im.paste(rozet(deger.get(vp, 0)), (x * SS, y * SS))
            elif vp == 0x1322:
                im.paste(sarj_batarya(deger.get("_kd", 12), deger.get("_fz", 2)),
                         (x * SS, y * SS))
            continue
        if tip == "TOUCH":
            continue
        if vp not in deger:
            continue
        if tip == "BAR":
            fw = (w - 4) * deger[vp] / 100.0
            if fw > 4:
                rrect(d, (x + 2, y + 2, x + 2 + fw, y + h - 2), 3, fill=(72, 226, 150))
            continue
        s = deger[vp]
        f = kucult(d, s, B(min(h - 4, 34)), w, B, 8)
        renk = PALE_K if vp in (0x1301, 0x1310, 0x1311) and blink is not None and \
            0x1300 <= vp <= 0x1311 and im4 is not None and deger.get("_kirmizi") else INK
        txt(d, ((x + w) if "SAGA" in acik else x, y + h / 2), s, f, renk,
            "rm" if "SAGA" in acik else "lm")
    return im.resize((W, H), Image.LANCZOS)


def main():
    dd = os.path.join(BASE, "sayfalar")
    od = os.path.join(dd, "onizleme")
    os.makedirs(od, exist_ok=True)
    satirlar = []
    for no, (ad, fn) in sorted(SAYFALAR.items()):
        im, k = fn()
        im.resize((W, H), Image.LANCZOS).save(os.path.join(dd, "%d.bmp" % no))
        kilavuz_ciz(no, im, k)
        deger = dict(ORNEK)
        if no == 105:
            deger[0x1080] = "42.3"
        if no == 102:
            deger["_kirmizi"] = 1
            deger[0x1301] = "34"      # HK_SARJ_ACIL
        doldur(im, k, deger, ONIZ_MSG.get(no), ONIZ_BANT.get(no, 1), ONIZ_ALT.get(no, 0)) \
            .save(os.path.join(od, "%d.png" % no))
        satirlar.append((no, ad, k))
        print("sayfa %d (%-13s): %2d kontrol" % (no, ad, len(k)))

    for i, m in enumerate(MESAJLAR):
        kaydet(mesaj_serit(*m), "mesajlar", "%02d" % i, MW, MH)
    for a in range(len(ALT)):
        kaydet(alt_serit(a), "altserit", str(a), AW, AH)
    for f in range(2 * len(BANT)):
        kaydet(kritik_bant(f), "kritik", str(f), KW, KH)
    for a in (0, 1):
        kaydet(rozet(a), "rozet", str(a), RW, RH)
    for kd in range(BAT_KADEME):
        for fz in range(BAT_FAZ):
            kaydet(sarj_batarya(kd, fz), "batarya",
                   "%02d" % (kd * BAT_FAZ + fz), BW, BH)

    print("\nmesajlar : %2d kare  %dx%d  -> ICL 56, Icon Var 0x1302" % (len(MESAJLAR), MW, MH))
    print("altserit :  %d kare  %dx%d  -> ICL 57, Icon Var 0x1305" % (len(ALT), AW, AH))
    print("kritik   :  %d kare  %dx%d  -> ICL 58, Icon Var 0x1304" % (2 * len(BANT), KW, KH))
    print("rozet    :  2 kare  %dx%d   -> ICL 59, Icon Var 0x1330-32 (sayfa 104)" % (RW, RH))
    print("batarya  : %d kare  %dx%d   -> ICL 60, Icon Var 0x1322 (sayfa 105)"
          % (BAT_KADEME * BAT_FAZ, BW, BH))
    print("55.icl   : DOKUNULMUYOR")

    with open(os.path.join(BASE, "kontrol-listesi.txt"), "w", encoding="utf-8") as fh:
        top = 0
        for no, ad, k in satirlar:
            fh.write("\n== SAYFA %d (%s) ==\n" % (no, ad))
            top += len(k)
            for tip, vp, x, y, w, h, acik in k:
                fh.write("  %-5s VP 0x%04X  x=%-3d y=%-3d %3dx%-3d  %s\n"
                         % (tip, vp, x, y, w, h, acik))
        fh.write("\nTOPLAM %d kontrol\n" % top)
    print("\nkontrol-listesi.txt + kilavuz/*.png yazildi")


if __name__ == "__main__":
    main()
