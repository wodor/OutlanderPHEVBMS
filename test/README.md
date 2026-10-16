# Unit tests for the T-2CAN Outlander BMS

The native suite protects the deliberately small BMS responsibility:

- `test_bms_data.cpp`: CMU data validity and pack statistics.
- `test_soc_calc.cpp`: voltage-only SOC interpolation.
- `test_protection.cpp`: the active-HIGH `battery_safe_to_use` permissive,
  10-second CAN/selected-CMU timeout, high-temperature stop, 4.20 V/2.80 V
  emergency cell-voltage stops, and the bounded recovery override. The startup
  test explicitly asserts HIGH; it does not assert a fail-low boot policy.
- `test_safety_critical.cpp`: fixed CMU, cell, and temperature array bounds.

Current sensing, contactor-input handling, inverter current control, and ESS
policy are intentionally outside this firmware and have no tests here.

## Coverage limits

The native environment compiles `bms_data`, `soc_calc`, `protection`,
`soc_curve_validation` and `current_taper` with mocked Arduino/Preferences.
It does not compile the CAN handler, main loop, MQTT or web runtime. Protection
tests inject module state and timestamps directly; they do not prove complete
or fresh status/voltage groups arrive through CAN. A type-4 frame refreshing a
module while temperature or voltage fields freeze is outside the current
CAN-path test coverage.

Passing native tests therefore does not validate either physical bus, GPIO15
power-loss behavior, the SSR/GEYA/V9 breaker chain, network reconnection or the
BMS-to-gateway MQTT path. GPIO15 currently initializes HIGH and communication
faults wait until ten seconds of uptime. Changes to that contract require
explicit behavioral tests and subsequent physical commissioning evidence.

## Run

```bash
cd OutlanderPHEVBMS
.venv-pio313/bin/python -m platformio test -e native
.venv-pio313/bin/python -m platformio run -e outlander_bms
```

Use `pio test -e outlander_bms --upload-port <port>` only when an explicit
hardware test is intended.
