# -*- coding: utf-8 -*-
"""
README gorselleri, YUKSEK COZUNURLUKLU (3x = 960x720): ekranlar buyutulmez, dogrudan 3 kat
buyuk cizilir (sekiller ve yazilar). Panelin kendisi 320x240'tir; bunlar tanitim icindir.
  docs/screens/*.png     her ekran 960x720
  docs/demo.gif          acilis + ana ekran + uyari ekranlari turu
Once:  python uret_guvenlik.py   (100.bmp icin)     Sonra:  python docs_gorselleri.py
"""
import os
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import uret_guvenlik as G          # popup / serit cizicileri (kalip ve metinler tek yerde)
import net_100 as N

K = 3                              # cikis olcegi
W, H = 320 * K, 240 * K
DOCS = os.path.normpath(os.path.join(HERE, "..", "..", "docs"))
FW = "C:/Windows/Fonts/"
BAS, MUTED, INK, ORANGE, AMBER = (14, 16, 20), N.MUTED, N.INK, N.ORANGE, (245, 166, 35)
ALT = 64


# ------------------------------------------------------------------ uretici cizicilerini K olcegine al
def _kucuk(im):                    # 4x tuval -> K olcegi (net LANCZOS)
    return im.resize((im.width * K // G.SS, im.height * K // G.SS), Image.LANCZOS)


def _ertele_ciz(im):               # ertelenen yazilar K olcegindeki boyda, o cozunurlukte
    d = ImageDraw.Draw(im)
    for xy, s, yol, boy, fill, anchor in G._ERTELE:
        d.text((xy[0] * K, xy[1] * K), s, font=ImageFont.truetype(yol, max(1, int(round(boy * K)))), fill=fill, anchor=anchor)
    G._ERTELE = None
    return im


G.kucuk = _kucuk
G.ertele_ciz = _ertele_ciz


def f(yol, boy):
    return ImageFont.truetype(yol, max(1, int(round(boy * K))))


# ------------------------------------------------------------------ hizli seffaflik (siyah = seffaf)
def bindir(alt, ust, x=0, y=0, esik=40):
    r, g, b = ust.split()
    m = ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 255 if v > esik else 0)
    out = alt.copy()
    out.paste(ust, (int(x * K), int(y * K)), m)
    return out


# ------------------------------------------------------------------ ana sayfa (taban)
def taban_sayfa(orj):
    S = G.SS
    cv = Image.new("RGB", (320 * S, 240 * S), BAS)               # sekiller 4x'te, K'ye kuculur
    d = ImageDraw.Draw(cv)

    def kart(box, r):
        d.rounded_rectangle([c * S for c in box], radius=r * S, fill=N.CARD, outline=N.CARD_E, width=S)
        x0, y0, x1, _ = box
        d.line([((x0 + 8) * S, (y0 + 1) * S), ((x1 - 8) * S, (y0 + 1) * S)], fill=N.CARD_HI, width=S)

    d.line([(12 * S, 26 * S), (308 * S, 26 * S)], fill=N.CARD_E, width=S)
    for b in N.UST_KARTLAR:
        kart(b, 10)
    d.rounded_rectangle([26 * S, 124 * S, 176 * S, 140 * S], radius=5 * S, fill=N.TRACK, outline=N.CARD_E, width=S)
    d.rounded_rectangle([178 * S, 128 * S, 181 * S, 136 * S], radius=1 * S, fill=N.CARD_E)
    for b, r in N.ALT_KARTLAR:
        kart(b, r)
    im = cv.resize((W, H), Image.LANCZOS)
    d = ImageDraw.Draw(im)
    zw = d.textlength("ZASS", font=f(N.BOLD, 15)) / K
    d.text((14 * K, 12 * K), "ZASS", font=f(N.BOLD, 15), fill=INK, anchor="lm")
    d.text(((14 + zw + 6) * K, 12 * K), "LITHIUM", font=f(N.BOLD, 15), fill=ORANGE, anchor="lm")
    for xy, t, boy, anc in (((28, 44), "STATE OF CHARGE", 11, "la"), ((216, 44), "VOLTAJ", 10, "la"), ((216, 105), "AKIM", 10, "la"),
                            ((149, 178), "SICAKLIK 1", 10, "rm"), ((297, 178), "SICAKLIK 2", 10, "rm")):
        d.text((xy[0] * K, xy[1] * K), t, font=f(N.REG, boy), fill=MUTED, anchor=anc)
    for metin, fnt, renk, xy, boy, hedef, hata in N.birim_yerlesimi(orj):    # %, V, A, C: panelle ayni yerde
        d.text((xy[0] * K, xy[1] * K), metin, font=f(fnt.path, boy), fill=renk, anchor="la")
    return im


def durum_cipi(im, metin="BEKLEMEDE", renk=AMBER):
    d = ImageDraw.Draw(im)
    tw = d.textlength(metin, font=f(N.BOLD, 10)) / K
    x0 = 306 - tw
    d.ellipse([(x0 - 11) * K, 9 * K, (x0 - 5) * K, 15 * K], fill=renk)
    d.text((306 * K, 12 * K), metin, font=f(N.BOLD, 10), fill=renk, anchor="rm")
    return im


def degerler(taban, soc="58", v="26.2", a="0.0", t1="38", t2="31"):
    im = taban.copy()
    d = ImageDraw.Draw(im)

    def t(xy, s, boy):
        d.text((xy[0] * K, xy[1] * K), s, font=f(N.BOLD, boy), fill=INK, anchor="rm")
    t((100, 87), soc, 50); t((275, 74), v, 25); t((270, 133), a, 25)
    t((72, 178), t1, 32); t((223, 178), t2, 32)
    d.rounded_rectangle([32 * K, 126 * K, (32 + 140 * int(soc) / 100) * K, 136 * K], radius=4 * K, fill=(72, 226, 150))
    return durum_cipi(im)


def bilgi_rakamlari(im, deger):
    d = ImageDraw.Draw(im)
    for (ad, vp, sp, bx, bw, birim, ornek, ayar), v in zip(G.BILGI, deger):
        d.text(((bx + bw) * K, (G.RAKAM_Y + G.RAKAM_H / 2) * K), v, font=f(N.BOLD, 13), fill=(240, 242, 246), anchor="rm")
    return im


# ------------------------------------------------------------------ ekran listesi
SERIT = [("main-normal", "Main screen", "everything OK: status strip says \"Batarya durumu normal\"", G.S_NORMAL, {}),
         ("strip-rule4-temp-high", "Rule 4 · temperature above 55 °C", "warning strip, beep every 5 s", G.S_K4, dict(t1="57", t2="52")),
         ("strip-rule8-sensor", "Rule 8 · temperature sensor fault", "warning strip, beep every 5 s", G.S_K8, {}),
         ("strip-rule3-imbalance", "Rule 3 · cell imbalance ≥ 0.3 V", "dimmed strip for 15 min, no buzzer", G.S_K3, {})]
POPUP = [(1, "popup-rule1-overcharge", "Rule 1 · over-charge", "pop-up + continuous buzzer", ("97", "29.7", "+24.5", "33")),
         (2, "popup-rule2-overdischarge", "Rule 2 · over-discharge", "pop-up + continuous buzzer", ("0", "20.6", "-38.2", "36")),
         (5, "popup-rule5-temp-critical", "Rule 5 · temperature above 60 °C", "pop-up + continuous buzzer", ("71", "26.4", "-95.0", "62")),
         (6, "popup-rule6-cold", "Rule 6 · temperature below −10 °C", "pop-up + continuous buzzer", ("44", "25.9", "+4.1", "-12")),
         (7, "popup-rule7-comm", "Rule 7 · no BMS data for 5 s", "pop-up, values hidden, beep every 5 s", None)]


def altyazili(im, baslik, aciklama):
    out = Image.new("RGB", (im.width, im.height + ALT), BAS)
    out.paste(im, (0, 0))
    d = ImageDraw.Draw(out)
    d.text((18, im.height + 8), baslik, font=ImageFont.truetype(FW + "segoeuib.ttf", 26), fill=INK)
    d.text((18, im.height + 38), aciklama, font=ImageFont.truetype(FW + "segoeui.ttf", 18), fill=MUTED)
    return out


def main():
    orj = Image.open(os.path.join(HERE, "100.bmp")).convert("RGB")
    taban = durum_cipi(taban_sayfa(orj))
    norm = degerler(taban)
    os.makedirs(os.path.join(DOCS, "screens"), exist_ok=True)
    kareler, sureler = [], []

    boot = Image.new("RGB", (W, H), (0, 0, 0))
    bd = ImageDraw.Draw(boot)
    bd.text((W // 2, 105 * K), "ZASS", font=ImageFont.truetype(FW + "ariblk.ttf", 56 * K), fill=(255, 255, 255), anchor="mm")
    bd.text((W // 2, 150 * K), "LITHIUM BATTERIES", font=f(N.REG, 15), fill=(130, 130, 130), anchor="mm")
    kareler.append(altyazili(boot, "Start-up", "boot screen, then the dashboard")); sureler.append(1800)

    for cikti, bas, acik, serit, ek in SERIT:
        im = bindir(degerler(taban, **ek) if ek else norm, G.serit_kare(serit), G.SX, G.SY)
        im.save(os.path.join(DOCS, "screens", cikti + ".png"), optimize=True)
        kareler.append(altyazili(im, bas, acik)); sureler.append(2400)

    for kural, cikti, bas, acik, deger in POPUP:
        if deger is None:
            alt = bindir(norm, G.serit_kare(G.S_VERIYOK), G.SX, G.SY)
        else:
            alt = bilgi_rakamlari(bindir(norm, G.serit_kare(G.S_BILGI), G.SX, G.SY), deger)
        f0, f1 = (bindir(alt, G.popup(kural, faz)) for faz in (0, 1))
        f0.save(os.path.join(DOCS, "screens", cikti + ".png"), optimize=True)
        for _ in range(2):                                   # baslik 500 ms'de bir yanip soner
            kareler.append(altyazili(f0, bas, acik)); sureler.append(500)
            kareler.append(altyazili(f1, bas, acik)); sureler.append(500)

    q = [k.convert("P", palette=Image.Palette.ADAPTIVE, colors=256, dither=Image.Dither.NONE) for k in kareler]
    yol = os.path.join(DOCS, "demo.gif")
    q[0].save(yol, save_all=True, append_images=q[1:], duration=sureler, loop=0, optimize=False, disposal=1)
    print("docs/screens: %d png (%dx%d), demo.gif %d kare, %.1f MB" % (
        len(SERIT) + len(POPUP), W, H, len(q), os.path.getsize(yol) / 1048576))


if __name__ == "__main__":
    main()
