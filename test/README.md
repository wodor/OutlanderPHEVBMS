# Unit tests for the T-2CAN Outlander BMS

The native suite protects the deliberately small BMS responsibility:

- `test_bms_data.cpp`: CMU data validity and pack statistics.
- `test_soc_calc.cpp`: voltage-only SOC interpolation.
- `test_protection.cpp`: the active-HIGH `battery_safe_to_use` permissive,
  10-second CAN/selected-CMU timeout, high-temperature stop, 4.20 V/2.80 V
  emergency cell-voltage stops, and the bounded recovery override.
- `test_safety_critical.cpp`: fixed CMU, cell, and temperature array bounds.

Current sensing, contactor-input handling, inverter current control, and ESS
policy are intentionally outside this firmware and have no tests here.

## Run

```bash
cd OutlanderPHEVBMS
.venv-pio313/bin/python -m platformio test -e native
.venv-pio313/bin/python -m platformio run -e outlander_bms
```

Use `pio test -e outlander_bms --upload-port <port>` only when an explicit
hardware test is intended.
