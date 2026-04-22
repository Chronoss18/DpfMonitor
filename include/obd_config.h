#pragma once

#include <Arduino.h>

namespace obd_config {

// BLE target selection: fill name and/or MAC once known.
// Simulator-specific settings live in include/simulator_config.h.
inline constexpr const char* kObdDeviceName = "TODO_FILL_ME_VGATE_NAME";
inline constexpr const char* kObdMacAddress = "";  // Optional: "AA:BB:CC:DD:EE:FF"
inline constexpr const char* kBleServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
inline constexpr const char* kBleTxCharUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
inline constexpr const char* kBleRxCharUuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";

inline constexpr uint32_t kScreenDurationMs = 5000;
inline constexpr uint32_t kPollIntervalMs = 1000;
inline constexpr uint32_t kDataStaleTimeoutMs = 5000;
inline constexpr uint32_t kReconnectDelayMs = 2500;
inline constexpr uint32_t kReconnectWarmupMs = 3000;
inline constexpr float kEgtRedThresholdC = 550.0f;

struct MetricDefinition {
  const char* key;
  const char* command;       // Mode/PID or UDS request string.
  const char* unit;
  const char* decode_note;   // Keep formula notes here until parser is implemented.
  uint8_t expected_bytes;
};

// TODO_FILL_ME: replace placeholders with real OBD commands and formulas.
inline constexpr MetricDefinition kDpfSoot{
    "dpf_soot",
    "TODO_FILL_ME_CMD_DPF_SOOT",
    "mg",
    "TODO_FILL_ME formula example: ((A*256)+B)/10.0",
    2};

inline constexpr MetricDefinition kDistanceSinceRegen{
    "distance_since_regen",
    "TODO_FILL_ME_CMD_DISTANCE_REGEN",
    "km",
    "TODO_FILL_ME formula example: ((A*256)+B)",
    2};

inline constexpr MetricDefinition kRegenCounter{
    "regen_counter",
    "TODO_FILL_ME_CMD_REGEN_COUNTER",
    "",
    "TODO_FILL_ME formula example: A",
    1};

inline constexpr MetricDefinition kEgt{
    "egt",
    "TODO_FILL_ME_CMD_EGT",
    "C",
    "TODO_FILL_ME formula example: ((A*256)+B)/10.0 - 40",
    2};

}  // namespace obd_config
