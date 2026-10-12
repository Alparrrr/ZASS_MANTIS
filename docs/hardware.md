# Hardware

| | |
|---|---|
| MCU | STM32G0B1CBT6 (128 KB flash, 2× FDCAN) |
| Display | DWIN T5L, 2.8", 320 × 240, DGUS II |
| Storage | microSD over SPI (FAT32) with card-detect |
| Clock | RTC on a 32.768 kHz crystal, **backup battery** |
| Interfaces | 2× CAN, 1× RS485, 2× UART, buzzer |
| Components | **all electronic components are automotive grade (AEC-Q100 / AEC-Q102)** |

## Board revisions

| Change | Notes |
|---|---|
| Backup battery added | keeps the real-time clock running when the unit is powered off |
| SD card socket moved | new position on the board; card-detect wired to `PA3` |
| Everything else | design unchanged |

The Altium project is in [`01_HARDWARE_ALTIUM/`](../01_HARDWARE_ALTIUM/) (`PCB1.PcbDoc`, `PCB2.PcbDoc`).

[← Back to README](../README.md)
