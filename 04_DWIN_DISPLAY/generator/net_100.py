# -*- coding: utf-8 -*-
"""
Ana sayfa (100.bmp) NET YAZI surumu.

Sorun: sayfa 4 kat buyuk cizilip kucultulerek uretilmisti (home_card.py + uret_guvenlik.py).
Sekil kenarlari icin iyi, ama 10-15 px yazilar bu yolla bulaniklasiyor; 2,8 inc panelde
"dusuk cozunurluk" gibi gorunuyor.

Cozum: sekiller (kartlar, cizgiler, SOC bar kutusu) AYNI yolla (4x -> kucult) cizilir; yazilar
kucultmeden SONRA ekranin kendi cozunurlugunde, FreeType hinting ile cizilir.
Geometri DEGISMEZ:
  - etiket konumlari home_card.py ile ayni (dogrulandi: orijinalle piksel farki yok),
  - birimler (%, V, A, °C) orijinal 100.bmp'de elle kaydirilmis -> yerleri ve boylari
    orijinalden OLCULUR, net glif ayni kutuya oturtulur (DGUS sayilari birimlere hizali kalir).

uret_guvenlik.py main() bu modulu kullanir. Tek basina:  python net_100.py  -> 100_net.png
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
F = "C:/Windows/Fonts/"
W, H, SS = 320, 240, 4
BG, CARD, CARD_HI, CARD_E = (14, 16, 20), (28, 31, 38), (40, 44, 53), (52, 56, 66)
INK, MUTED, ORANGE, TRACK = (237, 239, 243), (138, 144, 156), (240, 138, 34), (44, 48, 57)
REG, BOLD = F + "segoeui.ttf", F + "segoeuib.ttf"

# ust kisim: home_card.py CARDS (SOC, VOLTAJ, AKIM); alt kisim: uret_guvenlik.py
UST_KARTLAR = [(12, 34, 196, 150), (204, 34, 308, 91), (204, 95, 308, 150)]
ALT_KARTLAR = [((12, 155, 160, 201), 10), ((164, 155, 308, 201), 10), ((12, 206, 308, 228), 7)]

# (metin, font, boy, renk, birimin orijinalde aranacagi pencere)
# "%" orijinalde yatay sikistirilmis (16x19) ve koyu haleli: hicbir boy birebir tutmaz -> sabit
# kalin 24 (19x17), sag-alt koseden hizalanir (DGUS SOC sayisi ile iliskisi degismez).
SABIT = {"%": (BOLD, 24)}
BIRIMLER = [("%",  BOLD, 26, MUTED,  (90, 60, 150, 100)),
            ("V",  REG,  12, MUTED,  (262, 62, 300, 89)),
            ("A",  REG,  12, MUTED,  (262, 122, 300, 148)),
            ("°C", REG,  13, ORANGE, (60, 157, 100, 175)),
            ("°C", REG,  13, ORANGE, (210, 157, 247, 175))]


def _sekiller():
    im = Image.new("RGB", (W * SS, H * SS), BG)
    d = ImageDraw.Draw(im)

    def kart(box, r):
        d.rounded_rectangle([c * SS for c in box], radius=r * SS, fill=CARD, outline=CARD_E, width=SS)
        x0, y0, x1, _ = box
        d.line([((x0 + 8) * SS, (y0 + 1) * SS), ((x1 - 8) * SS, (y0 + 1) * SS)], fill=CARD_HI, width=SS)

    d.line([(12 * SS, 26 * SS), ((W - 12) * SS, 26 * SS)], fill=CARD_E, width=SS)       # header cizgisi
    for b in UST_KARTLAR:
        kart(b, 10)
    d.rounded_rectangle([26 * SS, 124 * SS, 176 * SS, 140 * SS], radius=5 * SS, fill=TRACK, outline=CARD_E, width=SS)
    d.rounded_rectangle([178 * SS, 128 * SS, 181 * SS, 136 * SS], radius=1 * SS, fill=CARD_E)  # bar uc basligi
    for b, r in ALT_KARTLAR:
        kart(b, r)
    return im.resize((W, H), Image.LANCZOS)


def _murekkep(im, kutu, zemin=CARD, esik=40):
    """kutu icinde zeminden belirgin farkli piksellerin sinir kutusu (ekran koordinati)"""
    px = im.load()
    xs, ys = [], []
    for y in range(kutu[1], kutu[3]):
        for x in range(kutu[0], kutu[2]):
            c = px[x, y]
            if max(abs(c[i] - zemin[i]) for i in range(3)) > esik:
                xs.append(x); ys.append(y)
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1) if xs else None


def _glif(metin, yol, boy, renk):
    """1x hinting'li glif: (resim, murekkep kutusu) - saydam zemin icin maske"""
    f = ImageFont.truetype(yol, boy)
    m = Image.new("L", (boy * 4, boy * 3), 0)
    ImageDraw.Draw(m).text((boy, boy // 2), metin, font=f, fill=255)
    bb = m.point(lambda v: 255 if v > 100 else 0).getbbox()
    return f, (boy, boy // 2), bb


def birim_yerlesimi(orj):
    """her birim icin: orijinaldeki murekkep kutusuna en iyi oturan boy + cizim konumu"""
    sonuc = []
    for metin, yol, boy0, renk, pencere in BIRIMLER:
        hedef = _murekkep(orj, pencere)
        if hedef is None:
            raise SystemExit("orijinalde %r bulunamadi (pencere %s)" % (metin, pencere))
        hw, hh = hedef[2] - hedef[0], hedef[3] - hedef[1]
        en_iyi = None
        if metin in SABIT:
            f, (ox, oy), bb = _glif(metin, SABIT[metin][0], SABIT[metin][1], renk)
            en_iyi = (0, SABIT[metin][1], f, ox, oy, bb)
        for yol_ in (() if metin in SABIT else (yol, BOLD if yol == REG else REG)):           # agirlik da olculur
            for boy in range(boy0 - 8, boy0 + 9):
                f, (ox, oy), bb = _glif(metin, yol_, boy, renk)
                hata = abs((bb[2] - bb[0]) - hw) + abs((bb[3] - bb[1]) - hh) + (0.5 if yol_ != yol else 0)
                if en_iyi is None or hata < en_iyi[0]:
                    en_iyi = (hata, boy, f, ox, oy, bb)
        hata, boy, f, ox, oy, bb = en_iyi
        xy = (hedef[2] - (bb[2] - ox), hedef[3] - (bb[3] - oy))   # murekkep SAG-ALT kosesi hedefe
        sonuc.append((metin, f, renk, xy, boy, hedef, hata))
    return sonuc


def yap(orj):
    """orj: mevcut 100.bmp (birim yerleri buradan olculur). Donus: net 100.bmp + rapor"""
    im = _sekiller()
    d = ImageDraw.Draw(im)
    big = ImageDraw.Draw(Image.new("RGB", (8, 8)))
    # header (home_card: "lm", ZASS genisligi 4x olcumle - konum aynen)
    zw = big.textlength("ZASS", font=ImageFont.truetype(BOLD, 15 * SS)) / SS
    d.text((14, 12), "ZASS", font=ImageFont.truetype(BOLD, 15), fill=INK, anchor="lm")
    d.text((14 + zw + 6, 12), "LITHIUM", font=ImageFont.truetype(BOLD, 15), fill=ORANGE, anchor="lm")
    # etiketler (home_card / uret_guvenlik konumlari)
    d.text((28, 44), "STATE OF CHARGE", font=ImageFont.truetype(REG, 11), fill=MUTED, anchor="la")
    d.text((216, 44), "VOLTAJ", font=ImageFont.truetype(REG, 10), fill=MUTED, anchor="la")
    d.text((216, 105), "AKIM", font=ImageFont.truetype(REG, 10), fill=MUTED, anchor="la")
    d.text((149, 178), "SICAKLIK 1", font=ImageFont.truetype(REG, 10), fill=MUTED, anchor="rm")
    d.text((297, 178), "SICAKLIK 2", font=ImageFont.truetype(REG, 10), fill=MUTED, anchor="rm")
    rapor = []
    for metin, f, renk, xy, boy, hedef, hata in birim_yerlesimi(orj):
        d.text(xy, metin, font=f, fill=renk, anchor="la")
        yeni = _murekkep(im, (hedef[0] - 6, hedef[1] - 6, hedef[2] + 6, hedef[3] + 6))
        rapor.append("%-3s %s %2d  orijinal %s  net %s" % (metin, "kalin" if "uib" in f.path else "normal", boy, hedef, yeni))
    return im, rapor


if __name__ == "__main__":
    orj = Image.open(os.path.join(HERE, "100.bmp")).convert("RGB")
    im, rapor = yap(orj)
    print("\n".join(rapor))
    im.save(os.path.join(HERE, "100_net.png"))
