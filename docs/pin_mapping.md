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
to open. The firmware drives it LOW on its configured critical CMU, CAN,
temperature, or emergency voltage fault. This signal is not a main contactor
drive or an ESS sequencing output. The observed API state on 16 October 2026
was `batterySafeToUse: true` with protection `OK`; that readback does not prove
the downstream SSR/GEYA/V9 breaker chain physically opened in a fault test.

The MCP2515 oscillator setting defaults to 16 MHz and is selectable with
`CAN_CRYSTAL_MHZ`. The physical oscillator must be checked against the actual
board before changing that setting or diagnosing CAN-A timing.

The current source has no pack-current sensor inputs, coulomb counter, AC/key
inputs, or ESS contactor/precharge/charger-enable pin assignments. The board's
silkscreen-to-header positions and external control-box wiring are not
verified by this software map.
