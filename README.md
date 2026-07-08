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

## Repository Structure

| Folder | Contents |
|---|---|
| `01_HARDWARE_ALTIUM/` | Altium Designer project (STM32G0B1CBT6), Gerber outputs, BOM, 3D STEP model |
| `02_Firmware_STM32/` | STM32CubeIDE firmware project (`ZASS_MANTIS_114`) |
| `04_DWIN_DISPLAY/` | DWIN HMI screen project and image assets |

## Hardware

- **MCU:** STM32G0B1CBT6 (Cortex-M0+, 2× FDCAN)
- **Display:** DWIN HMI (UART)
- **Storage:** microSD card (FATFS)

## Status

| Milestone | State |
|---|---|
| Hardware design | ✅ Completed |
| First sample PCB | ✅ Ordered & validated |
| **-30 version design** | ✅ **Completed** |
| **Production batch (50 units)** | 🏭 **Ordered** |
