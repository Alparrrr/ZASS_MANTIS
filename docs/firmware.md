# Firmware, tests and display project

## Firmware modules

All under `02_Firmware_STM32/ZASS_MANTIS_115/Core/`.

| Module | Role |
|---|---|
| `guvenlik.c/.h` | the 8 safety rules, display overlay, buzzer, on-board event memory |
| `bms_veri.c/.h` | CAN and UART decoding into one data structure, CSV formatting |
| `Task_SDCard.c` | SD logging, card detect, clock file, start-up reason |
| `saat.c/.h` | RTC clock source selection, calendar, text parsing |
| `soc_bar.c` | SOC bar, working-state chip, digit colours |

## Build

Open `02_Firmware_STM32/ZASS_MANTIS_115` in **STM32CubeIDE 1.18** and build. Both configurations
optimise for size; the Debug image is about 85 KB of the 128 KB flash. The last two flash pages are
reserved for the on-board event memory.

## Tests

The rules, decoders, clock code and SD logging are compiled from the same sources and run on a PC
against a simulated BMS, flash and file system. No hardware needed.

```
pip install ziglang
cd 02_Firmware_STM32	ests
calistir.bat
```

It runs eight build configurations (about 1,500 checks) and a full scenario: a 25-cell pack on CAN,
then an 8-cell pack on UART. Sample output: [`tests/ornek_sd/`](../02_Firmware_STM32/tests/ornek_sd/).

## Display project

`04_DWIN_DISPLAY/DWIN_SET` is the folder the display loads from its SD card. The warning overlay uses
`50.icl` (status strip, 296 × 22, 7 frames) and `51.icl` (pop-ups, 320 × 240, 11 frames); their source
frames are in `04_DWIN_DISPLAY/50` and `/51`. Text in these images is drawn at the panel's native
resolution so it stays sharp. To regenerate the artwork:

```
cd 04_DWIN_DISPLAY\generator
python uret_guvenlik.py        # page background, strip and pop-up frames
python docs_gorselleri.py      # screenshots and animation used in the docs
```

> On a 16 MB T5L display, file numbers must be 0–63. A library numbered 64 or higher is silently
> ignored: the icon never appears.

[← Back to README](../README.md)
