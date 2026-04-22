# Regen_Check

DPF monitoring firmware for M5Stack Atom Matrix (ESP32-PICO-D4) that supports:

- Real vehicle mode via BLE OBD adapter
- Bench simulation mode via USB serial + PC console app

The device uses 5x5 LED matrix status signaling only (no text UI).

## Open-Source Meta

- License: MIT (`LICENSE`)
- Contribution guide: `CONTRIBUTING.md`

## DISCLAIMER: USE AT YOUR OWN RISK

This project is provided "as is", without warranty of any kind, express or implied. The author(s) shall not be held liable for any damages, including but not limited to:

- Damage to your vehicle's electronics, ECU, or engine.
- Voiding of vehicle warranties.
- Traffic accidents or legal issues resulting from the use of this device while driving.

Important: This is a DIY project. Connecting unauthorized hardware to your vehicle's OBD-II port can be dangerous. Always prioritize road safety and do not operate the device while driving.

By using, modifying, or distributing this project, you acknowledge that you do so entirely at your own risk. You are solely responsible for any commands, configuration values, and usage decisions, as well as compliance with local laws and regulations. The author(s) disclaim all liability for direct or indirect damages, vehicle faults, warranty impacts, or incidents arising from use of this software/hardware.

## Built with AI

This project was developed with the assistance of AI (Cursor and LLM). It is a showcase of how AI can help bridge the gap between automotive diagnostics and DIY hardware (M5Stack). While the logic was AI-assisted, it has been manually configured and tested on a real 2019 Kia Sorento 2.2 CRDi.

## 1) Features

- BLE OBD connection with reconnect handling
- LED-only status behavior (green/yellow/red with simple blink patterns)
- Data stale detection
- Regen-dominant alert behavior (when regen counter > 0)
- Idle heartbeat pixel while disconnected
- PC simulation via command-based serial protocol

## 2) Project Structure

- `src/main.cpp` - main firmware logic (BLE + simulator paths + UI)
- `include/obd_config.h` - real OBD/BLE configuration and metric command definitions
- `include/simulator_config.h` - simulator mode configuration (USB serial defaults, mode toggle)
- `tools/obd_sim_console.py` - PC console simulator app
- `docs/OBD_DATA_LOOKUP_CHECKLIST.md` - checklist for final real-car OBD setup
- `docs/SIMULATOR_TESTING.md` - simulator testing quick guide
- `platformio.ini` - PlatformIO board/build configuration

## 3) Requirements

### Firmware / Device

- M5Stack Atom Matrix (ESP32-PICO-D4, configured in `platformio.ini` as `m5stack-atom`)
- USB cable
- PlatformIO CLI (or PlatformIO in VS Code/Cursor)

### Real OBD Mode

- BLE OBD adapter (example: Vgate)
- Correct BLE name/MAC and GATT service/characteristic UUIDs
- Correct OBD commands (Mode/PID or UDS DID) for your vehicle

### Simulator Mode

- Python 3.x
- `pyserial` package

Install dependency:

```bash
pip install pyserial
```

## 4) Build and Upload Firmware

Run these commands from project root.

Build only:

```bash
platformio run
```

Build + upload:

```bash
platformio run -t upload
```

If your shell exposes `pio` instead of `platformio`, use:

```bash
pio run
pio run -t upload
```

List serial devices:

```bash
platformio device list
```

Open serial monitor (default 115200 from `platformio.ini`):

```bash
platformio device monitor -b 115200
```

## 5) Configuration You Must Provide

## 5.1 Real-Time (BLE OBD) Configuration

Edit `include/obd_config.h`.

### BLE adapter identity

- `kObdDeviceName` - BLE advertised name of adapter
- `kObdMacAddress` - optional MAC (recommended for stable targeting)

### BLE GATT UUIDs

- `kBleServiceUuid` - BLE service UUID
- `kBleTxCharUuid` - notify characteristic (adapter -> device)
- `kBleRxCharUuid` - write characteristic (device -> adapter)

Defaults currently assume Nordic UART service.

### OBD metric command definitions

Each metric needs command + decode expectation values:

- `kDpfSoot.command`
- `kDistanceSinceRegen.command`
- `kRegenCounter.command`
- `kEgt.command`

Notes:

- These can be Mode/PID or UDS DID style requests depending on ECU.
- If you refer to them as "DATA_ID", treat that as the UDS DID encoded in the command field.
- Current parser expects numeric-first replies; keep command/adapter formatting aligned with parser behavior.

### Why metrics are still kept in LED-only firmware

Even without text rendering, these metrics are intentionally kept because they are common DPF-related signals:

- `kRegenCounter` (regen activity/state)
- `kEgt` (exhaust gas temperature trend during regen)
- `kDpfSoot` (loading estimate)
- `kDistanceSinceRegen` (interval context)

Current default decision rule is simple and conservative:

- if `regen_counter > 0`, the device enters red alert behavior.

This is only a default strategy. Different cars expose different semantics and ranges, so users may choose a vehicle-specific rule set later, for example:

- `EGT > 610` as regen/alert signal
- `SOOT > 32` as caution threshold
- combined logic (e.g. `regen_counter > 0` OR `EGT > threshold`)

Keeping all four metrics in config allows this customization without changing BLE transport wiring or overall firmware flow.

### Runtime timing and thresholds

- `kScreenDurationMs`
- `kPollIntervalMs`
- `kDataStaleTimeoutMs`
- `kReconnectDelayMs`
- `kReconnectWarmupMs`
- `kEgtRedThresholdC`
- `kScreenBrightnessPercent`
- `kDataDisplaySleepEnabled`
- `kDataDisplayBootOnMs`
- `kDataDisplayWakeOnButtonMs`
- `kColorHealthy`
- `kColorWarning`
- `kColorAlert`

## 5.2 Simulator Configuration

Edit `include/simulator_config.h`.

- `kEnableSimulator`
  - `false`: real BLE mode
  - `true`: simulator serial mode
- `kSerialBaud` - firmware serial baud
- `kReadLineTimeoutMs` - read timeout for command lines
- `kCommandMaxLen` - command length limit (reserved for protocol constraints)
- `kDefaultHostPort` - default PC COM port label (e.g. `COM5`)
- `kDefaultHostBaud` - PC app baud default
- `kStartConnected` - initial simulated connection state
- `kDefaultDpfSoot`
- `kDefaultDistanceSinceRegen`
- `kDefaultRegenCounter`
- `kDefaultEgtC`

## 6) Select Runtime Mode (Real vs Simulator)

Mode is compile-time selected through:

- `include/simulator_config.h` -> `kEnableSimulator`

Workflow:

1. Set mode (`true` for simulator, `false` for real BLE)
2. Build + upload firmware
3. Run appropriate test path

## 7) Real BLE Mode Workflow

1. Set `kEnableSimulator = false`
2. Fill required values in `include/obd_config.h` (name/MAC, UUIDs, metric commands)
3. Build + upload
4. Power adapter + device
5. Check LED matrix + serial logs for connect/reconnect and live behavior

## 8) Simulator Mode Workflow

1. Set `kEnableSimulator = true`
2. Build + upload firmware
3. Start PC app:

```bash
python tools/obd_sim_console.py --list-ports
python tools/obd_sim_console.py --port COM5 --baud 115200
```

4. Send simulator commands from REPL
5. Observe LED matrix behavior

One-shot sequence mode:

```bash
python tools/obd_sim_console.py --port COM5 --baud 115200 --send "connect; egt 600; regen 2"
```

## 9) Simulator Command Reference

Firmware commands accepted over USB serial:

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

Examples:

```text
connect
egt 600
soot 12.4
dist 180
regen 2
drop 6000
status
disconnect
```

Expected behavior:

- `connect` -> healthy status (green), short center white blink on initial connect
- `disconnect` -> warning status (yellow) + top-right white heartbeat blink
- `drop 6000` -> warning status + stale blink pattern (yellow with blinking X)
- `regen > 0` -> pulsing red alert
- high EGT -> contributes to stale/alert state behavior via metric updates

## 10) LED Matrix Behavior (Current Firmware)

The firmware is intentionally LED-only for Atom Matrix.

- Healthy connected state: solid `kColorHealthy` (default bright green)
- Warning state (disconnected or stale): `kColorWarning` (default bright yellow)
- Alert state (regen active): pulsing `kColorAlert` (default bright red)
- Idle live heartbeat: blinking white top-right pixel while disconnected
- Recent connect indicator: blinking white center pixel during connect splash window
- Stale indicator: blinking X pattern overlay

## 11) Troubleshooting

### Upload issues

- Confirm correct COM port (`platformio device list`)
- Close other apps using the same COM port
- If needed, set `upload_port` in `platformio.ini`

### No simulator response

- Verify `kEnableSimulator = true`
- Verify baud matches (`kSerialBaud` and `--baud`)
- Ensure newline-delimited commands are being sent

### Real BLE not connecting

- Verify adapter name/MAC
- Verify UUIDs (`kBleServiceUuid`, `kBleTxCharUuid`, `kBleRxCharUuid`)
- Ensure adapter is advertising and not connected to another host

### Metrics not affecting expected colors

- Placeholder commands still present in `include/obd_config.h`
- Adapter response format not parseable as numeric-first token

## 12) Safety Note

Use simulator mode for bench validation first. Before in-car use:

- set `kEnableSimulator = false`
- verify all BLE/OBD configuration values are correct
- confirm expected warning behavior under real data
