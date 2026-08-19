# Outlander PHEV CMU Monitor

Standalone firmware for a LilyGO T-2Can that reads Mitsubishi Outlander PHEV
CMUs over CAN. The maintained application is in [`t2can_port/`](t2can_port/).

It monitors CMU cell voltages and temperatures, publishes telemetry through
MQTT, supports CMU balancing, and drives one physical safety permissive:
`BATTERY_SAFE_TO_USE` on GPIO15. The output is normally HIGH; a LOW output
instructs the external breaker interface to open.

This repository is independent of Battery Emulator. It has no Battery Emulator
source or build dependency, sends no inverter protocol, and has no live
inter-controller protocol. A future RX/TX serial connection to the T-Panel is
an intentionally separate interface that will be added only after its pin and
frame contract are defined.

See the [T-2Can project documentation](t2can_port/README.md) for the current
hardware, safety, MQTT, build, and test contract.
