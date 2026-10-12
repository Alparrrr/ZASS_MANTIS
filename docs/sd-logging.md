# SD card logging and clock

Files are organised by year and month:

```
2026/10_2026/09_10_2026_LOG.CSV      measurements, one file per day, one row per second
2026/10_2026/OLAYLAR_10_2026.CSV     events, one file per month, one line per event
```

## Measurement log

Logging starts once every frame type has arrived (or after 5 seconds), so the first rows are not
half-filled. A header is written at the start of each session and again whenever the cell count or
the data source changes.

```csv
Saat kaynagi LSE, BMS kaynagi CAN, 25 hucre
Tarih Saat,Kaynak,Paket V,Akim A,SOC %,MOS C,Maks C,Min C,Hucre Fark mV,Ort Hucre mV,Hucre Max mV,Hucre Min mV,Sarj MOS,Desarj MOS,Sarj Fisi,BMS Uyari,Guvenlik,T1 C,T2 C,Hucre Sayisi,H1 mV,H2 mV,...,H25 mV
2026.10.08 09:00:02,CAN,82.40,-42.2,76,36,31,29,37,3296,3305,3268,1,1,0,0000,,31,29,25,3302,3302,...,3290
```

| Column | Meaning |
|---|---|
| `Kaynak` | data source: `CAN` or `UART` |
| `Paket V`, `Akim A`, `SOC %` | pack voltage, current (+ charge / − discharge), state of charge |
| `MOS C`, `Maks C`, `Min C` | MOSFET temperature; highest and lowest battery temperature (same values as on screen) |
| `Hucre Fark / Ort / Max / Min mV` | cell spread, average, highest and lowest cell |
| `Sarj MOS`, `Desarj MOS`, `Sarj Fisi` | charge switch, discharge switch, charger plugged |
| `BMS Uyari` | BMS fault word (hex) |
| `Guvenlik` | active safety rules, e.g. `K1 K7` |
| `T1 C`… | the battery probes that are actually fitted (empty slots are not written) |
| `Hucre Sayisi`, `H1 mV`… | cell count and every cell voltage |

## Event log

One line per event: when it happened and what happened, in plain Turkish. Cell events name the cell.
An event that keeps toggling within 60 seconds is written once.

```csv
Tarih Saat,Kod,Olay
2026.10.09 08:00:02,SISTEM,Cihaz açıldı: güç açıldı ya da besleme düştü
2026.10.09 14:31:05,K1,Aşırı şarj: 7. hücre 3.712 V ile 3.700 V sınırını geçti / şarj kesikken akım +6.0 A
2026.10.09 15:02:40,K2,Aşırı deşarj: 12. hücre 2.580 V ile 2.600 V sınırının altına düştü / akım -3.8 A
2026.10.09 16:20:00,K4,Batarya sıcaklığı yüksek: 57 °C (sınır 55 °C)
2026.10.09 09:02:05,K7,BMS haberleşme hatası: 5 sn'den uzun süre veri gelmedi
```

The lines above illustrate the format. The file is UTF-8 with a byte-order mark, so Excel shows the
Turkish characters correctly. Real output produced by the test suite is in
[`02_Firmware_STM32/tests/ornek_sd/`](../02_Firmware_STM32/tests/ornek_sd/).

## Setting the clock

1. Create `SAAT.TXT` in the root of the card with one line: `09.10.2026 14:30:00`
   (or `2026-10-09 14:30:00`).
2. Insert the card. The clock is set and the file is renamed to `SAAT_OK.TXT`
   (`SAAT_HATALI.TXT` if the text could not be read).

The card must be **FAT32**.

[← Back to README](../README.md)
