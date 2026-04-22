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
inline constexpr uint8_t kScreenBrightnessPercent = 60;  // 0..100
inline constexpr bool kScrollEnabled = true;
inline constexpr uint8_t kScrollTextSize = 2;      // Larger text for matrix readability.
inline constexpr uint8_t kScrollStepPx = 1;        // Pixels moved each scroll tick.
inline constexpr uint32_t kScrollIntervalMs = 70;  // Lower = faster movement.
inline constexpr uint8_t kScrollGapPx = 10;        // Gap before text restarts.

// Data-display sleep mode:
// - Keep status color always active
// - Hide scrolling metric text after timeout
// - Button A wakes text display again for a limited window
inline constexpr bool kDataDisplaySleepEnabled = true;
inline constexpr uint32_t kDataDisplayBootOnMs = 180000;       // 3 minutes
inline constexpr uint32_t kDataDisplayWakeOnButtonMs = 300000;  // 5 minutes

// High-contrast traffic-light colors (RGB565) for easier distinction.
inline constexpr uint16_t kColorHealthy = 0x07E0;  // Bright green
inline constexpr uint16_t kColorWarning = 0xFFE0;  // Bright yellow
inline constexpr uint16_t kColorAlert = 0xF800;    // Bright red

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
    "22042C",
    "g", 
    "((A*256)+B)/100.0",
    2};

inline constexpr MetricDefinition kDistanceSinceRegen{
    "distance_since_regen",
    "2204F5",
    "km",
    "((A*256)+B)",
    2};

inline constexpr MetricDefinition kRegenCounter{
    "regen_counter",
    "220421",
    "status",
    "A",
    1};

inline constexpr MetricDefinition kEgt{
    "egt",
    "2203E0",
    "C",
    "((A*256)+B)/10.0 - 40",
    2};

}  // namespace obd_config
