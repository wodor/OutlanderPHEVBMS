# Outlander PHEV BMS Reader for T-2Can

This is standalone PlatformIO firmware for the LilyGo T-2Can board (ESP32-S3).
It reads cell voltages and temperatures from Mitsubishi Outlander PHEV battery
modules over CAN and provides monitoring, balancing, MQTT telemetry, and a
physical safety permissive for DIY home energy storage.

## Current runtime contract (reviewed 16 October 2026)

The firmware reads CMU CAN data, controls balancing, publishes MQTT telemetry,
and drives GPIO15 `BATTERY_SAFE_TO_USE` as an active-HIGH permissive. The
external breaker interface must open when this output goes LOW.

**Startup is fail-low in application software:** `setup()` and
`protectionInit()` drive GPIO15 LOW. `protectionCheck()` can raise it only
after every selected CMU has a complete, fresh and valid sample: one status /
three-temperature frame and both four-cell voltage frames. There is no
startup grace period. Missing, partial, malformed or stale required data is a
communication fault from the first protection check. This does not prove the
ESP32 reset, bootloader or unpowered-pin electrical state; those still need
measured commissioning evidence.

Freshness is per required frame group. Type 1 (status/temperatures), type 2
(cells 1–4) and type 3 (cells 5–8) each have their own timestamp; type 4 and
malformed frames are not liveness evidence. HIGH is the firmware's permissive
state, not proof that the external breaker chain has passed a physical fault
test. Temperature and communication faults cannot be overridden; the bounded
supervised voltage-recovery override affects only emergency voltage trips.

This firmware does not read amperage or perform coulomb counting. It calculates
cell-voltage-derived charge/discharge current ceilings for PowerWall-Gateway,
but does not control inverter work modes, run ESS contactor sequencing, or emit
inverter-side SIMPBMS/FoxESS frames. It has no Battery-Emulator application
dependency. MQTT is its current external interface. A future dedicated framed
serial sender is not implemented. SOC here is voltage-derived diagnostic
telemetry. FoxGW uses its separate calibrated energy ledger for operational
SOC and does not fall back to this voltage gauge if that ledger is unavailable.
The currently identified BMS is at `http://192.168.2.99/`; verify identity
before device operations.

### Current highest-cell voltage gauge

The gauge uses the highest selected cell and the configured piecewise curve.
Its live full reference is 4.200 V: reaching that threshold displays 100%
immediately, and falling below it clears 100% immediately. Below full, the
15-second median and upward-slew filter remain. This gauge does not measure
stored energy or establish usable capacity.

With 80 selected cells, the 3.20/4.20-V design endpoints advertise 256/336 V.
The normal charge taper stops at 4.150 V, so charging can stop before this
gauge reaches 100%. Emergency protection independently trips at >=4200 mV.
A 1-mV display increment is not proof of 1-mV measurement accuracy.
The current masks select ten CMUs across both buses; an additional observed,
unselected module is excluded from pack statistics and protection.

Dated deployment, readback and rollback evidence is retained in the
[historical SOC record](historical/2026-10-13-highest-cell-soc.md). It does not
prove that the installed binary matches this checkout byte-for-byte.

Balancing-cell count MQTT telemetry is deliberately published at a 10-second interval. Per-CMU maximum temperature topics and the overall pack maximum temperature topic are published with the other BMS telemetry.

## Features

### Current Capabilities

- **CAN Bus Communication**: Both CAN-A (MCP2515) and CAN-B (TWAI) are dedicated CMU buses, reading up to 20 Outlander PHEV CMUs across isolated segments. MQTT is the current external transport; this firmware does not emit Battery Emulator or inverter CAN traffic.
- **Cell Voltage Monitoring**: Tracks 8 cells per configured CMU
- **Temperature Monitoring**: 3 temperature sensors per CMU
- **Cell Balancing Control**: Uses the eighth-lowest valid cell voltage, preserving the lowest seven cells from discharge
- **Web Dashboard**: Real-time monitoring via WiFi
- **Serial/Web Command Parity**: Every serial command is available through `POST /api/command`; `GET /api/help` returns the mapping
- **Physical permissive**: GPIO15 `BATTERY_SAFE_TO_USE`, held LOW in application startup until complete selected-CMU health data is present
- **Temperature telemetry**: Per-CMU and overall maximum temperature MQTT topics
- **CAN diagnostics**: MCP2515/TWAI status, error counters, and message counters in serial and web output

## Hardware Requirements

- **LilyGO T-2Can board** (ESP32-S3)
- **Outlander PHEV battery modules** with CMUs
- **CAN bus connection** to the battery modules

## Setup Instructions

### 1. Install PlatformIO

Follow the [T-2Can PlatformIO setup](https://github.com/Xinyuan-LilyGO/T-2Can?tab=readme-ov-file#platformio)

### 2. Configure WiFi

```bash
cp .config.h.template .config.h
# Edit .config.h and set your WiFi credentials
```

### 3. Build and Upload

```bash
pio run -t upload
```

### 4. Access the Dashboard

Once connected to WiFi, the serial console will display the IP address. Navigate to `http://<ip-address>/` in your browser to see the web dashboard.

### OTA Updates

The first OTA-capable firmware must be installed over USB. Later updates can be uploaded over WiFi:

```bash
pio run -e outlander_bms_ota -t upload
```

The OTA environment defaults to the currently identified BMS at
`192.168.2.99`; confirm device identity before uploading and use
`--upload-port <ip-or-hostname>` if its address changes. OTA is authenticated
with `OTA_PASSWORD` from the ignored `.config.h`, falling back to the private
`WIFI_PASSWORD` when no separate OTA password is defined. The OTA listener is
restored automatically after a WiFi reconnect.

## Web Dashboard

The web interface provides real-time monitoring of:
- State of Charge (SOC) percentage
- Pack voltage
- Pack-wide cell-voltage delta
- Per-module voltage totals for each complete eight-cell CMU
- Overall maximum temperature and per-module maximum temperatures
- Individual cell voltages (color-coded)
- Temperature readings
- Cell balancing status
- Protection system status
- CAN bus connectivity
- GPIO15 battery-safe-to-use state and supervised-override timer
- Collapsible Bus A/Bus B sections when no CMUs are selected
- Confirmed reboot control with visible restart feedback
- Full report, detailed statistics, CAN diagnostics, debug toggle, balancing toggle, supervised voltage override, and expected-CMU mask controls

![web server](web_server.png)

## Serial commands and web equivalents

Connect via USB serial (115200 baud) and use these commands:

- `b` - Toggle cell balancing on/off
- `d` - Toggle debug mode (shows raw CAN frames)
- `r` - Show the full BMS report
- `o` - Enable the ten-minute supervised voltage-recovery override (refused when CAN/temperature is unsafe)
- `O` - Cancel the supervised override
- `s` - Show detailed module statistics
- `c` - Show MCP2515/TWAI CAN diagnostics
- `A` - Set the expected Bus A CMU mask, entered as hexadecimal
- `B` - Set the expected Bus B CMU mask, entered as hexadecimal
- `h` or `?` - Show help

The dashboard exposes these through `POST /api/command` with form field `command`. For `A` and `B`, add `mask=HEX`; for example `command=B&mask=3FF`. `GET /api/help` returns the same mapping. The existing `/api/summary`, `/api/module/N`, `/api/balancing`, `/api/config`, and `/api/reboot` endpoints remain available.

The current taper is persisted in NVS and can be replaced atomically without a
firmware rebuild. Values are charge full/reduced/stop voltage, charge
full/reduced current, then the equivalent discharge values, in volts and amps:

```bash
curl -X POST http://outlander-bms.local/api/config \
  --data-urlencode 'currentTaper=4.050,4.140,4.150,13.0,2.0,3.600,3.500,3.300,11.0,1.0'
```

`GET /api/summary` reports both the persisted curve and the live charge and
discharge limits. Invalid, reversed, out-of-range, or partial curves are
rejected without changing the active settings.

Home Assistant MQTT Discovery also creates ten configuration `number` entities
on the Outlander BMS device, one for each taper voltage/current parameter, plus
a `switch` named **Balancing**. Number commands are applied to a copy of the
complete curve and persisted only when the resulting curve remains valid; a
rejected command is answered with the unchanged authoritative value. The
balancing switch controls the existing runtime flag and deliberately starts
OFF after a reboot or OTA update. Command messages are not retained, preventing
an old balancing request from being replayed when the BMS reconnects.

The controls use `outlander_bms/control/current_taper/<parameter>/set` and
`outlander_bms/control/balancing/set`; their retained state topics end in
`/state`. The firmware subscribes to `outlander_bms/control/#` after each MQTT
connection.

The BMS voltage SOC gauge uses a configurable monotonic piecewise curve. Each
point is `cell_millivolts:percent`; voltages must increase and percentages may
increase or stay flat. It currently uses the highest selected cell. The
deployed curve is:

```bash
curl -X POST http://outlander-bms.local/api/config \
  --data-urlencode 'socCurvePoints=3200:0,3440:10,3695:15,3735:20,3800:30,3855:40,3895:50,3920:60,3965:70,3990:80,4010:90,4200:100'
```

The historical `socCurve=lowMv,lowPercent,highMv,highPercent` form remains
accepted and installs a two-point curve. MQTT and `/api/summary` expose the
unfiltered mapped SOC, the 15-second median cell voltage, and the active curve
for diagnosis. Brief downward voltage sag is median-filtered; upward recovery
is limited to one percentage point per 30 seconds. Raw cell extrema still own
protection and current taper.

## Current configuration reference

The active settings are defined in `src/bms_data.h` and persisted in NVS. They
include the high-temperature trip, voltage-derived SOC curve, expected-CMU
masks, and the normal-use current taper. Emergency cell-voltage stops and the
GPIO15 permissive remain fixed in `src/protection.cpp`.

Charge current is governed only by the highest selected cell. The 16 October
live settings allow up to 13 A through 4.050 V, taper to 2 A at 4.140 V, and
stop at 4.150 V. Discharge is governed only by the lowest selected cell: up to
11 A at and above 3.600 V, taper to 1 A at 3.500 V, and stop at 3.300 V.
The BMS publishes the two
limits atomically on `outlander_bms/pack/current_limits`; PowerWall-Gateway
turns them into the FoxESS CAN current ceilings.

On startup, the firmware migrates the historical 4.00 V voltage-SOC endpoint
to 4.05 V. An existing 4.05 V endpoint remains unchanged. The 4.20 V emergency
cell-voltage stop remains a separate absolute safety ceiling and is never a
normal charging target. The current persisted configuration uses 4.20 V as its
SOC/design reference only; normal charging is still stopped by the independent
taper below that reference.

## Project Structure

```
./
├── src/
│   ├── main.cpp           # Entry point, main loop
│   ├── config.h           # Hardware pins, constants
│   ├── bms_data.h/cpp     # Data structures, global state
│   ├── can_handler.h/cpp  # CAN bus communication
│   ├── serial_menu.h/cpp  # Serial console interface
│   ├── wifi_handler.h/cpp # WiFi management
│   ├── web_server.h/cpp   # Web dashboard
│   ├── soc_calc.h/cpp     # Voltage-derived SOC
│   ├── current_taper.h/cpp # Cell-extrema current ceilings
│   └── protection.h/cpp   # GPIO15 safety permissive
├── test/
│   ├── test_main.cpp      # Test entry point
│   ├── test_bms_data.cpp  # BMS data tests
│   ├── test_soc_calc.cpp  # SOC calculation tests
│   ├── test_protection.cpp # Protection system tests
│   ├── test_safety_critical.cpp # Safety critical tests
│   └── mocks/             # Mock Arduino/Preferences for native tests
├── platformio.ini         # Build configuration
├── README.md              # This file
└── AGENTS.md              # Development notes

```

## Status

✅ **Working**: CAN communication, voltage/temp reading, MQTT telemetry, web dashboard, serial interface, and web command bridge
✅ **Local verification**: 50/50 native tests and the ESP32-S3 build passed for the M01 source candidate on 16 October 2026
⚠️ **Supervised**: GPIO15 application behavior needs end-to-end breaker-trip testing; reset/power-loss behavior is not established
⚠️ **Deployment provenance**: API readback confirms highest-cell gauge behavior; the installed binary is not proven byte-for-byte from this checkout
⚠️ **Repository metadata**: The GitHub repository is public and still marked as a fork of `Tom-evnut/OutlanderPHEVBMS`, although the local upstream remote and inherited firmware were removed
❌ **Intentionally absent**: amperage input, coulomb counting, ESS contactor control, and inverter-side SIMPBMS transmitter

## Remaining work

1. Physically prove GPIO15 LOW through the installed SSR/GEYA/V9 breaker chain for every fault and power-loss case.
2. Verify loss-of-either-bus, malformed-frame and frozen frame-group behavior with all selected modules, then record GPIO15 and breaker-chain observations.
3. Implement and bench-test the framed serial sender after the T-CAN485 receiver contract is ready.
4. Record the exact source commit and firmware hash at the next OTA deployment.

## More Information

- See `AGENTS.md` for detailed development notes, hardware specifications, and CAN protocol documentation.
- See [test coverage](test/README.md) for what host tests establish.
- See [historical evidence and proposals](historical/README.md) for dated deployment records and the unimplemented remote logging design.

## Hardware

- LilyGO T-2Can
