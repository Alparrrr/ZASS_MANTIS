# -*- coding: utf-8 -*-
"""
README gorselleri: uret_guvenlik.py'nin onizlemelerinden (onizleme/*.png)
  docs/screens/*.png           her ekran, 2 kat buyutulmus (640x480)
  docs/safety-warnings.gif     uyari ekranlari turu (seritler + yanip sonen pop-up'lar)
Once:  python uret_guvenlik.py     Sonra:  python docs_gorselleri.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ONIZ = os.path.join(HERE, "onizleme")
DOCS = os.path.normpath(os.path.join(HERE, "..", "..", "docs"))
F = "C:/Windows/Fonts/"
Z = 2                                               # buyutme (tam kat: piksel net kalir)
ALT = 44                                            # GIF altyazi seridi

# (onizleme dosyasi, docs/screens adi, altyazi basligi, altyazi aciklamasi)
SERIT = [("01-normal", "main-normal", "Main screen", "everything OK: status strip says \"Batarya durumu normal\""),
         ("02-k4", "strip-rule4-temp-high", "Rule 4 · temperature above 55 °C", "warning strip, beep every 5 s"),
         ("03-k8", "strip-rule8-sensor", "Rule 8 · temperature sensor fault", "warning strip, beep every 5 s"),
         ("04-k3", "strip-rule3-imbalance", "Rule 3 · cell imbalance ≥ 0.3 V", "dimmed strip for 15 min, no buzzer")]
POPUP = [("11-k1", "popup-rule1-overcharge", "Rule 1 · over-charge", "pop-up + continuous buzzer"),
         ("12-k2", "popup-rule2-overdischarge", "Rule 2 · over-discharge", "pop-up + continuous buzzer"),
         ("15-k5", "popup-rule5-temp-critical", "Rule 5 · temperature above 60 °C", "pop-up + continuous buzzer"),
         ("16-k6", "popup-rule6-cold", "Rule 6 · temperature below −10 °C", "pop-up + continuous buzzer"),
         ("17-k7", "popup-rule7-comm", "Rule 7 · no BMS data for 5 s", "pop-up, values hidden, beep every 5 s")]


def buyut(ad):
    im = Image.open(os.path.join(ONIZ, ad + ".png")).convert("RGB")
    return im.resize((im.width * Z, im.height * Z), Image.NEAREST)


def altyazili(im, baslik, aciklama):
    out = Image.new("RGB", (im.width, im.height + ALT), (14, 16, 20))
    out.paste(im, (0, 0))
    d = ImageDraw.Draw(out)
    d.text((14, im.height + 5), baslik, font=ImageFont.truetype(F + "segoeuib.ttf", 16), fill=(237, 239, 243))
    d.text((14, im.height + 25), aciklama, font=ImageFont.truetype(F + "segoeui.ttf", 12), fill=(138, 144, 156))
    return out


def main():
    os.makedirs(os.path.join(DOCS, "screens"), exist_ok=True)
    kareler, sureler = [], []
    boot = Image.new("RGB", (320 * Z, 240 * Z), (0, 0, 0))                     # acilis ekrani
    bd = ImageDraw.Draw(boot)
    bd.text((160 * Z, 105 * Z), "ZASS", font=ImageFont.truetype(F + "ariblk.ttf", 56 * Z // 2 * 2), fill=(255, 255, 255), anchor="mm")
    bd.text((160 * Z, 150 * Z), "LITHIUM BATTERIES", font=ImageFont.truetype(F + "segoeui.ttf", 15 * Z), fill=(130, 130, 130), anchor="mm")
    kareler.append(altyazili(boot, "Start-up", "boot screen, then the dashboard")); sureler.append(1800)
    for ad, cikti, bas, acik in SERIT:
        im = buyut(ad)
        im.save(os.path.join(DOCS, "screens", cikti + ".png"), optimize=True)
        kareler.append(altyazili(im, bas, acik)); sureler.append(2400)
    for ad, cikti, bas, acik in POPUP:
        f0, f1 = buyut(ad + "-faz0"), buyut(ad + "-faz1")
        f0.save(os.path.join(DOCS, "screens", cikti + ".png"), optimize=True)
        for _ in range(2):                           # pop-up basligi 500 ms'de bir yanip soner
            kareler.append(altyazili(f0, bas, acik)); sureler.append(500)
            kareler.append(altyazili(f1, bas, acik)); sureler.append(500)
    # her kare kendi paletiyle (duz renkli arayuz: dither yok, bant yok)
    q = [k.convert("P", palette=Image.Palette.ADAPTIVE, colors=256, dither=Image.Dither.NONE) for k in kareler]
    yol = os.path.join(DOCS, "demo.gif")
    q[0].save(yol, save_all=True, append_images=q[1:], duration=sureler, loop=0, optimize=False, disposal=1)
    print("docs/screens: %d png, demo.gif %d kare, %.0f KB" % (
        len(SERIT) + len(POPUP), len(q), os.path.getsize(yol) / 1024))


if __name__ == "__main__":
    main()
