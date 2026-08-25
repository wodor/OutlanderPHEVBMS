# Agent Notes: Outlander PHEV BMS on T-2Can

## Project Overview

This is the standalone Outlander PHEV CMU monitor for the LilyGO T-2Can board
(ESP32-S3).

**Purpose:** Read cell voltages and temperatures from Mitsubishi Outlander PHEV battery modules via CAN bus, with optional cell balancing control.

## Hardware

### T-2Can Board
- **MCU:** ESP32-S3 (240MHz, 320KB RAM, 16MB Flash)
- **Two CAN interfaces:**
  - **CAN-A:** External MCP2515 controller via SPI
  - **CAN-B:** ESP32's built-in TWAI controller
- Both buses are implemented. The live eight-CMU configuration currently selects Bus B; the final conflicting-ID split is expected to use both buses.
- **USB:** Native USB CDC for serial communication
- **Serial port:** `/dev/cu.usbmodem2101` (may vary)

### Pin Mapping (from T-2Can schematic)
```
MCP2515 (CAN-A):
  CS   = GPIO 10
  SCLK = GPIO 12
  MOSI = GPIO 11
  MISO = GPIO 13
  RST  = GPIO 9

Built-in TWAI (CAN-B):
  TX = GPIO 7
  RX = GPIO 6
```

### MCP2515 Notes
- Crystal frequency must match the physical board; it is compile-time selectable
  with `CAN_CRYSTAL_MHZ` and the current source default is 16 MHz.
- Do not infer the oscillator from old handoff notes. Verify it before using
  CAN-A; the live eight-CMU installation currently uses TWAI Bus B.
- Requires reset sequence: HIGH → LOW → HIGH with delays

## Outlander BMS CAN Protocol

### Bus Configuration
- Baud rate: 500 kbit/s
- Standard CAN (11-bit IDs)

### Message IDs
The battery pack has 10 CMUs (Cell Monitoring Units). Each CMU sends 3 message types:

| CMU | Status (temps) | Voltages 1-4 | Voltages 5-8 |
|-----|----------------|--------------|--------------|
| 1   | 0x011          | 0x012        | 0x013        |
| 2   | 0x021          | 0x022        | 0x023        |
| ... | ...            | ...          | ...          |
| 8   | 0x081          | 0x082        | 0x083        |

### Message Formats (8 bytes each)

**Type 1 (Status + Temperatures):**
```
[0]: Balance status bitmask (bit 0 = cell 1, etc.)
[1]: Unknown
[2-3]: Temperature 1 (big-endian, multiply by 0.001 for °C)
[4-5]: Temperature 2
[6-7]: Temperature 3
```

**Type 2 (Cells 1-4):**
```
[0-1]: Cell 1 voltage (mV, big-endian)
[2-3]: Cell 2 voltage
[4-5]: Cell 3 voltage
[6-7]: Cell 4 voltage
```

**Type 3 (Cells 5-8):**
```
[0-1]: Cell 5 voltage (mV, big-endian)
[2-3]: Cell 6 voltage
[4-5]: Cell 7 voltage
[6-7]: Cell 8 voltage
```

### Balance Command (TX)
Send to ID `0x3C3` every ~200 ms:
```
[0]: Target voltage high byte (eighth-lowest valid cell mV)
[1]: Target voltage low byte
[2]: Enable flag (1 = balance, 0 = off)
[3]: 4 (fixed)
[4]: 3 (fixed)
[5-7]: 0
```

## Build System

### PlatformIO Configuration
- Platform: `espressif32 @6.5.0`
- Board: `esp32s3_flash_16MB`
- Framework: Arduino
- Board definition: self-contained in `boards/esp32s3_flash_16MB.json`
- MCP2515 and network libraries: pinned through `platformio.ini`
- No Battery-Emulator or external T-2Can checkout is required to build

### Testing
The project includes unit tests that run on the `native` platform (x86/Linux):

```bash
pio test -e native                # Run tests
pio test -e native -vv            # Run with verbose output
```

Tests cover:
- BMS data structures and calculations
- voltage-derived SOC calculation
- persisted configuration validation
- GPIO15 protection/fail-low behavior
- pack statistics and safety-critical boundaries

### CI/CD Configuration

**GitHub Actions** (`.github/workflows/ci.yml`): Automated testing
- Runs on: Ubuntu latest
- Triggers: Pushes to main, all pull requests
- Steps:
  1. **Checkout code**
  2. **Set up Python 3.x**
  3. **Cache PlatformIO**: Caches `~/.platformio` and `~/.cache/pip` for faster builds
  4. **Install PlatformIO**: Installs via pip
  5. **Pre-install dependencies**: 
     - Installs `native` platform: `pio pkg install --platform native`
     - Downloads all environment dependencies: `pio pkg install -e native`
     - This ensures all libraries are cached before running tests
  6. **Run tests**: Executes `pio test -e native`

**Copilot Setup Steps** (`.github/workflows/copilot-setup-steps.yml`): Pre-configure Copilot's environment
- Job name: `copilot-setup-steps` (required for Copilot to pick it up)
- Runs on: Ubuntu latest
- Triggers: Workflow dispatch, changes to the setup file
- Purpose: Pre-installs PlatformIO and dependencies before Copilot coding agent starts
- Steps (same as CI workflow):
  1. **Checkout code**
  2. **Set up Python 3.x**
  3. **Cache PlatformIO**: Caches dependencies for faster agent startup
  4. **Install PlatformIO**: Installs via pip
  5. **Pre-install dependencies**: Downloads `native` platform and packages

**Note for Copilot Agents**: The `copilot-setup-steps.yml` workflow runs automatically before you start working, ensuring PlatformIO and all dependencies are pre-installed and cached. This prevents firewall/network issues when you need to run PlatformIO commands, as everything is already available locally. The CI workflow provides the same setup for automated testing.

**Current CI caveat (20 August 2026):** local native tests passed 27/27, but
the latest GitHub `main` workflow is red because `test/mocks/Arduino.h` defines
a `min` macro that collides with the Ubuntu/GCC standard library. Do not report
CI green until that mock is fixed and a new workflow succeeds.

### Build Commands
```bash
cd "$(git rev-parse --show-toplevel)"
pio run                                              # Build
pio run -t upload --upload-port /dev/cu.usbmodem2101 # Upload
pio device monitor --port /dev/cu.usbmodem2101      # Serial monitor
```

## Code Structure

```
src/
├── main.cpp          # Entry point, timing loop
├── config.h          # Hardware pins, constants
├── bms_data.h/cpp    # Data structures, global state
├── can_handler.h/cpp # CAN bus communication
├── protection.h/cpp  # GPIO15 safety permissive
├── soc_calc.h/cpp    # Voltage-derived telemetry SOC
├── mqtt_handler.h/cpp# MQTT telemetry
├── web_server.h/cpp  # Dashboard and API
└── serial_menu.h/cpp # Local command interface
```

### Key Patterns Used
1. **Non-blocking timing:** `millis()` pattern instead of `delay()`
2. **Module-private state:** `static` variables at file scope
3. **Global state:** Single `g_bmsState` struct for BMS data
4. **Polling loop:** Check inputs, do periodic tasks, repeat

## Gotchas

1. **USB Serial startup:** Add `delay(1000)` after `Serial.begin()` or early prints are lost
2. **MCP2515 crystal:** Source defaults to 16 MHz, but the exact board must be verified before using CAN-A; a mismatch gives the wrong baud rate
3. **Port availability:** Close VS Code serial monitor before uploading
4. **CAN termination:** May need 120Ω terminator on CAN bus depending on setup

## Responsibility Boundary

This repository owns:

- CMU voltage and temperature acquisition over CAN A/B;
- expected-CMU freshness checks and pack statistics;
- optional CMU balancing control;
- MQTT, web, and serial telemetry/diagnostics;
- voltage-derived SOC telemetry;
- the active-HIGH GPIO15 `BATTERY_SAFE_TO_USE` physical permissive.

This repository intentionally does **not** own:

- pack-current measurement or coulomb counting;
- FoxESS/inverter CAN framing;
- charge/discharge operating-current policy;
- ESS contactor or precharge sequencing;
- Home Assistant operating-mode policy.

Those inverter-facing responsibilities belong to the sibling
`PowerWall-Gateway` repository. MQTT is the current supervised data transport;
a future short framed serial sender will replace that dependency after the
T-CAN485 receiver pins and protocol are finalized.

## Current Protection Contract

- GPIO15 is active HIGH: HIGH means safe-to-use, LOW must trip the external breaker chain.
- It drops LOW for high temperature, no global CMU CAN for 10 seconds, a selected CMU stale/missing for 10 seconds, cell voltage `>= 4200 mV`, or cell voltage `<= 2800 mV`.
- Temperature and communication faults cannot be overridden.
- The supervised override can suppress only the emergency voltage trip and requires fresh CMU data plus safe temperature.
- The configured 4.05/3.20 V SOC endpoints are telemetry/design values, not the 4.20/2.80 V emergency thresholds. Historical 4.00 V and 4.05 V high endpoints migrate to 4.05 V and 4.10 V respectively; 4.20 V is never a normal target.

## Current State and Remaining Work

- `main` matched `origin/main` at the 20 August 2026 audit.
- Local native tests previously passed 27/27 and the ESP32-S3 build passed.
- The cleanup/flattening commits have not been OTA-proven; record the exact commit and image hash on the next deployment.
- Fix the GitHub native CI macro collision and obtain a green run.
- Physically prove GPIO15 LOW through the SSR/GEYA/V9 breaker chain for every fault and power-loss case.
- Wire/configure the second CMU bus and prove loss-of-either-bus behavior with all 10 modules.
- Implement the framed serial sender only after the PowerWall-Gateway receiver contract is fixed.
- GitHub still labels this project as a fork even though its local upstream remote and inherited firmware have been removed.

## OTA Updates

- Arduino OTA is serviced on every main-loop iteration after WiFi connects.
- Hostname defaults to `outlander-bms.local`.
- `outlander_bms_ota` uploads to `192.168.2.90` by default.
- OTA authentication comes from ignored `.config.h`; never commit credentials.
