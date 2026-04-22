# Simulator Testing Guide

This project supports two firmware transport modes:

- Real hardware mode (BLE OBD adapter)
- Simulator mode (USB serial commands from PC)

Simulator mode is intended for bench testing before in-car tests.

## 1) Enable Simulator Mode

Open `include/simulator_config.h` and set:

- `kEnableSimulator = true` for simulator testing
- `kEnableSimulator = false` for normal BLE operation

Build and flash firmware after changing the mode.

## 2) Install PC Console Dependency

Install Python package:

`pip install pyserial`

## 3) Run the Console App

From project root:

`python tools/obd_sim_console.py --port COM5 --baud 115200`

Useful options:

- `--list-ports`
- `--send "connect; egt 600; regen 2"` for one-shot scripts

## 4) Firmware Simulator Commands

- `help`
- `status`
- `connect`
- `disconnect`
- `drop <ms>`
- `regen <value>`
- `egt <value>`
- `soot <value>`
- `dist <value>`
- `all <soot> <dist> <regen> <egt>`

## 5) Suggested Validation Scenarios

### Connection

1. `connect`
2. Expect healthy green state and short center white blink.

### Disconnection

1. `disconnect`
2. Expect warning yellow state with top-right white heartbeat blink.

### Connection Drop + Reconnect

1. `connect`
2. `drop 6000`
3. Wait for stale timeout; expect warning yellow with blinking X pattern.
4. After drop expires or after sending metric commands, data should recover.

### Fake EGT/DPF

1. `connect`
2. `egt 450`
3. `soot 9.5`
4. `dist 120`
5. Verify device remains in healthy/warning behavior as expected.

### Fake Active Regeneration

1. `regen 2`
2. Expect pulsing red alert state.
3. `regen 0`
4. Device returns to normal connected state behavior.

## 6) Return To Production Mode

Before in-car use:

1. Set `kEnableSimulator = false` in `include/simulator_config.h`
2. Rebuild and flash
3. Verify BLE credentials/UUIDs/commands in `include/obd_config.h`
