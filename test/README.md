# Unit tests for the T-2CAN Outlander BMS

The native suite protects the deliberately small BMS responsibility:

- `test_bms_data.cpp`: CMU data validity and pack statistics.
- `test_soc_calc.cpp`: voltage-only SOC interpolation.
- `test_protection.cpp`: the active-HIGH `battery_safe_to_use` permissive is
  LOW in application startup and may rise only after complete fresh status,
  cells 1–4 and cells 5–8 data from every selected CMU. It also covers empty
  and partial startup, missing selected CMUs, each required frame group going
  stale, type-4 rejection as a health frame, reset/recovery, high-temperature
  and 4.20 V/2.80 V emergency stops, and the bounded recovery override.
- `test_safety_critical.cpp`: fixed CMU, cell, and temperature array bounds.

Current sensing, contactor-input handling, inverter current control, and ESS
policy are intentionally outside this firmware and have no tests here.

## Coverage limits

The native environment compiles `bms_data`, `soc_calc`, `protection`,
`soc_curve_validation` and `current_taper` with mocked Arduino/Preferences.
It does not compile the CAN handler, main loop, MQTT or web runtime. Protection
tests inject the same per-frame state transitions used by the CAN handler; they
do not prove physical CAN delivery, malformed-frame rejection in the driver,
or GPIO/relay behavior.

Passing native tests therefore does not validate either physical bus, GPIO15
reset/bootloader/power-loss behavior, the SSR/GEYA/V9 breaker chain, network
reconnection or the BMS-to-gateway MQTT path. The target build confirms the
CAN-handler compilation path, but controlled hardware commissioning is still
required.

## Run

```bash
cd OutlanderPHEVBMS
.venv-pio313/bin/python -m platformio test -e native
.venv-pio313/bin/python -m platformio run -e outlander_bms
```

Use `pio test -e outlander_bms --upload-port <port>` only when an explicit
hardware test is intended.
