# Pin Mapping (T-2Can Header)

This document records the GPIO assignments used by the PlatformIO port and how the
silkscreened header labels map to the ESP32-S3 IOxx numbers used in code.

## Variant Used by This Project

PlatformIO uses the custom board definition `esp32s3_flash_16MB`, which sets:

- `variant = esp32s3`

That means the active Arduino pin map comes from:

- `.pio-core/packages/framework-arduinoespressif32/variants/esp32s3/pins_arduino.h`

This variant does not define `D4`, `D5`, etc. The code in this repo uses raw GPIO
numbers (e.g. `4`, `5`, `15`, `16`, `39`) which matches the `esp32s3` variant.

## Board Label Mapping

The board silkscreen uses numeric labels that correspond 1:1 to the GPIO number.
This document and the code use `IOxx` notation.

IO numbers used by this project:
- `IO4`
- `IO5`
- `IO15`
- `IO16`
- `IO17`
- `IO18`
- `IO21`
- `IO39`
- `IO41`
- `IO42`

## ESS Control I/O (implemented)

Inputs (active HIGH):
- AC presence: `IO39`
- Key/Enable: `IO41`
- AUX input (optional): `IO42`

Outputs (active HIGH):
- Main contactor: `IO15`
- Precharge: `IO16`
- Negative contactor: `IO17`
- Charger enable: `IO18`
- Discharge enable (optional): `IO21`

## Current Sensing (analog)

Analog inputs:
- Low-range sensor: `IO4`
- High-range sensor: `IO5`

These pins are configured in `src/config.h` as:
- `PIN_CURRENT_SENSE_LOW`
- `PIN_CURRENT_SENSE_HIGH`

## Back-of-Board Header Wiring (ASCII Guide)

Back view, header pins as printed, USB at bottom. Only used pins are labeled
with signal names; others are shown as `NC` (not connected).

```
Left column (top)                     Right column (top)
┌───────────────────────────────┐    ┌───────────────────────────────┐
│ 3V3            [3V3]          │    │ GND            [GND]          │
│ 5V             [5V]           │    │ GND            [GND]          │
│ IO35           [NC]           │    │ IO39           [IN:AC_PRESENT]│
│ IO38           [NC]           │    │ IO42           [IN:AUX_IN]    │
│ IO37           [NC]           │    │ IO41           [IN:KEY_ON]    │
│ IO36           [NC]           │    │ IO40           [NC]           │
│ IO16           [OUT:PRECHG]   │    │ IO4            [IN:CUR_LOW]   │
│ IO15           [OUT:MAIN]     │    │ IO5            [IN:CUR_HIGH]  │
│ IO45           [NC]           │    │ IO48           [NC]           │
│ IO47           [NC]           │    │ IO21           [OUT:DISCHG_EN]│
│ IO14           [NC]           │    │ IO17           [OUT:NEG_CONT] │
│ IO18           [OUT:CHG_EN]   │    │ GND            [GND]          │
│ IO46           [NC]           │    │ IO3            [NC]           │
└───────────────────────────────┘    └───────────────────────────────┘
Left column (bottom)                  Right column (bottom)
```
