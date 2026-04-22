# OBD Data Lookup Checklist

Use this file as your reminder for what must be looked up before final tuning.

## 1) BLE Adapter Identity (Vgate)

- [ ] Find BLE advertised name for the adapter.
  - Put into `include/obd_config.h` -> `kObdDeviceName`
- [ ] (Optional but recommended) find BLE MAC address.
  - Put into `include/obd_config.h` -> `kObdMacAddress`

## 2) BLE GATT UUIDs (only if your Vgate is not Nordic UART profile)

Defaults currently assume Nordic UART Service UUIDs.

- [ ] Service UUID
  - Put into `include/obd_config.h` -> `kBleServiceUuid`
- [ ] Notify characteristic (adapter TX to device)
  - Put into `include/obd_config.h` -> `kBleTxCharUuid`
- [ ] Write characteristic (device TX to adapter)
  - Put into `include/obd_config.h` -> `kBleRxCharUuid`

## 3) OBD Commands and Decoding

Look up and confirm command + formula + unit for each metric.

### DPF soot
- [ ] OBD command (Mode/PID or UDS DID)
  - `include/obd_config.h` -> `kDpfSoot.command`
- [ ] Decode formula
  - `include/obd_config.h` -> `kDpfSoot.decode_note`
- [ ] Expected byte count
  - `include/obd_config.h` -> `kDpfSoot.expected_bytes`

### Distance since last regen
- [ ] OBD command
  - `include/obd_config.h` -> `kDistanceSinceRegen.command`
- [ ] Decode formula
  - `include/obd_config.h` -> `kDistanceSinceRegen.decode_note`
- [ ] Expected byte count
  - `include/obd_config.h` -> `kDistanceSinceRegen.expected_bytes`

### DPF regeneration counter (authoritative regen signal)
- [ ] OBD command
  - `include/obd_config.h` -> `kRegenCounter.command`
- [ ] Decode formula
  - `include/obd_config.h` -> `kRegenCounter.decode_note`
- [ ] Expected byte count
  - `include/obd_config.h` -> `kRegenCounter.expected_bytes`

### EGT
- [ ] OBD command
  - `include/obd_config.h` -> `kEgt.command`
- [ ] Decode formula
  - `include/obd_config.h` -> `kEgt.decode_note`
- [ ] Expected byte count
  - `include/obd_config.h` -> `kEgt.expected_bytes`

## 4) Safety / Reliability Tuning

- [ ] Confirm EGT threshold for warning red background
  - `include/obd_config.h` -> `kEgtRedThresholdC` (default `550.0`)
- [ ] Confirm stale timeout for lost data caution
  - `include/obd_config.h` -> `kDataStaleTimeoutMs`
- [ ] Confirm reconnect delay
  - `include/obd_config.h` -> `kReconnectDelayMs`

## 5) Suggested Tools for Lookup

- Car Scanner ELM OBD2 app (custom PID definitions and raw frame verification)
- Torque Pro (custom PID setup and logging)
- Any BLE explorer app (nRF Connect) for service/characteristic UUIDs

## 6) Final Quick Validation Before Daily Use

- [ ] Device connects and shows `OK` on green for 5 seconds
- [ ] When `regen_counter > 0`, screen shows red `REGEN ON`
- [ ] If no fresh data for timeout, `DATA LOST` appears
- [ ] Button A changes page when not in dominant regen/stale states
- [ ] EGT page and warning color react with live data
