# Safety rules

| # | Rule | Trips when | Delay | Screen |
|---|---|---|---|---|
| 1 | Over-charge / stuck charge contactor | highest cell > 3.70 V **and** charge current > 5 A with the charge switch open | 5 s | pop-up |
| 2 | Over-discharge / stuck discharge contactor | lowest cell < 2.60 V **or** discharge current > 5 A with the discharge switch open | 5 s | pop-up |
| 3 | Cell imbalance | highest − lowest cell ≥ 300 mV | 2 s | strip |
| 4 | Temperature high | highest temperature > 55 °C | 2 s | strip |
| 5 | Temperature critical | highest temperature > 60 °C | 2 s | pop-up |
| 6 | Temperature too low | lowest temperature < −10 °C | 2 s | pop-up |
| 7 | Communication loss | no BMS status frame for 5 s | — | pop-up |
| 8 | Temperature sensor fault | fewer than 3 valid sensors on CAN (2 on UART), or no temperature data | 5 s | strip |

All thresholds are `#define`s at the top of
[`guvenlik.h`](../02_Firmware_STM32/ZASS_MANTIS_115/Core/Inc/guvenlik.h). Highest / lowest temperature
is taken from the BMS's own max/min **and** from every valid battery probe, so a single probe going
out of range is enough to trip a rule. The MOSFET temperature is logged but does not trip the
battery-temperature rules.

[← Back to README](../README.md)
