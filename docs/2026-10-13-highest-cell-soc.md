# Highest-cell voltage SOC — 13 October 2026

User-requested scope: make the BMS at `192.168.2.99` show 100% when its highest
selected cell reaches 4.200 V, with the corresponding 80S design maximum of
336 V. The user clarified that 4201 mV was illustrative; retain the existing
GPIO15 overvoltage trip at **>=4200 mV**. Do not raise normal charge taper limits.

## Implementation

- SOC input changes from minimum to maximum selected cell voltage.
- The last persisted curve point changes from `4100:100` to `4200:100` only.
  The lower historical fitted points are retained, not recalibrated. Their
  previous minimum-cell discharge-fit accuracy does not transfer to this gauge.
- Full indication follows the live maximum immediately, bypassing smoothing
  only at this boundary. Below 4200 mV both filtered and unfiltered results are
  capped at 99%, including integer rounding and a retained full median.
- Below full the 15-sample median and 1 percentage point / 30-second upward
  slew remain. The UI and filtered-voltage discovery label identify maximum-cell
  voltage; existing MQTT entity identifiers and topics are retained.
- Summary diagnostics expose `socVoltageSource`, `socFullCellMv`, and
  `overVoltageTripMv` using the same constants as the actual code.
- Taper, temperature/communications/undervoltage trips, selected CMU masks,
  balancing logic and supervised-override behavior are not changed.

This indicator is **not measured energy SOC** and does not establish usable
battery capacity. Reaching it also reaches the existing voltage protection
threshold (unless the existing supervised voltage override is explicitly active).
The live normal charge stop remains 4.150 V, so taper can stop charging with
this indicator below 100%. No physical overvoltage test is performed.

## Preflight and rollback

`2026-10-13-soc-preflight.json` captures the API settings and live data before
the change. Original curve:

`3200:0,3440:10,3695:15,3735:20,3800:30,3855:40,3895:50,3920:60,3965:70,3990:80,4010:90,4100:100`

Both CMU masks remain A=144 / B=987 (10 selected modules). Charge taper:
4.050/4.140/4.150 V and 13/2 A. Discharge taper: 3.600/3.500/3.300 V and 11/1 A.
Preflight protection was OK, no supervised override; inverter was On Grid,
energy-counted/operational SOC 81.27%, CAN/inverter SOC 81%, Predbat read-only.

The previously available local OTA binary was preserved as
`.pio/rollback/2026-10-13-pre-highest-cell-local.ota.bin`, SHA-256
`be1022b3dbea84ae74e3256433a2731fc51ebd4bafdfbb7fbcce2bbd3c42ee60`.
It is an older local artifact, **not proven identical to the preflight live
firmware**. Preferred software rollback is reversing only this task's source
changes, rebuilding with the existing private configuration, then restoring
the original curve through `POST /api/config`. Preserve unrelated dirty edits.

## Validation / deployment

- Native tests: 46/46 passed, including highest-cell selection, 4199/4200/4201 mV
  SOC, clearing 100% before the next sample and with a full median, and unchanged
  GPIO15/temperature/communications protection tests.
- ESP32-S3 `outlander_bms_ota` build passed with the pinned Python 3.13
  environment and Espressif32 6.5.0. Firmware SHA-256:
  `af251ba8ed731918702ce033cf593b37a4bb86ce5129d375d90a530bd3a3bc47`.
  Saved at `.pio/rollback/2026-10-13-highest-cell-soc.ota.bin`.
- Targeted curve API update succeeded before flashing; design 336 V and
  unchanged taper/masks were read back before the upload.
- Authenticated Arduino OTA to `192.168.2.99` completed successfully at
  **15:16:17 BST**. Source base is `f8e76afcc0b1b4ef7976e6aeaf965d7a9cc14d7d`
  plus existing uncommitted changes and this task's narrow SOC changes.
- Post-reboot summary proves `highest_cell`, full=4200 and trip=4200, saved
  curve ending `4200:100`, maximum design 336 V / minimum 256 V. Fresh CAN was
  2 ms old, all 10 modules present, protection OK, GPIO15 reported HIGH, no
  supervised override. Taper and CMU masks matched the immediate pre-write
  snapshot. Highest/filtered maximum 4072 mV and BMS SOC 93%; lowest 4057 mV.
  Balancing was OFF immediately after reboot but ON again at final readback;
  this task did not send a balancing command. Snapshot in
  `2026-10-13-soc-postflight.json`.
- The deployed web page contains the new highest-cell label. FoxGW received
  design maximum 336 V and raw BMS SOC 93%; its operational/energy SOC remained
  81.25%, CAN SOC 81%, and the inverter readback recovered to 81%. TX errors 0.
  Predbat read-only stayed ON; no estimator reset or FoxGW firmware change.

### Inverter fault observed during this deployment

The inverter entered `Fault` during the OTA (HA transition 15:15:26 BST).
Immediately after reboot its SOC temporarily read 0%; it subsequently returned
to 81%, but the final inverter state remained **Fault / 0 A**. The BMS itself
recovered fresh data and healthy protection. The exposed inverter-state entity
does not provide a fault code, so this is not confirmed as the earlier
Battery Voltage Fault. No automatic reset, safety override, second flash or
control change was attempted. At the time, this deployment was verified at
BMS/MQTT/SOC interfaces, **not as resumed charging**.

Later readback on **16 October 2026 at 02:47 BST** found the inverter **On
Grid**, with the gateway's operational energy SOC at **60.32%** and the CAN,
inverter, and Predbat SOC values each at **60%** after rounding. The fault in
the 13 October snapshot is therefore a historical incident, not a pending
recovery action. This later readback does not prove which action cleared the
fault or physically exercise the breaker chain.
