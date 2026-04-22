#include <Arduino.h>
#include <ELMduino.h>
#include <FastLED.h>
#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <math.h>

#include "obd_config.h"
#include "simulator_config.h"

namespace {

enum class ConnectionState { CONNECTING, LIVE, STALE, RECONNECTING };

struct MetricValue {
  String shown = "--";
  float numeric = 0.0f;
  bool has_numeric = false;
  bool valid = false;
  uint32_t updated_ms = 0;
};

struct RuntimeData {
  ConnectionState conn_state = ConnectionState::CONNECTING;
  bool is_connected = false;
  uint32_t last_connect_attempt_ms = 0;
  uint32_t connect_success_ms = 0;
  uint32_t last_any_data_ms = 0;
  uint32_t last_poll_ms = 0;
  uint32_t data_display_until_ms = 0;

  MetricValue dpf_soot;
  MetricValue distance_since_regen;
  MetricValue regen_counter;
  MetricValue egt;
} g_data;

struct SimulatorState {
  bool connected = simulator_config::kStartConnected;
  uint32_t drop_until_ms = 0;
  float dpf_soot = simulator_config::kDefaultDpfSoot;
  float distance_since_regen = simulator_config::kDefaultDistanceSinceRegen;
  float regen_counter = simulator_config::kDefaultRegenCounter;
  float egt = simulator_config::kDefaultEgtC;
} g_sim;

constexpr uint8_t kMatrixWidth = 5;
constexpr uint8_t kMatrixHeight = 5;
constexpr uint8_t kMatrixLedCount = kMatrixWidth * kMatrixHeight;
constexpr uint8_t kMatrixDataPin = 27;
CRGB g_matrix_leds[kMatrixLedCount];

class ObdBleBridge {
 public:
  bool begin() {
    NimBLEDevice::init("");
    return true;
  }

  bool ensureConnected() {
    if (connected()) {
      return true;
    }
    disconnect();

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->start(4, false);
    NimBLEAdvertisedDevice target;
    bool found_target = false;

    for (int i = 0; i < results.getCount(); ++i) {
      NimBLEAdvertisedDevice dev = results.getDevice(i);
      if (matchesTarget(dev)) {
        target = dev;
        found_target = true;
        break;
      }
    }

    if (!found_target) {
      Serial.println("OBD BLE target not found during scan.");
      return false;
    }

    client_ = NimBLEDevice::createClient();
    if (client_ == nullptr || !client_->connect(&target)) {
      Serial.println("BLE client connect failed.");
      disconnect();
      return false;
    }

    NimBLERemoteService* service =
        client_->getService(NimBLEUUID(obd_config::kBleServiceUuid));
    if (service == nullptr) {
      Serial.println("OBD BLE service UUID not found.");
      disconnect();
      return false;
    }

    tx_char_ =
        service->getCharacteristic(NimBLEUUID(obd_config::kBleTxCharUuid));
    rx_char_ =
        service->getCharacteristic(NimBLEUUID(obd_config::kBleRxCharUuid));
    if (tx_char_ == nullptr || rx_char_ == nullptr) {
      Serial.println("OBD BLE RX/TX characteristic not found.");
      disconnect();
      return false;
    }

    if (tx_char_->canNotify()) {
      tx_char_->subscribe(true, notifyCallback, false);
    }

    clearRxBuffer();
    sendAndRead("ATZ");
    sendAndRead("ATE0");
    sendAndRead("ATL0");
    sendAndRead("ATS0");
    sendAndRead("ATH0");
    // Target ECM request header for UDS-style queries (Kia/Hyundai common path).
    sendAndRead("ATSH7E0");
    return true;
  }

  bool connected() const {
    return client_ != nullptr && client_->isConnected() && rx_char_ != nullptr &&
           tx_char_ != nullptr;
  }

  String sendAndRead(const String& cmd, uint32_t timeout_ms = 1000) {
    if (!connected()) {
      return "";
    }
    clearRxBuffer();

    String payload = cmd;
    payload += "\r";
    if (!rx_char_->writeValue(reinterpret_cast<const uint8_t*>(payload.c_str()),
                              payload.length(), false)) {
      return "";
    }

    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
      if (rx_complete_) {
        break;
      }
      delay(5);
    }
    return sanitizeResponse(rx_buffer_, cmd);
  }

  void disconnect() {
    if (client_ != nullptr) {
      if (client_->isConnected()) {
        client_->disconnect();
      }
      NimBLEDevice::deleteClient(client_);
    }
    client_ = nullptr;
    tx_char_ = nullptr;
    rx_char_ = nullptr;
    clearRxBuffer();
  }

 private:
  static inline String rx_buffer_;
  static inline volatile bool rx_complete_ = false;

  static void notifyCallback(NimBLERemoteCharacteristic*,
                             uint8_t* data,
                             size_t len,
                             bool) {
    for (size_t i = 0; i < len; ++i) {
      const char c = static_cast<char>(data[i]);
      rx_buffer_ += c;
      if (c == '>') {
        rx_complete_ = true;
      }
    }
  }

  static void clearRxBuffer() {
    rx_buffer_ = "";
    rx_complete_ = false;
  }

  static bool matchesTarget(NimBLEAdvertisedDevice& dev) {
    const String target_name = String(obd_config::kObdDeviceName);
    const String target_mac = String(obd_config::kObdMacAddress);
    const String dev_name = String(dev.getName().c_str());
    const String dev_mac = String(dev.getAddress().toString().c_str());

    if (target_mac.length() > 0 &&
        target_mac.equalsIgnoreCase(dev_mac.c_str())) {
      return true;
    }
    if (target_name.startsWith("TODO_FILL_ME")) {
      return false;
    }
    return target_name.length() > 0 && target_name == dev_name;
  }

  static String sanitizeResponse(String raw, const String& cmd) {
    raw.replace("\r", " ");
    raw.replace("\n", " ");
    raw.replace(">", " ");
    raw.trim();
    if (raw.startsWith(cmd)) {
      raw = raw.substring(cmd.length());
      raw.trim();
    }
    return raw;
  }

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* tx_char_ = nullptr;
  NimBLERemoteCharacteristic* rx_char_ = nullptr;
} g_obd;

bool isPlaceholder(const char* value) {
  return String(value).startsWith("TODO_FILL_ME");
}

bool parseFloatFromResponse(const String& response, float& out) {
  if (response.length() == 0) {
    return false;
  }
  String token = response;
  const int space_index = token.indexOf(' ');
  if (space_index > 0) {
    token = token.substring(0, space_index);
  }
  token.trim();
  if (token.length() == 0) {
    return false;
  }
  char* end_ptr = nullptr;
  out = strtof(token.c_str(), &end_ptr);
  return end_ptr != token.c_str();
}

void setMetricFromPoll(const obd_config::MetricDefinition& metric,
                       MetricValue& dest) {
  if (isPlaceholder(metric.command)) {
    dest.shown = "--";
    dest.valid = false;
    return;
  }

  const String reply = g_obd.sendAndRead(metric.command);
  float numeric = 0.0f;
  if (!parseFloatFromResponse(reply, numeric)) {
    dest.shown = "--";
    dest.valid = false;
    return;
  }

  dest.numeric = numeric;
  dest.has_numeric = true;
  dest.valid = true;
  dest.updated_ms = millis();
  dest.shown = String(numeric, 1);
  if (strlen(metric.unit) > 0) {
    dest.shown += " ";
    dest.shown += metric.unit;
  }
  g_data.last_any_data_ms = millis();
}

void setMetricFromNumeric(const obd_config::MetricDefinition& metric,
                          MetricValue& dest,
                          float numeric) {
  dest.numeric = numeric;
  dest.has_numeric = true;
  dest.valid = true;
  dest.updated_ms = millis();
  dest.shown = String(numeric, 1);
  if (strlen(metric.unit) > 0) {
    dest.shown += " ";
    dest.shown += metric.unit;
  }
}

bool isRegenActive() {
  return g_data.regen_counter.valid && g_data.regen_counter.has_numeric &&
         g_data.regen_counter.numeric > 0.0f;
}

bool isEgtHigh() {
  return g_data.egt.valid && g_data.egt.has_numeric &&
         g_data.egt.numeric > obd_config::kEgtRedThresholdC;
}

bool isDataStale() {
  if (!g_data.is_connected || g_data.last_any_data_ms == 0) {
    return true;
  }
  return (millis() - g_data.last_any_data_ms) > obd_config::kDataStaleTimeoutMs;
}

void wakeDataDisplayFor(uint32_t duration_ms) {
  g_data.data_display_until_ms = millis() + duration_ms;
}

bool isDataDisplayActive() {
  if (!obd_config::kDataDisplaySleepEnabled) {
    return true;
  }
  return millis() < g_data.data_display_until_ms;
}

uint32_t currentStatusColor() {
  if (isRegenActive()) {
    return obd_config::kColorAlert;
  }
  if (!g_data.is_connected || isDataStale()) {
    return obd_config::kColorWarning;
  }
  return obd_config::kColorHealthy;
}

void drawColorOnlyStatus(uint32_t background) {
  M5.Display.fillScreen(background);
}

bool isMatrixDisplay() {
  return M5.Display.width() <= 8 && M5.Display.height() <= 8;
}

CRGB rgb565ToCRGB(uint16_t color) {
  const uint8_t r = ((color >> 11) & 0x1F) * 255 / 31;
  const uint8_t g = ((color >> 5) & 0x3F) * 255 / 63;
  const uint8_t b = (color & 0x1F) * 255 / 31;
  return CRGB(r, g, b);
}

int16_t matrixIndex(int16_t x, int16_t y) {
  if (x < 0 || y < 0 || x >= kMatrixWidth || y >= kMatrixHeight) {
    return -1;
  }
  if (y % 2 == 0) {
    return y * kMatrixWidth + x;
  }
  return y * kMatrixWidth + (kMatrixWidth - 1 - x);
}

void matrixFill(CRGB color) {
  fill_solid(g_matrix_leds, kMatrixLedCount, color);
}

void matrixDrawPixel(int16_t x, int16_t y, CRGB color) {
  const int16_t idx = matrixIndex(x, y);
  if (idx >= 0) {
    g_matrix_leds[idx] = color;
  }
}

void matrixShow() {
  FastLED.show();
}

void drawIdleHeartbeatIndicator() {
  const bool blink_on = ((millis() / 500UL) % 2UL) == 0UL;
  if (!blink_on) {
    return;
  }

  if (isMatrixDisplay()) {
    matrixDrawPixel(kMatrixWidth - 1, 0, CRGB::White);
    return;
  }

  const int16_t x = M5.Display.width() - 4;
  const int16_t y = 4;
  M5.Display.fillCircle(x, y, 2, WHITE);
}

void renderMatrixUi(uint32_t status_color) {
  matrixFill(rgb565ToCRGB(static_cast<uint16_t>(status_color)));

  if (!isDataDisplayActive()) {
    matrixShow();
    return;
  }

  if (!g_data.is_connected) {
    drawIdleHeartbeatIndicator();
    matrixShow();
    return;
  }

  if (isDataStale()) {
    const bool blink_on = ((millis() / 250UL) % 2UL) == 0UL;
    if (blink_on) {
      for (int16_t i = 0; i < min<int16_t>(kMatrixWidth, kMatrixHeight); ++i) {
        matrixDrawPixel(i, i, CRGB::Black);
        matrixDrawPixel((kMatrixWidth - 1) - i, i, CRGB::Black);
      }
    }
    matrixShow();
    return;
  }

  if (isRegenActive()) {
    const bool pulse_on = ((millis() / 300UL) % 2UL) == 0UL;
    const uint32_t regen_color = pulse_on ? obd_config::kColorAlert : 0x7800;
    matrixFill(rgb565ToCRGB(static_cast<uint16_t>(regen_color)));
    matrixShow();
    return;
  }

  if ((millis() - g_data.connect_success_ms) < obd_config::kScreenDurationMs) {
    const bool blink_on = ((millis() / 300UL) % 2UL) == 0UL;
    if (blink_on) {
      matrixDrawPixel(kMatrixWidth / 2, kMatrixHeight / 2, CRGB::White);
    }
    matrixShow();
    return;
  }
  matrixShow();
}

void renderUi() {
  const uint32_t status_color = currentStatusColor();
  renderMatrixUi(status_color);
}

void pollMetricsIfNeeded() {
  if (!g_data.is_connected || (millis() - g_data.connect_success_ms) <
                                  obd_config::kReconnectWarmupMs) {
    return;
  }
  if ((millis() - g_data.last_poll_ms) < obd_config::kPollIntervalMs) {
    return;
  }
  g_data.last_poll_ms = millis();
  setMetricFromPoll(obd_config::kDpfSoot, g_data.dpf_soot);
  setMetricFromPoll(obd_config::kDistanceSinceRegen, g_data.distance_since_regen);
  setMetricFromPoll(obd_config::kRegenCounter, g_data.regen_counter);
  setMetricFromPoll(obd_config::kEgt, g_data.egt);
}

void maintainConnectionBle() {
  if (g_obd.connected()) {
    if (!g_data.is_connected) {
      g_data.is_connected = true;
      g_data.connect_success_ms = millis();
      g_data.last_any_data_ms = millis();
      g_data.conn_state = ConnectionState::LIVE;
      Serial.println("OBD BLE connected.");
    } else if (isDataStale()) {
      g_data.conn_state = ConnectionState::STALE;
    } else {
      g_data.conn_state = ConnectionState::LIVE;
    }
    return;
  }

  g_data.is_connected = false;
  g_data.conn_state = ConnectionState::RECONNECTING;
  const uint32_t now = millis();
  if ((now - g_data.last_connect_attempt_ms) < obd_config::kReconnectDelayMs) {
    return;
  }

  g_data.last_connect_attempt_ms = now;
  Serial.println("Attempting BLE connect...");
  if (!g_obd.ensureConnected()) {
    Serial.println("BLE connect failed, waiting for retry.");
  }
}

void maintainConnectionSim() {
  if (g_sim.connected) {
    if (!g_data.is_connected) {
      g_data.is_connected = true;
      g_data.connect_success_ms = millis();
      g_data.last_any_data_ms = millis();
      g_data.conn_state = ConnectionState::LIVE;
      Serial.println("Simulator connected.");
    } else if (isDataStale()) {
      g_data.conn_state = ConnectionState::STALE;
    } else {
      g_data.conn_state = ConnectionState::LIVE;
    }
    return;
  }

  g_data.is_connected = false;
  g_data.conn_state = ConnectionState::RECONNECTING;
}

void pollMetricsSimIfNeeded() {
  if (!g_data.is_connected) {
    return;
  }
  if ((millis() - g_data.last_poll_ms) < obd_config::kPollIntervalMs) {
    return;
  }
  g_data.last_poll_ms = millis();

  const bool drop_active = g_sim.drop_until_ms > millis();
  if (drop_active) {
    return;
  }

  setMetricFromNumeric(obd_config::kDpfSoot, g_data.dpf_soot, g_sim.dpf_soot);
  setMetricFromNumeric(obd_config::kDistanceSinceRegen,
                       g_data.distance_since_regen,
                       g_sim.distance_since_regen);
  setMetricFromNumeric(obd_config::kRegenCounter,
                       g_data.regen_counter,
                       g_sim.regen_counter);
  setMetricFromNumeric(obd_config::kEgt, g_data.egt, g_sim.egt);
  g_data.last_any_data_ms = millis();
}

bool parseArgAsFloat(const String& arg, float& value) {
  char* end_ptr = nullptr;
  value = strtof(arg.c_str(), &end_ptr);
  return end_ptr != arg.c_str();
}

void printSimStatus() {
  Serial.printf("mode=sim connected=%d drop_until_ms=%lu soot=%.1f dist=%.1f regen=%.1f egt=%.1f\n",
                g_sim.connected ? 1 : 0,
                static_cast<unsigned long>(g_sim.drop_until_ms),
                g_sim.dpf_soot,
                g_sim.distance_since_regen,
                g_sim.regen_counter,
                g_sim.egt);
}

void handleSimulatorCommand(const String& line_in) {
  String line = line_in;
  line.trim();
  if (line.length() == 0) {
    return;
  }

  const int space_idx = line.indexOf(' ');
  String cmd = space_idx < 0 ? line : line.substring(0, space_idx);
  String args = space_idx < 0 ? "" : line.substring(space_idx + 1);
  cmd.toLowerCase();
  args.trim();

  if (cmd == "help") {
    Serial.println("OK commands: help status connect disconnect drop <ms> regen <n> egt <v> soot <v> dist <v> all <soot> <dist> <regen> <egt>");
    return;
  }
  if (cmd == "status") {
    Serial.print("OK ");
    printSimStatus();
    return;
  }
  if (cmd == "connect") {
    g_sim.connected = true;
    g_sim.drop_until_ms = 0;
    Serial.println("OK connected");
    return;
  }
  if (cmd == "disconnect") {
    g_sim.connected = false;
    g_sim.drop_until_ms = 0;
    Serial.println("OK disconnected");
    return;
  }
  if (cmd == "drop") {
    float ms = 0.0f;
    if (!parseArgAsFloat(args, ms) || ms < 0.0f) {
      Serial.println("ERR usage: drop <ms>");
      return;
    }
    g_sim.drop_until_ms = millis() + static_cast<uint32_t>(ms);
    Serial.printf("OK dropping_for=%lu\n", static_cast<unsigned long>(static_cast<uint32_t>(ms)));
    return;
  }

  float value = 0.0f;
  if (cmd == "regen") {
    if (!parseArgAsFloat(args, value)) {
      Serial.println("ERR usage: regen <n>");
      return;
    }
    g_sim.regen_counter = value;
    Serial.println("OK regen");
    return;
  }
  if (cmd == "egt") {
    if (!parseArgAsFloat(args, value)) {
      Serial.println("ERR usage: egt <value>");
      return;
    }
    g_sim.egt = value;
    Serial.println("OK egt");
    return;
  }
  if (cmd == "soot") {
    if (!parseArgAsFloat(args, value)) {
      Serial.println("ERR usage: soot <value>");
      return;
    }
    g_sim.dpf_soot = value;
    Serial.println("OK soot");
    return;
  }
  if (cmd == "dist") {
    if (!parseArgAsFloat(args, value)) {
      Serial.println("ERR usage: dist <value>");
      return;
    }
    g_sim.distance_since_regen = value;
    Serial.println("OK dist");
    return;
  }
  if (cmd == "all") {
    float soot = 0.0f;
    float dist = 0.0f;
    float regen = 0.0f;
    float egt = 0.0f;
    int p1 = args.indexOf(' ');
    if (p1 < 0) {
      Serial.println("ERR usage: all <soot> <dist> <regen> <egt>");
      return;
    }
    int p2 = args.indexOf(' ', p1 + 1);
    if (p2 < 0) {
      Serial.println("ERR usage: all <soot> <dist> <regen> <egt>");
      return;
    }
    int p3 = args.indexOf(' ', p2 + 1);
    if (p3 < 0) {
      Serial.println("ERR usage: all <soot> <dist> <regen> <egt>");
      return;
    }

    String a1 = args.substring(0, p1);
    String a2 = args.substring(p1 + 1, p2);
    String a3 = args.substring(p2 + 1, p3);
    String a4 = args.substring(p3 + 1);
    a1.trim();
    a2.trim();
    a3.trim();
    a4.trim();
    if (!parseArgAsFloat(a1, soot) || !parseArgAsFloat(a2, dist) ||
        !parseArgAsFloat(a3, regen) || !parseArgAsFloat(a4, egt)) {
      Serial.println("ERR usage: all <soot> <dist> <regen> <egt>");
      return;
    }
    g_sim.dpf_soot = soot;
    g_sim.distance_since_regen = dist;
    g_sim.regen_counter = regen;
    g_sim.egt = egt;
    Serial.println("OK all");
    return;
  }

  Serial.println("ERR unknown command");
}

void readSimulatorSerial() {
  if (!Serial.available()) {
    return;
  }
  String line = Serial.readStringUntil('\n');
  handleSimulatorCommand(line);
}

void runStartupDisplaySelfTest() {
  const CRGB kTestColors[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White, CRGB::Black};
  for (const CRGB color : kTestColors) {
    matrixFill(color);
    matrixShow();
    delay(220);
  }
}

void printStartupDiagnostics(uint8_t brightness_level) {
  Serial.printf("boot board_id=%d display=%dx%d matrix=%d brightness=%u\n",
                static_cast<int>(M5.getBoard()),
                static_cast<int>(M5.Display.width()),
                static_cast<int>(M5.Display.height()),
                isMatrixDisplay() ? 1 : 0,
                static_cast<unsigned>(brightness_level));
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5AtomMatrix;
  cfg.led_brightness = 96;
  M5.begin(cfg);
  FastLED.addLeds<NEOPIXEL, kMatrixDataPin>(g_matrix_leds, kMatrixLedCount);
  FastLED.setBrightness(128);
  matrixFill(CRGB::Black);
  matrixShow();
  Serial.begin(simulator_config::kSerialBaud);
  Serial.setTimeout(simulator_config::kReadLineTimeoutMs);
  const uint8_t clamped_brightness_percent =
      static_cast<uint8_t>(min<uint8_t>(obd_config::kScreenBrightnessPercent, 100U));
  const uint8_t brightness_level =
      static_cast<uint8_t>((static_cast<uint16_t>(clamped_brightness_percent) * 255U) / 100U);
  FastLED.setBrightness(brightness_level);
  printStartupDiagnostics(brightness_level);
  runStartupDisplaySelfTest();

  if constexpr (!simulator_config::kEnableSimulator) {
    g_obd.begin();
  } else {
    g_sim.connected = simulator_config::kStartConnected;
    Serial.println("Simulator mode enabled.");
  }
  wakeDataDisplayFor(obd_config::kDataDisplayBootOnMs);
}

void loop() {
  M5.update();
  if constexpr (simulator_config::kEnableSimulator) {
    readSimulatorSerial();
    maintainConnectionSim();
    pollMetricsSimIfNeeded();
  } else {
    maintainConnectionBle();
    pollMetricsIfNeeded();
  }

  if (M5.BtnA.wasPressed()) {
    wakeDataDisplayFor(obd_config::kDataDisplayWakeOnButtonMs);
  }

  renderUi();
  delay(20);
}