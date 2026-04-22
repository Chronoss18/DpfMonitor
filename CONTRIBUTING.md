# Contributing

Thanks for your interest in contributing to Regen_Check.

This project is a DPF monitoring firmware for M5Stack Atom with:

- Real BLE OBD mode
- USB serial simulator mode for safe bench testing

## Ground Rules

- Keep changes focused and small where possible.
- Do not commit personal/vehicle-identifying data (MAC tied to your device, VIN-related logs, private traces).
- Preserve simulator workflow when changing runtime logic.
- Keep configuration defaults generic and safe.

## Development Setup

1. Install PlatformIO CLI.
2. Build firmware from project root:

```bash
platformio run
```

3. Upload firmware:

```bash
platformio run -t upload
```

4. For simulator testing:

```bash
pip install pyserial
python tools/obd_sim_console.py --list-ports
python tools/obd_sim_console.py --port COM5 --baud 115200
```

## Configuration Files

- Real OBD/BLE settings: `include/obd_config.h`
- Simulator settings and mode switch: `include/simulator_config.h`

When testing real OBD functionality, ensure `kEnableSimulator = false`.
When testing simulator workflow, ensure `kEnableSimulator = true`.

## What To Include In PRs

- Clear description of what changed and why.
- Testing notes:
  - build success
  - real mode or simulator mode tested
  - key commands/scenarios used
- Any config changes required by reviewers to reproduce.

## PR Checklist

- [ ] Code builds with PlatformIO
- [ ] No secrets or personal data committed
- [ ] README/docs updated if behavior changed
- [ ] Simulator command behavior still works (if affected)
- [ ] Real BLE flow not unintentionally broken (if affected)

## Suggested First Contributions

- Improve OBD decode robustness
- Add better parser validation for simulator commands
- Improve docs for vehicle-specific setup
- Add automated host-side simulator scenario scripts

## Safety and Responsibility

This project is provided for informational/experimental use.
Contributors should avoid suggesting unsafe in-vehicle usage patterns and should encourage bench testing first.
