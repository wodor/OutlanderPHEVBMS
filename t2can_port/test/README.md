# Unit tests for the T-2CAN Outlander BMS

The native suite protects the deliberately small BMS responsibility:

- `test_bms_data.cpp`: CMU data validity and pack statistics.
- `test_soc_calc.cpp`: voltage-only SOC interpolation.
- `test_protection.cpp`: the active-HIGH `battery_safe_to_use` permissive,
  10-second CAN/selected-CMU timeout, high-temperature stop, 4.20 V/2.80 V
  emergency cell-voltage stops, and the bounded recovery override.
- `test_safety_critical.cpp`: fixed CMU, cell, and temperature array bounds.

Current sensing, contactor-input handling, and ESS policy are intentionally
outside this firmware and have no tests here; the T-Panel Battery Emulator owns
that policy.

## Run

```bash
cd OutlanderPHEVBMS/t2can_port
.venv-pio313/bin/pio test -e native
.venv-pio313/bin/pio run -e outlander_bms
```

Use `pio test -e outlander_bms --upload-port <port>` only when an explicit
hardware test is intended.
