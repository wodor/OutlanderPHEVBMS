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
- [ ] Add WiFi web interface for remote monitoring
- [ ] Port the full V2 features (SOC calculation, charger control, etc.)
