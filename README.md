# ZASS MANTIS — Battery Display & Logger Unit

STM32-based display and control unit designed for **ZASS Lithium Battery Packs**.

The system reads battery telemetry over **FDCAN / RS485**, visualizes it on a **DWIN HMI** touch screen via UART, and logs every frame received from the pack to an SD card.

<p align="center">
  <img src="docs/mantis-display.gif" width="480" alt="ZASS MANTIS display — boot screen and live telemetry dashboard"/>
  <br/>
  <em>Boot sequence and live telemetry dashboard (DWIN HMI, 320×240)</em>
</p>

## Features

- 📊 Displays cell voltages, total pack voltage, SOC and temperature
- 🔌 **2× FDCAN** and **1× RS485** communication ports
- 🖥️ **UART** driver for the DWIN HMI display
- ⚠️ Detailed fault monitoring (over-voltage, under-voltage, over-temperature, over-current)
- 💾 Full data logging to SD card (FATFS) — every frame coming from the pack is stored

## Communication Flow

```
BMS ──(FDCAN / RS485)──▶ MANTIS ──(UART / FDCAN)──▶ DWIN DISPLAY
        raw battery data        parsed data & screen variables
```

## SD Card Data Logging

Every frame received from the pack is appended to a daily CSV file. Logs are organized into year / month folders on the SD card:

```
/2026/07_2026/09_07_2026_LOG.CSV
```

Example log content:

```csv
2026.07.09 14:32:01,52.61,3287,3290,3288,3286,3291,3289,3287,3285,26,25,-14.6,78
2026.07.09 14:32:02,52.60,3287,3289,3288,3286,3290,3289,3286,3285,26,25,-14.8,78
2026.07.09 14:32:03,52.58,3286,3289,3287,3285,3290,3288,3286,3284,26,25,-15.1,78
2026.07.09 14:32:04,52.59,3287,3290,3288,3286,3290,3289,3287,3285,26,25,-14.9,78
```

| Column | Field | Unit |
|---|---|---|
| 1 | Timestamp (RTC) | `YYYY.MM.DD hh:mm:ss` |
| 2 | Total pack voltage | V |
| 3–10 | Cell voltages 1–8 | mV |
| 11 | MOSFET temperature | °C |
| 12 | Battery temperature | °C |
| 13 | Current (+ charge / − discharge) | A |
| 14 | State of charge | % |

## Repository Structure

| Folder | Contents |
|---|---|
| `01_HARDWARE_ALTIUM/` | Altium Designer project, Gerber outputs, BOM, 3D STEP model |
| `02_Firmware_STM32/` | STM32CubeIDE firmware project (`ZASS_MANTIS_114`) |
| `04_DWIN_DISPLAY/` | DWIN HMI screen project and image assets |

## Hardware

- **MCU:** STM32 (2× FDCAN)
- **Display:** DWIN HMI (UART)
- **Storage:** microSD card (FATFS)

## Status

| Milestone | State |
|---|---|
| Hardware design | ✅ Completed |
| First sample PCB | ✅ Ordered & validated |
| **-30 version design** | ✅ **Completed** |
| **Production batch (50 units)** | 🏭 **Ordered** |
