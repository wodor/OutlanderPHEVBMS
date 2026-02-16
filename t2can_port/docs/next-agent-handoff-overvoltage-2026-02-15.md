# Next Agent Handoff: SIMPBMS Overvoltage Diagnosis (2026-02-15)

## Summary
Battery Emulator overvoltage alert is caused by incoming SIMPBMS frame `0x351` advertising a too-low pack charge cutoff (`49.2V`), while actual pack voltage is `58.4V`.

## Scope
- Outlander sender repo: `/Users/artwielogorski/prv/PowerWall/OutlanderPHEVBMS/t2can_port`
- Sender device: `192.168.2.95` (`/dev/cu.usbmodem2101`)
- Battery Emulator receiver: `192.168.2.53` (`/dev/cu.usbmodem101`)
- Timestamp of investigation: `2026-02-15 17:38:50 GMT`

## Live Evidence Collected
1. Emulator main page showed:
- `Voltage: 58.4 V`
- Event: `BATTERY_OVERVOLTAGE`, data `72`

2. Emulator CAN log (`http://192.168.2.53/canlog`) repeatedly showed:
- `RX0 351 [8] EC 01 2C 01 2C 01 80 01`
- `RX0 356 [8] D3 16 00 00 00 00 00 00`

3. Decoding:
- `0x351 bytes[0..1] = 0x01EC = 492 dV = 49.2V` (max design from SIMPBMS)
- `0x356 bytes[0..1] = 0x16D3 = 5843 cV = 58.43V` (actual pack voltage)

4. Emulator safety logic confirms event condition:
- `voltage_dV > max_design_voltage_dV`
- File: `Battery-Emulator/Software/src/devboard/safety/safety.cpp:102`

5. Emulator SIMPBMS parser overwrites max/min pack design values from `0x351` every update:
- File: `Battery-Emulator/Software/src/battery/SIMPBMS-BATTERY.cpp:33`

## Root Cause
Outlander sender is still transmitting `0x351` cutoff based on 12 cells (`4.1V * 12 = 49.2V`) instead of active pack cell count (16 cells).

## Fix Applied in Outlander Repo
`src/simpbms_can.cpp` now derives series-cell count from live valid CMU voltages and falls back to configured value only if no live data exists.

- Added helper: `getSeriesCellCountForLimits()`
- Updated `0x351` generation to use detected `seriesCells`

Expected `0x351` with 16s at 4.1V/cell:
- charge cutoff approx `65.6V` -> `656 dV` -> hex `0x0290` -> bytes `90 02`

## Additional Clarification
Event data value `72` is not a voltage in volts; it is truncated uint8 payload (`584 dV mod 256 = 72`).

## Verification Sequence for Next Agent
1. Build sender firmware from this repo using the project-local PlatformIO binary:
```bash
cd /Users/artwielogorski/prv/PowerWall/OutlanderPHEVBMS/t2can_port
/Users/artwielogorski/.platformio/penv/bin/platformio run
```

2. Upload sender firmware to LilyGo #1:
```bash
/Users/artwielogorski/.platformio/penv/bin/platformio run -t upload --upload-port /dev/cu.usbmodem2101
```

3. Open serial monitor and confirm no regressions:
```bash
/Users/artwielogorski/.platformio/penv/bin/platformio device monitor --port /dev/cu.usbmodem2101 --baud 115200
```

4. On emulator (`192.168.2.53`), refresh CAN log and verify `RX0 351` bytes[0..1] are no longer `EC 01`.

5. Decode new `0x351` and confirm:
- max design voltage from frame is above current pack voltage (~58.4V)
- `BATTERY_OVERVOLTAGE` no longer appears after event clear/reboot cycle

6. Clear emulator events (`/clearevents`) and watch for reappearance for at least 2-3 minutes.

## Build Environment Note
System `pio` at `/opt/homebrew/bin/pio` uses Python `3.14.3` and fails for `espressif32@6.5.0`.
Use:
- `/Users/artwielogorski/.platformio/penv/bin/platformio` (Python `3.11.7`)

## Current Repo State
Branch: `lilygo-t2-can-platformio-port`
Working tree is dirty with multiple local changes unrelated to this issue; do not hard reset.
