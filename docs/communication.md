# Communication

```
BMS ──CAN 250 kbit/s──▶┐
                       ├─▶ MANTIS ──UART 115200──▶ DWIN display
BMS ──UART 2400 baud──▶┘      │
                              ├─▶ SD card (SPI, FatFs)
Charger ◀─CAN 125 kbit/s──────┘
```

| Port | Use in this firmware |
|---|---|
| FDCAN1 | BMS, 250 kbit/s, JK / ZASS CAN protocol V2.1 |
| FDCAN2 | charger, 125 kbit/s |
| USART3 | DWIN display, 115200 baud |
| USART6 | BMS over UART, 2400 baud |
| SPI1 + card-detect | microSD |
| RS485 | fitted on the board, not used by this firmware yet |

CAN frames decoded: pack status `0x02F4`, cell max/min with cell numbers `0x04F4`, temperature
max/min `0x05F4`, cell voltages `0x18E0…E6` (4 cells per frame), all temperatures `0x18F2`, fault
word `0x18F3`, switch status `0x18F5`.

> Protocol V2.1 defines 24 cells (frames `E0`–`E5`). The 25th cell is read from frame `E6` by analogy;
> this has not been confirmed against a 25-cell pack.

[← Back to README](../README.md)
