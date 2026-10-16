# Current T-2Can firmware pin mapping

This is the GPIO assignment compiled from `src/config.h` for the standalone
Outlander CMU monitor. It describes firmware signals, not a verified physical
header or control-box wiring diagram. Confirm the board schematic and trace the
installed wiring before using any pin to diagnose or change hardware.

| Function | ESP32-S3 GPIO | Direction | Source/role |
| --- | ---: | --- | --- |
| MCP2515 CAN-A chip select | 10 | Output | SPI controller select |
| MCP2515 CAN-A clock | 12 | Output | SPI SCLK |
| MCP2515 CAN-A MOSI | 11 | Output | SPI MOSI |
| MCP2515 CAN-A MISO | 13 | Input | SPI MISO |
| MCP2515 CAN-A reset | 9 | Output | Reset pulse |
| TWAI CAN-B TX | 7 | Output | Built-in CAN controller |
| TWAI CAN-B RX | 6 | Input | Built-in CAN controller |
| `BATTERY_SAFE_TO_USE` | 15 | Output | Active HIGH safety permissive |

GPIO15 is the **only physical control output** in this firmware. HIGH permits
the external breaker chain to remain closed; LOW must cause the external chain
to open. `protectionInit()` drives it HIGH immediately; communication faults are
suppressed until ten seconds of uptime, even with no CMU data. After this
grace period, missing/stale selected-module traffic drives LOW. Detected
critical temperature and emergency voltage faults also drive LOW, subject to
the bounded voltage-only recovery override.

Each accepted CMU type 1–4 refreshes one module timestamp; voltage and
temperature groups have no independent freshness checks, and type 4 updates
neither group. This output must not be described as LOW until complete fresh
data arrives, or as proof that every field is fresh. This signal is not a main
contactor drive or an ESS sequencing output. An API
`batterySafeToUse: true`/`OK` readback does not prove the downstream SSR/GEYA/V9
breaker chain physically opens in a fault or power-loss test.

The MCP2515 oscillator setting defaults to 16 MHz and is selectable with
`CAN_CRYSTAL_MHZ`. The physical oscillator must be checked against the actual
board before changing that setting or diagnosing CAN-A timing.

The current source has no pack-current sensor inputs, coulomb counter, AC/key
inputs, or ESS contactor/precharge/charger-enable pin assignments. The board's
silkscreen-to-header positions and external control-box wiring are not
verified by this software map.
