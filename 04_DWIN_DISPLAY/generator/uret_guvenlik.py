# -*- coding: utf-8 -*-
"""
ZASS MANTIS - GUVENLIK UYARILARI, TEK SAYFA (ana sayfa 100 uzerinde)

SAYFA DEGISMEZ. Mevcut kontrollerin VP/SP/renkleri aynen; sadece 2 sicaklik sayisinin Y'si
25 px yukari. Ana sayfaya 2 ikon eklenir, ikisi de durum cipi gibi "Transparence":

  100.bmp        yeni ana sayfa arka plani: UST KISIM PIKSEL PIKSEL AYNI. Sicaklik kartlari
                 25 px yukari (155..201), etiketler saga, serit EN ALTTA (206..228).
                 DGUS'ta sicaklik sayilari 0x1020/0x1040 SADECE Y: 185 -> 160.
  50/00..06.bmp  ICL 50  serit     296x22   Icon Var 0x1402  x=12 y=206
  51/00..10.bmp  ICL 51  pop-up    320x240  Icon Var 0x1404  x=0  y=0   (kare 0 = siyah = yok)
  ICL NO: 16 MB flash = 64 blok x 256 KB -> dosya no 0..63, ICL 16..63. 64 ve ustu panelde YOK
  SAYILIR. 51.icl ~513 KB = 3 blok (51..53). Dolu: 32.icl 32..40, 55.icl 55; eski V2 56..60.
  onizleme/*.png, ozet.png, once-sonra.png, kontrol-listesi.txt

Kaynak: kaynak/100_orijinal.bmp (ilk ana sayfa). Esikler ve VP'ler firmware'deki guvenlik.h'tan
(02_Firmware_STM32/ZASS_MANTIS_115/Core/Inc/guvenlik.h).
Calistir:  python uret_guvenlik.py
"""
import math
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KAYNAK = os.path.join(HERE, "kaynak")                            # orijinal sayfa + durum cipi ornekleri
sys.path.insert(0, HERE)
import uret as U                                                 # palet + yardimcilar (ayni klasorde)
from PIL import Image, ImageDraw, ImageFont

W, H, SS = U.W, U.H, U.SS
B, R = U.B, U.R
KAYNAK_100 = os.path.join(KAYNAK, "100_orijinal.bmp")
ICL_SERIT, ICL_POPUP = 50, 51                                    # 0..63 olmali (bkz. ustteki not)

# ---------------------------------------------------------------- guvenlik.h
HDR = open(os.path.join(HERE, "..", "..", "02_Firmware_STM32", "ZASS_MANTIS_115", "Core", "Inc", "guvenlik.h"),
           encoding="utf-8").read()


def hv(ad):
    m = re.search(r"#define\s+%s\s+\(?\s*(-?0[xX][0-9A-Fa-f]+|-?\d+)[uU]?\s*\)?" % ad, HDR)
    if not m:
        raise SystemExit("guvenlik.h icinde %s yok" % ad)
    return int(m.group(1), 0)


VP_SERIT, VP_POPUP = hv("GVN_VP_SERIT"), hv("GVN_VP_POPUP")
S_BOS, S_NORMAL, S_K4, S_K8, S_K3 = (hv("GVN_SERIT_BOS"), hv("GVN_SERIT_NORMAL"), hv("GVN_SERIT_K4"),
                                     hv("GVN_SERIT_K8"), hv("GVN_SERIT_K3"))
R1_V, R2_V = hv("GVN_R1_HUCRE_MV"), hv("GVN_R2_HUCRE_MV")
T4, T5, T6 = hv("GVN_R4_SICAK_C"), hv("GVN_R5_SICAK_C"), hv("GVN_R6_SOGUK_C")
R7_S = hv("GVN_R7_SURE_MS") // 1000


def volt(mv):
    return ("%.1f V" % (mv / 1000.0)).replace(".", ",")


# ---------------------------------------------------------------- geometri
# Kullanicinin DGUS ekran goruntusunden olculen mevcut kontroller (DOKUNULMUYOR)
MEVCUT = {"durum cipi 0x1100": (130, 0, 190, 25), "SOC 0x1000": (13, 61, 90, 54),
          "voltaj 0x1060": (217, 60, 63, 28), "akim 0x1080": (209, 119, 64, 28),
          "SOC bar 0x100C": (31, 125, 144, 13),
          "sicaklik 1 0x1020": (25, 185, 48, 36), "sicaklik 2 0x1040": (176, 185, 48, 36)}
# Serit EN ALTTA: sicaklik kartlari yukari kayar. Sicaklik sayilari (0x1020/0x1040) SADECE Y'de
# DY kadar tasinir (X/W/H/VP/SP aynen -> renk gecisleri bozulmaz).
DY = -25
TASINAN = {"sicaklik 1 0x1020": (25, 185 + DY, 48, 36), "sicaklik 2 0x1040": (176, 185 + DY, 48, 36)}
SERIT = (12, 206, 308, 228)                     # kart (x0, y0, x1, y1); eski kartlarin alt kenari 228
SX, SY, SW, SH = 12, 206, 296, 22               # serit ikonu
KART1, KART2 = (12, 155, 160, 201), (164, 155, 308, 201)
KES_Y = 153                                     # bu satirdan asagisi yeniden cizilir
DERECE = [(78, 186, 93, 200), (227, 186, 242, 200)]   # orijinal "°C" yazilari (aynen tasinir)

BG, CARD, CARD_E, CARD_HI, MUTED, INK = U.BG, U.CARD, U.CARD_E, U.CARD_HI, U.MUTED, U.INK
AMBER = U.AMBER
SIYAH = (0, 0, 0)


# ---------------------------------------------------------------- yardimcilar
def poly(d, pts, fill):
    d.polygon([(x * SS, y * SS) for x, y in pts], fill=fill)


def rr(d, box, r, **kw):
    d.rounded_rectangle([c * SS for c in box], radius=r * SS, **kw)


def cizgi(d, a, b, w, fill):
    d.line([(a[0] * SS, a[1] * SS), (b[0] * SS, b[1] * SS)], fill=fill, width=max(1, int(w * SS)))


def sar(d, metin, font, maks):
    satirlar, cur = [], ""
    for k in metin.split():
        t = (cur + " " + k).strip()
        if d.textlength(t, font=font) / SS <= maks or not cur:
            cur = t
        else:
            satirlar.append(cur)
            cur = k
    if cur:
        satirlar.append(cur)
    return satirlar


def kucuk(im):
    return im.resize((im.width // SS, im.height // SS), Image.LANCZOS)


# NET YAZI: sekiller SS kat buyuk cizilip kucultulur (yumusak kenar), ama yazi ayni yolla
# kucultulunce 9-11 px'te bulaniklasiyordu (2,8 inc panelde "dusuk cozunurluk"). ICL 50/51'de
# yazilar ERTELENIR ve kucultmeden SONRA ekranin kendi cozunurlugunde, FreeType hinting ile cizilir.
_ERTELE = None


def txt(d, xy, s, f, fill, anchor="la"):
    if _ERTELE is None:
        return U.txt(d, xy, s, f, fill, anchor)
    _ERTELE.append((xy, s, f.path, f.size / SS, fill, anchor))


def ertele_bas():
    global _ERTELE
    _ERTELE = []


def ertele_ciz(im):
    global _ERTELE
    d = ImageDraw.Draw(im)
    for xy, s_, yol, boy, fill, anchor in _ERTELE:
        d.text(xy, s_, font=ImageFont.truetype(yol, max(1, int(round(boy)))), fill=fill, anchor=anchor)
    _ERTELE = None
    return im


def ucgen(d, cx, cy, s, col, ici):
    poly(d, [(cx, cy - s * 0.55), (cx + s * 0.62, cy + s * 0.48), (cx - s * 0.62, cy + s * 0.48)], col)
    rr(d, (cx - s * 0.07, cy - s * 0.2, cx + s * 0.07, cy + s * 0.15), s * 0.05, fill=ici)
    d.ellipse([(cx - s * 0.08) * SS, (cy + s * 0.22) * SS, (cx + s * 0.08) * SS, (cy + s * 0.37) * SS], fill=ici)


def ic_pil(d, cx, cy, s, col, dolu, sim=None):
    w, h = s * 1.25, s * 0.74
    x0, y0 = cx - w / 2, cy - h / 2
    rr(d, (x0, y0, x0 + w, y0 + h), s * 0.12, outline=col, width=max(1, int(s * 0.075 * SS)))
    rr(d, (x0 + w, cy - h * 0.2, x0 + w + s * 0.1, cy + h * 0.2), s * 0.03, fill=col)
    ic = w - s * 0.2
    for i in range(4):
        if (i + 1) / 4.0 <= dolu + 1e-6:
            bx = x0 + s * 0.1 + i * ic / 4
            rr(d, (bx, y0 + s * 0.1, bx + ic / 4 - s * 0.05, y0 + h - s * 0.1), s * 0.03, fill=col)
    if sim:
        poly(d, [(cx + s * 0.1, cy - s * 0.5), (cx - s * 0.22, cy + s * 0.04), (cx - s * 0.02, cy + s * 0.04),
                 (cx - s * 0.1, cy + s * 0.5), (cx + s * 0.22, cy - s * 0.06), (cx + s * 0.02, cy - s * 0.06)], sim)


def ic_termo(d, cx, cy, s, col):
    tw = s * 0.26
    top, bot = cy - s * 0.58, cy + s * 0.34
    rr(d, (cx - tw / 2, top, cx + tw / 2, bot), tw / 2, outline=col, width=max(1, int(s * 0.06 * SS)))
    d.ellipse([(cx - s * 0.30) * SS, (cy + s * 0.14) * SS, (cx + s * 0.30) * SS, (cy + s * 0.74) * SS], fill=col)
    rr(d, (cx - tw * 0.22, top + s * 0.12, cx + tw * 0.22, bot), tw * 0.2, fill=col)
    for i in range(4):
        y = top + s * 0.18 + i * s * 0.15
        cizgi(d, (cx + tw / 2 + s * 0.06, y), (cx + tw / 2 + s * 0.2, y), s * 0.045, col)


def ic_kar(d, cx, cy, s, col):
    for a in (0, 60, 120):
        r = s * 0.62
        dx, dy = math.cos(math.radians(a)) * r, math.sin(math.radians(a)) * r
        cizgi(d, (cx - dx, cy - dy), (cx + dx, cy + dy), s * 0.075, col)
        for sg in (-1, 1):
            ex, ey = cx + sg * dx, cy + sg * dy
            for da in (-35, 35):
                ba = math.radians(a + (0 if sg > 0 else 180) + da + 180)
                cizgi(d, (ex, ey), (ex + math.cos(ba) * s * 0.2, ey + math.sin(ba) * s * 0.2), s * 0.06, col)


def ic_sinyal_yok(d, cx, cy, s, col, kesik):
    for i in range(4):
        bw, bh = s * 0.17, s * (0.2 + i * 0.2)
        x = cx - s * 0.5 + i * s * 0.26
        rr(d, (x, cy + s * 0.5 - bh, x + bw, cy + s * 0.5), s * 0.03, fill=col)
    cizgi(d, (cx - s * 0.62, cy - s * 0.56), (cx + s * 0.62, cy + s * 0.56), s * 0.13, kesik)
    cizgi(d, (cx - s * 0.62, cy - s * 0.56), (cx + s * 0.62, cy + s * 0.56), s * 0.06, col)


# ---------------------------------------------------------------- KRITIK POP-UP (ICL 51)
# Seffaflik filtresinde kaybolmasin diye pop-up'ta KOYU RENK YOK: zemin dolgun renk, yazi beyaz.
TEMA = {"kirmizi": (206, 40, 34), "mavi": (34, 102, 200), "turuncu": (206, 104, 14)}
KRITIK = {
    1: dict(tema="kirmizi", basl=("!!! ŞARJI", "DURDURUN"),
            ana=["Tekrar ederse servisle", "iletişime geçin"],
            neden="Hücre gerilimi %s üstünde ve şarj kontaktörü kesikken şarj akımı sürüyor" % volt(R1_V),
            ikon="pil_sarj"),
    2: dict(tema="kirmizi", basl=("!!! KULLANIMI", "DURDURUN"),
            ana=["Bataryayı şarj edin.", "Tekrar ederse servisle iletişime geçin"],
            neden="Hücre gerilimi %s altında ya da deşarj kontaktörü kesikken akım sürüyor" % volt(R2_V),
            ikon="pil_bos"),
    5: dict(tema="kirmizi", basl=("BATARYA SICAKLIĞI", "ÇOK YÜKSEK"),
            ana=["Kullanımı veya şarjı", "durdurun"],
            neden="Batarya sıcaklığı %d °C üzerinde" % T5, ikon="termo"),
    6: dict(tema="mavi", basl=("BATARYAYI", "ŞARJ ETMEYİN"),
            ana=["Uygun sıcaklığa gelmesini", "bekleyin"],
            neden="Batarya sıcaklığı %s °C altında" % str(T6).replace("-", "−"), ikon="kar"),
    7: dict(tema="turuncu", basl=("HABERLEŞME", "HATASI"),
            ana=["Aracı kullanmayın.", "Servisle iletişime geçin"],
            neden="BMS'ten %d saniyeden uzun süredir veri alınamıyor" % R7_S, ikon="sinyal"),
}
SIRA = [1, 2, 5, 6, 7]                      # ICL 51 kare = 1 + sira*2 + faz


def acik(c, k):
    return tuple(int(v + (255 - v) * k) for v in c)


POP = (6, 30, 314, 202)                      # pop-up paneli: header ustte, bilgi cubugu altta acik


def popup(kural, faz):
    """faz 0: renkli bant + beyaz baslik, faz 1: beyaz bant + renkli baslik (yanip soner).
    Panel 30..202 arasi; altindaki serit alani (206..228) CANLI bilgi cubugu icin acik kalir."""
    t = KRITIK[kural]
    ana_renk = TEMA[t["tema"]]
    ertele_bas()
    im = Image.new("RGB", (W * SS, H * SS), SIYAH)            # siyah = seffaf (ana sayfa gorunur)
    d = ImageDraw.Draw(im)
    P = POP
    rr(d, P, 12, fill=(255, 255, 255))                       # ince beyaz cerceve
    rr(d, (P[0] + 2, P[1] + 2, P[2] - 2, P[3] - 2), 10, fill=ana_renk)
    bant = (P[0] + 2, P[1] + 2, P[2] - 2, 86)
    if faz == 1:
        rr(d, bant, 10, fill=(255, 255, 255))
        d.rectangle([bant[0] * SS, 70 * SS, bant[2] * SS, bant[3] * SS], fill=(255, 255, 255))
    bas_renk = (255, 255, 255) if faz == 0 else ana_renk
    plaka = acik(ana_renk, 0.22) if faz == 0 else acik(ana_renk, 0.80)
    d.ellipse([15 * SS, 35 * SS, 63 * SS, 83 * SS], fill=plaka)
    ic = (255, 255, 255) if faz == 0 else ana_renk
    a = t["ikon"]
    cx, cy, sz = 39, 59, 26
    if a == "pil_sarj":
        ic_pil(d, cx, cy, sz, ic, 1.0, sim=(255, 222, 80) if faz == 0 else (255, 196, 40))
    elif a == "pil_bos":
        ic_pil(d, cx, cy, sz, ic, 0.25)
    elif a == "termo":
        ic_termo(d, cx, cy - 2, sz, ic)
    elif a == "kar":
        ic_kar(d, cx, cy, sz, ic)
    else:
        ic_sinyal_yok(d, cx, cy, sz, ic, plaka)
    b1, b2 = t["basl"]
    txt(d, (74, 46), b1, U.kucult(d, b1, B(22), 232, B, 12), bas_renk, "lm")
    txt(d, (74, 70), b2, U.kucult(d, b2, B(22), 232, B, 12), bas_renk, "lm")
    if faz == 0:
        cizgi(d, (P[0] + 12, 88), (P[2] - 12, 88), 1, acik(ana_renk, 0.45))

    # govde: kullanicinin cumleleri (beyaz)
    y = 95
    for i, satir in enumerate(t["ana"]):
        boy = 17 if i == 0 else 14 if len(satir) > 26 else 16
        f = U.kucult(d, satir, B(boy), 286, B, 10)
        txt(d, (18, y + boy / 2), satir, f, (255, 255, 255), "lm")
        y += boy + 5
    # neden (acik ton, alta yaslanir) + uyari kodu rozeti
    fn = R(10)                                               # 9 -> 10 px: panelde okunur
    soluk = acik(ana_renk, 0.72)
    nd = sar(d, t["neden"], fn, 228)
    yy = 189 - 12 * (len(nd) - 1)
    for i, s_ in enumerate(nd):
        txt(d, (18, yy + i * 12), s_, fn, soluk, "lm")
    rr(d, (P[2] - 40, 177, P[2] - 10, 197), 6, fill=(255, 255, 255))
    txt(d, (P[2] - 25, 187), str(kural), B(13), ana_renk, "mm")
    txt(d, (P[2] - 44, 187), "KOD", B(9), soluk, "rm")         # 8 px ince -> 9 px kalin
    return ertele_ciz(kucuk(im))


# ---------------------------------------------------------------- SERIT (ICL 50)
SERIT_TANIM = {
    S_NORMAL: dict(nokta=(72, 200, 130), bas=None, yazi="Batarya durumu normal", renk=MUTED, bar=None),
    S_K4: dict(ucgen=AMBER, bas="BATARYA SICAKLIĞI YÜKSEK", yazi="Soğumasını bekleyin", renk=AMBER, bar=AMBER),
    S_K8: dict(ucgen=AMBER, bas="SENSÖR HATASI!", yazi="Servisle iletişime geçin", renk=AMBER, bar=AMBER),
    S_K3: dict(ucgen=(176, 140, 80), bas=None, yazi="Uyarı! Direnç, devam ederse servise bildirin",
               renk=(196, 160, 96), bar=None),
}


# BILGI CUBUGU (pop-up acikken serit yerinde): 4 hucre x 74 px. Rakamlari DGUS'taki 4 Data
# Variable cizer (mevcut VP'ler, YENI SP'ler); bu kare sadece etiketleri ve ayiraclari cizer.
# (ad, VP, yeni SP, rakam kutusu x, genislik, birim, ornek, ayar)
BILGI = [("SOC", 0x1000, hv("GVN_SP_MINI_SOC"), 40, 24, "%", "58", "tamsayi 3 hane"),
         (None, 0x1060, hv("GVN_SP_MINI_V"), 94, 32, "V", "26.2", "FLOAT, 2 tam + 1 ondalik (mevcut voltajla ayni tip)"),
         (None, 0x1080, hv("GVN_SP_MINI_A"), 164, 48, "A", "-12.5", "FLOAT, isaretli, 3 tam + 1 ondalik (mevcut akimla ayni tip)"),
         (None, 0x1040, hv("GVN_SP_MINI_T"), 244, 24, "°C", "31", "isaretli tamsayi 3 hane (mevcut sicaklik 2 ile ayni tip)")]
RAKAM_Y, RAKAM_H = 209, 16                    # rakam kutularinin ekran Y'si ve yuksekligi (8x16 font)
S_BILGI, S_VERIYOK = hv("GVN_SERIT_BILGI"), hv("GVN_SERIT_VERIYOK")


def serit_bilgi(veri_yok=False):
    ertele_bas()
    im = Image.new("RGB", (SW * SS, SH * SS), CARD)
    d = ImageDraw.Draw(im)
    if veri_yok:
        ucgen(d, 16, SH / 2, 11, (214, 150, 60), CARD)
        txt(d, (28, SH / 2), "BMS verisi alınamıyor — değerler gösterilemiyor", R(10), (196, 160, 96), "lm")
    else:
        for i in range(1, 4):                                   # hucre ayiraclari
            x = i * 74
            cizgi(d, (x, 5), (x, SH - 5), 1, CARD_E)
        for ad, vp, sp, bx, bw, birim, ornek, ayar in BILGI:
            rx = bx - SX                                        # ikon koordinati
            if ad:
                txt(d, (6, SH / 2), ad, B(9), MUTED, "lm")
            txt(d, (rx + bw + 3, SH / 2 + 1), birim, R(10), U.ORANGE if birim == "°C" else MUTED, "lm")
    return karti_seffaf(ertele_ciz(kucuk(im)))


def karti_seffaf(k):
    px = k.load()
    for yy in range(k.height):
        for xx in range(k.width):
            c = px[xx, yy]
            if abs(c[0] - CARD[0]) + abs(c[1] - CARD[1]) + abs(c[2] - CARD[2]) <= 6:
                px[xx, yy] = SIYAH
    return k


def serit_kare(no):
    """kart rengi uzerine cizilir, sonra kart rengindeki pikseller SIYAH yapilir (seffaf).
    Boylece yazi kenarlari sayfadaki kartla birebir uyumlu kalir."""
    if no == S_BILGI or no == S_VERIYOK:
        return serit_bilgi(no == S_VERIYOK)
    ertele_bas()
    im = Image.new("RGB", (SW * SS, SH * SS), CARD)
    if no in SERIT_TANIM:
        t = SERIT_TANIM[no]
        d = ImageDraw.Draw(im)
        x = 10
        if t.get("bar"):
            rr(d, (2, 4, 5, SH - 4), 1.5, fill=t["bar"])
            x = 12
        if t.get("ucgen"):
            ucgen(d, x + 6, SH / 2, 11, t["ucgen"], CARD)
            x += 17
        if t.get("nokta"):
            d.ellipse([(x + 1) * SS, (SH / 2 - 3) * SS, (x + 7) * SS, (SH / 2 + 3) * SS], fill=t["nokta"])
            x += 13
        maks = SW - x - 8
        if t["bas"]:
            fb, fr = B(11), R(10)
            while (d.textlength(t["bas"], font=fb) + d.textlength("  " + t["yazi"], font=fr)) / SS > maks \
                    and fb.size > 8 * SS:
                fb, fr = B((fb.size - SS / 2) / SS), R((fr.size - SS / 2) / SS)
            txt(d, (x, SH / 2), t["bas"], fb, t["renk"], "lm")
            bw = d.textlength(t["bas"], font=fb) / SS
            txt(d, (x + bw + 6, SH / 2), t["yazi"], fr, (186, 190, 200), "lm")
        else:
            f = U.kucult(d, t["yazi"], R(10), maks, R, 8)
            txt(d, (x, SH / 2), t["yazi"], f, t["renk"], "lm")
    return karti_seffaf(ertele_ciz(kucuk(im)))


# ---------------------------------------------------------------- YENI SAYFA 100
def yeni_100():
    orj = Image.open(KAYNAK_100).convert("RGB")
    alt = Image.new("RGB", (W * SS, H * SS), BG)
    d = ImageDraw.Draw(alt)
    U.kart(d, SERIT, r=7)                                     # bos serit yuvasi (ikon icine yazar)
    for kb in (KART1, KART2):
        U.kart(d, kb, r=10)
    for kb, et in ((KART1, "SICAKLIK 1"), (KART2, "SICAKLIK 2")):
        U.txt(d, (kb[2] - 11, 203 + DY), et, U.fnt("segoeui.ttf", 10), MUTED, "rm")
    alt = kucuk(alt)
    yeni = orj.copy()
    yeni.paste(alt.crop((0, KES_Y, W, H)), (0, KES_Y))
    for box in DERECE:                                         # orijinal °C pikselleri
        yeni.paste(orj.crop(box), (box[0], box[1] + DY))   # sayiyla birlikte kayar
    return orj, yeni


# ---------------------------------------------------------------- ONIZLEME
def seffaf_bindir(alt, ust, x=0, y=0, esik=40):
    """DGUS 'Transparence' benzetimi: neredeyse siyah pikseller gosterilmez."""
    out = alt.copy()
    m = ust.convert("L").point(lambda v: 255 if v > esik else 0)
    mx = Image.new("L", ust.size, 0)
    pu = ust.load(); pm = mx.load()
    for yy in range(ust.height):
        for xx in range(ust.width):
            c = pu[xx, yy]
            pm[xx, yy] = 255 if max(c) > esik else 0
    out.paste(ust, (x, y), mx)
    return out


def degerler(im, soc="58", v="26.2", a="0.0", t1="38", t2="31", durum=0, ty=203 + DY, bar=(72, 226, 150)):
    """Ornek degerleri mevcut kontrollerin olculen kutularina yazar (temsili)."""
    big = im.resize((W * SS, H * SS), Image.LANCZOS)
    d = ImageDraw.Draw(big)
    num = lambda s: U.fnt("segoeuib.ttf", s)
    U.txt(d, (100, 87), soc, num(50), INK, "rm")
    U.txt(d, (275, 74), v, num(25), INK, "rm")
    U.txt(d, (270, 133), a, num(25), INK, "rm")
    if bar:
        rr(d, (32, 126, 32 + 140 * int(soc) / 100, 136), 4, fill=bar)
    U.txt(d, (72, ty), t1, num(32), INK, "rm")
    U.txt(d, (223, ty), t2, num(32), INK, "rm")
    chip = Image.open(os.path.join(KAYNAK, "chips", "%d.bmp" % durum)).convert("RGB")
    big = big.resize((W, H), Image.LANCZOS)
    return seffaf_bindir(big, chip, 320 - chip.width - 2, 1)


def etiketle(im, baslik, alt=None):
    pad = 34 if alt else 22
    out = Image.new("RGB", (im.width, im.height + pad), (10, 11, 14))
    out.paste(im, (0, 0))
    d = ImageDraw.Draw(out)
    d.text((6, im.height + 4), baslik, font=ImageFont.truetype(U.F + "segoeuib.ttf", 11), fill=(230, 232, 236))
    if alt:
        d.text((6, im.height + 19), alt, font=ImageFont.truetype(U.F + "segoeui.ttf", 9), fill=(150, 156, 168))
    return out


def izgara(hucreler, kol, yol, baslik=None):
    ch = max(c.height for c in hucreler)
    satir = (len(hucreler) + kol - 1) // kol
    ust = 30 if baslik else 0
    sheet = Image.new("RGB", (kol * (W + 12) + 12, satir * (ch + 12) + 12 + ust), (10, 11, 14))
    d = ImageDraw.Draw(sheet)
    if baslik:
        d.text((12, 8), baslik, font=ImageFont.truetype(U.F + "segoeuib.ttf", 13), fill=(240, 242, 246))
    for i, c in enumerate(hucreler):
        sheet.paste(c, (12 + (i % kol) * (W + 12), 12 + ust + (i // kol) * (ch + 12)))
    kaydet(sheet, yol)


def kaydet(im, yol):
    """Windows'ta dosya bir onizlemede acik (eslenmis) ise dogrudan yazilamaz -> gecici dosya + replace."""
    try:
        im.save(yol)
        return
    except OSError:
        pass
    gecici = yol[:-4] + ".yeni.png"
    im.save(gecici)
    try:
        os.replace(gecici, yol)
    except OSError:
        print("UYARI: %s acik oldugu icin yazilamadi -> %s" % (os.path.basename(yol), os.path.basename(gecici)))



# ---------------------------------------------------------------- ANA
def temizle(klasor):
    yol = os.path.join(HERE, klasor)
    if os.path.isdir(yol):
        shutil.rmtree(yol)
    os.makedirs(yol)
    return yol


def bilgi_rakamlari(im, deger):
    """Onizleme: 4 yeni Data Variable'in pop-up acikken cizecegi beyaz rakamlar (temsili font)."""
    big = im.resize((W * SS, H * SS), Image.LANCZOS)
    d = ImageDraw.Draw(big)
    for (ad, vp, sp, bx, bw, birim, ornek, ayar), v in zip(BILGI, deger):
        U.txt(d, (bx + bw, RAKAM_Y + RAKAM_H / 2), v, U.fnt("segoeuib.ttf", 13), (240, 242, 246), "rm")
    return big.resize((W, H), Image.LANCZOS)


def kritik_onizleme(taban, kural, faz, deger=("58", "26.2", "-12.5", "31")):
    if kural == 7:                                           # veri yok: rakamlar gizli
        return seffaf_bindir(seffaf_bindir(taban, serit_kare(S_VERIYOK), SX, SY), popup(kural, faz))
    alt = bilgi_rakamlari(seffaf_bindir(taban, serit_kare(S_BILGI), SX, SY), deger)
    return seffaf_bindir(alt, popup(kural, faz))


def main():
    for eski in ("56", "57", "64", "65", "66", "sayfalar", "secenekler.png", "anim-onizleme.gif", "anim-yakin.png"):   # eski tasarim + gecersiz ICL no artiklari
        yol = os.path.join(HERE, eski)
        if os.path.isdir(yol):
            shutil.rmtree(yol)
        elif os.path.exists(yol):
            os.remove(yol)

    orj, yeni = yeni_100()
    ust_ayni = orj.crop((0, 0, W, KES_Y)).tobytes() == yeni.crop((0, 0, W, KES_Y)).tobytes()
    import net_100                                             # yazilar ekran cozunurlugunde (net)
    yeni, net_rapor = net_100.yap(yeni)
    yeni.save(os.path.join(HERE, "100.bmp"))

    d_serit = temizle(str(ICL_SERIT))
    for no in range(7):
        serit_kare(no).save(os.path.join(d_serit, "%02d.bmp" % no))
    d_popup = temizle(str(ICL_POPUP))
    Image.new("RGB", (W, H), SIYAH).save(os.path.join(d_popup, "00.bmp"))
    for si, kural in enumerate(SIRA):
        for faz in (0, 1):
            popup(kural, faz).save(os.path.join(d_popup, "%02d.bmp" % (1 + si * 2 + faz)))

    # ---- onizlemeler
    od = temizle("onizleme")
    taban = degerler(yeni)
    ONIZ = [("00-once", "ONCE: mevcut ana sayfa", "degismeden", degerler(orj, ty=203)),
            ("01-normal", "SONRA: normal", "serit en altta, sicakliklar 25 px yukarida", seffaf_bindir(taban, serit_kare(S_NORMAL), SX, SY)),
            ("02-k4", "Kural 4  (>%d C)" % T4, "serit, buzzer 5 sn'de bir", seffaf_bindir(degerler(yeni, t1="57", t2="52"), serit_kare(S_K4), SX, SY)),
            ("03-k8", "Kural 8  (sensor NA)", "serit, buzzer 5 sn'de bir", seffaf_bindir(taban, serit_kare(S_K8), SX, SY)),
            ("04-k3", "Kural 3  (hucre farki)", "serit, sonuk, 15 dk, buzzer yok", seffaf_bindir(taban, serit_kare(S_K3), SX, SY))]
    adlar = {1: "asiri sarj", 2: "asiri desarj", 5: ">%d C" % T5, 6: "<%d C" % T6, 7: "haberlesme"}
    ornek = {1: ("97", "29.7", "+24.5", "33"), 2: ("0", "20.6", "-38.2", "36"), 5: ("71", "26.4", "-95.0", "62"),
             6: ("44", "25.9", "+4.1", "-12"), 7: None}
    for kural in SIRA:
        for faz in (0, 1):
            alt = ("surekli buzzer" if kural != 7 else "buzzer 5 sn'de bir") + \
                  (", altta CANLI SOC / V / A / C" if kural != 7 else ", veri yok: rakam gosterilmez")
            ONIZ.append(("1%d-k%d-faz%d" % (kural, kural, faz),
                         "Kural %d pop-up (%s)%s" % (kural, adlar[kural], "" if faz == 0 else "  - 2. kare"), alt,
                         kritik_onizleme(taban, kural, faz, ornek[kural]) if kural != 7 else kritik_onizleme(taban, 7, faz)))
    for ad, bas, alt, im in ONIZ:
        kaydet(im, os.path.join(od, ad + ".png"))
    izgara([etiketle(im, b, a) for ad, b, a, im in ONIZ if "faz1" not in ad], 3, os.path.join(HERE, "ozet.png"),
           "Tek sayfa: serit en altta, kritikte pop-up ustte + CANLI bilgi cubugu altta")
    izgara([etiketle(ONIZ[0][3], "ONCE (sahadaki)", "100.bmp aynen"),
            etiketle(ONIZ[1][3], "SONRA", "ust 153 satir piksel piksel ayni")], 2, os.path.join(HERE, "once-sonra.png"))

    N = chr(10)
    with open(os.path.join(HERE, "kontrol-listesi.txt"), "w", encoding="utf-8") as fh:
        fh.write("SAYFA 100 - arka plani ekran/100.bmp ile degistir, 2 sicaklik sayisinin sadece Y'sini kaydir," + N)
        fh.write("4 kucuk rakam + 2 ikon ekle. EKLEME SIRASI ONEMLI (sonra eklenen ustte cizilir):" + N + N)
        fh.write("  1..4) BILGI CUBUGU RAKAMLARI - mevcut kontrolu kopyala, SADECE asagidakileri degistir:" + N)
        fh.write("        X, Y, W, H, font 8x16 (Font size 8), SP = YENI adres, Y=209 (firmware gizlerken Y'yi 300 yapar)." + N)
        fh.write("        !! SP'yi MUTLAKA yeni adrese al; kopya eski SP'yi tasirsa ana sayfanin rengi bozulur !!" + N)
        for ad, vp, sp, bx, bw, birim, ornekd, ayar in BILGI:
            fh.write("     Data Var  VP 0x%04X  SP 0x%04X  X=%-3d Y=%d W=%d H=%d  saga hizali, %s" % (vp, sp, bx, RAKAM_Y, bw, RAKAM_H, ayar) + N)
        fh.write(N + "  5) Variable Icon  VP 0x%04X  X=%d Y=%d W=%d H=%d  ICL %d  Minimum 0 / Maximum 6" % (VP_SERIT, SX, SY, SW, SH, ICL_SERIT) + N)
        fh.write("     Display Mode = Transparence, Filter Set = 63 (durum cipiyle AYNI), SP kapali" + N)
        fh.write("     RAKAMLARDAN SONRA eklenmeli (serit yazisi gizli rakamlarin ustune cizilsin)" + N + N)
        fh.write("  6) Variable Icon  VP 0x%04X  X=0 Y=0 W=320 H=240     ICL %d  Minimum 0 / Maximum 10" % (VP_POPUP, ICL_POPUP) + N)
        fh.write("     Display Mode = Transparence, Filter Set = 63, SP kapali" + N)
        fh.write("     SAYFADAKI EN SON KONTROL (en ustte; panel 30..202, alttaki serit/bilgi cubugu acik kalir)" + N + N)
        fh.write("TASINACAK 2 mevcut kontrol - SADECE Y degisir (X, W, H, VP, SP AYNEN; renk gecisi bozulmaz):" + N)
        for ad, (x, y, w, h) in TASINAN.items():
            fh.write("  %-20s Y: %d -> %d   (X=%d, %dx%d)" % (ad, MEVCUT[ad][1], y, x, w, h) + N)
        fh.write(N + "Dokunulmayan mevcut kontroller (ekran goruntusunden olculdu):" + N)
        for ad, (x, y, w, h) in MEVCUT.items():
            if ad not in TASINAN:
                fh.write("  %-20s X=%-3d Y=%-3d %dx%d" % (ad, x, y, w, h) + N)
        fh.write(N + "Yeni SP bloklari 0x5200..0x527F baska bir kontrolde KULLANILMAMALI (DGUS'ta kontrol edin)." + N)
        fh.write(N + "ICL NUMARASI 0..63 OLMALI (16 MB flash = 64 blok x 256 KB). 64 ve ustu panelde YOK SAYILIR:" + N)
        fh.write("  ikon hic cizilmez, hata da vermez. %d.icl ~513 KB = 3 blok (%d..%d); bu bloklara baska dosya koymayin." % (ICL_POPUP, ICL_POPUP, ICL_POPUP + 2) + N)

    jpg = temizle("icl")                                          # DGUS ICL kaynagi: klasor = ICL no, kareler 0.jpg, 1.jpg ...
    for k in (str(ICL_SERIT), str(ICL_POPUP)):
        os.makedirs(os.path.join(jpg, k))
        for i, f in enumerate(sorted(os.listdir(os.path.join(HERE, k)))):
            Image.open(os.path.join(HERE, k, f)).convert("RGB").save(os.path.join(jpg, k, "%d.jpg" % i), quality=100, subsampling=0)

    print("100.bmp      : sekiller ust %d satirda orijinalle ayni = %s, yazilar net (1x hinting)" % (KES_Y, ust_ayni))
    for satir in net_rapor:
        print("               " + satir)
    print("ICL %d       : 7 kare  %dx%d  -> 0x%04X (5 = bilgi cubugu, 6 = veri yok)" % (ICL_SERIT, SW, SH, VP_SERIT))
    print("ICL %d       : 11 kare 320x240 -> 0x%04X (kare 0 bos, panel 30..202)" % (ICL_POPUP, VP_POPUP))
    print("onizleme/, ozet.png, once-sonra.png, kontrol-listesi.txt")


if __name__ == "__main__":
    main()
