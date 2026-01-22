# Agent Notes: Outlander PHEV BMS on T-2Can

## Project Overview

This is a port of the [OutlanderPHEVBMS](https://github.com/tomdebree/OutlanderPHEVBMS) project to the LilyGO T-2Can board (ESP32-S3).

**Purpose:** Read cell voltages and temperatures from Mitsubishi Outlander PHEV battery modules via CAN bus, with optional cell balancing control.

## Hardware

### T-2Can Board
- **MCU:** ESP32-S3 (240MHz, 320KB RAM, 16MB Flash)
- **Two CAN interfaces:**
  - **CAN-A:** External MCP2515 controller via SPI (used in this project)
  - **CAN-B:** ESP32's built-in TWAI controller (available but unused)
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
- Crystal: 8MHz (important for baud rate calculation)
- Use `MCP_8MHZ` constant, not `MCP_16MHZ`
- Requires reset sequence: HIGH → LOW → HIGH with delays

## Outlander BMS CAN Protocol

### Bus Configuration
- Baud rate: 500 kbit/s
- Standard CAN (11-bit IDs)

### Message IDs
The battery pack has 8 CMUs (Cell Monitoring Units). Each CMU sends 3 message types:

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
Send to ID `0x3C3` every ~400ms:
```
[0]: Target voltage high byte (lowest cell mV)
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
- Board definition: Uses T-2Can's custom board from `../../T-2Can/boards`
- Libraries: Uses T-2Can's libraries from `../../T-2Can/libraries`

### Build Commands
```bash
cd /Users/artwielogorski/prv/t2can/OutlanderPHEVBMS/t2can_port
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
└── serial_menu.h/cpp # User interface
```

### Key Patterns Used
1. **Non-blocking timing:** `millis()` pattern instead of `delay()`
2. **Module-private state:** `static` variables at file scope
3. **Global state:** Single `g_bmsState` struct for BMS data
4. **Polling loop:** Check inputs, do periodic tasks, repeat

## Original Project Variants

The original repo has two versions:
1. **`Outlander_BMS.ino`** - Simple, uses MCP_CAN library, Arduino-style
2. **`OutlanderBMSV2/`** - Complex, Teensy 3.2 only, uses FlexCAN, ADC, EEPROM

This port is based on the simpler version, adapted to use the `arduino-mcp2515` library that comes with T-2Can.

## Gotchas

1. **USB Serial startup:** Add `delay(1000)` after `Serial.begin()` or early prints are lost
2. **MCP2515 crystal:** T-2Can uses 8MHz, not 16MHz - wrong setting = wrong baud rate
3. **Port availability:** Close VS Code serial monitor before uploading
4. **CAN termination:** May need 120Ω terminator on CAN bus depending on setup

## Future Improvements

- [ ] Add TWAI (CAN-B) support for dual-bus monitoring
- [ ] Store settings in ESP32's NVS (flash) instead of RAM
- [x] Port the full V2 features (SOC calculation, charger control, etc.)
  - [x] SOC calculation with coulomb-counting and voltage fallback
  - [x] Current sensing framework (analog and CAN)
  - [x] Protection system (voltage/temp limits)
  - [x] Pack statistics tracking
  - [x] Enhanced web dashboard and serial interface
  - [ ] Physical current sensor integration (requires hardware)
  - [ ] Charger control (intentionally skipped)
  - [ ] PWM gauge output
  - [ ] Full settings persistence to NVS

## V2 Features Implementation

### State of Charge (SOC) Calculation

**Implementation**: `src/soc_calc.h` and `src/soc_calc.cpp`

The SOC system uses coulomb-counting (amp-hour integration) as the primary method, with voltage-based calculation as a fallback. Key features:

- **Coulomb Counting**: Integrates current over time to track charge/discharge
  ```cpp
  SOC = ((ampSeconds * 0.27777777777778) / (capacity * parallelStrings * 1000)) * 100
  ```
- **Voltage-Based Fallback**: Linear interpolation between configured voltage points
- **NVS Persistence**: SOC is saved every 60 seconds and restored on boot
- **Manual Reset**: Can be reset to 100% via serial command 'r'

### Current Sensing

**Implementation**: `src/current_sense.h` and `src/current_sense.cpp`

Framework supports multiple sensor types:
- **Dual-range analog**: High precision for low currents, wide range for high currents
- **Single-range analog**: Simpler configuration
- **CAN bus sensors**: LEM CAB300/500, IsaScale, Victron Lynx

Features:
- Low-pass exponential moving average filter
- Configurable dead-band for noise rejection
- Automatic range switching for dual-range sensors
- Offset calibration support

**Note**: Current sensor hardware integration requires actual ADC pin configuration and testing.

### Protection System

**Implementation**: `src/protection.h` and `src/protection.cpp`

Monitors and enforces safety limits:
- **Overvoltage**: Cell voltage exceeds `overVoltage` threshold
- **Undervoltage**: Cell voltage below `underVoltage` (with debounce)
- **Overtemperature**: Temperature above `overTemp`
- **Undertemperature**: Temperature below `underTemp`
- **Cell Imbalance**: Voltage difference exceeds `cellGap`

Each protection has hysteresis to prevent oscillation. Status reported via:
- Serial console: `protectionGetStatus()`
- Web dashboard: "Protection" field
- API: `/api/summary` endpoint

### Pack Statistics

**Implementation**: Enhanced `BmsState.updatePackStatistics()` in `src/bms_data.h`

Tracks across all modules:
- Lowest/highest/average cell voltages
- Total pack voltage
- Lowest/highest/average temperatures
- Cell voltage delta (imbalance)

Updated periodically and displayed in serial and web interfaces.

### Data Structures

**BmsSettings** (`src/bms_data.h`): Configuration parameters
- Voltage limits (per cell)
- Temperature limits
- Current limits
- Battery pack configuration (cells, capacity)
- SOC voltage curve
- Current sensor configuration
- Protection thresholds

**BmsState** (`src/bms_data.h`): Runtime state
- CMU data (voltages, temps, balance status)
- Pack statistics (min/max/avg)
- SOC tracking (%, amp-seconds)
- Current measurements
- Protection flags
- Timing variables

### Integration

**Main Loop** (`src/main.cpp`): Periodic tasks
- **50ms**: Update current sensing
- **100ms**: Update SOC calculation
- **400ms**: Send CAN balance command
- **500ms**: Check protection limits, update display
- **1000ms**: Poll WiFi
- **60000ms**: Save SOC to NVS

**Serial Interface** (`src/serial_menu.cpp`): Enhanced display
- Pack summary with SOC, voltage, current, temps
- Detailed per-module statistics
- Protection status
- Commands: balance toggle, SOC reset, detailed view

**Web Dashboard** (`src/web_server.cpp`): Real-time monitoring
- 10 summary metrics (SOC, voltage, current, temps, protection)
- Color-coded cell display
- Module temperatures
- Auto-refresh every 1 second
- API endpoints for programmatic access
