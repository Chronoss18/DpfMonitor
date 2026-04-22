#pragma once

#include <Arduino.h>

namespace simulator_config {

// Compile-time transport mode.
// false: use real BLE OBD adapter
// true: use USB serial simulator commands
inline constexpr bool kEnableSimulator = true;

// Firmware serial channel settings (device side).
inline constexpr uint32_t kSerialBaud = 115200;
inline constexpr uint32_t kReadLineTimeoutMs = 50;
inline constexpr size_t kCommandMaxLen = 96;

// Host-side defaults for the simulator console app.
inline constexpr const char* kDefaultHostPort = "COM5";
inline constexpr uint32_t kDefaultHostBaud = 115200;

// Simulator startup defaults.
inline constexpr bool kStartConnected = false;
inline constexpr float kDefaultDpfSoot = 0.0f;
inline constexpr float kDefaultDistanceSinceRegen = 0.0f;
inline constexpr float kDefaultRegenCounter = 0.0f;
inline constexpr float kDefaultEgtC = 150.0f;

}  // namespace simulator_config
