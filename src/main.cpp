#include <Arduino.h>
#include <ELMduino.h>
#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <math.h>

#include "obd_config.h"
#include "simulator_config.h"

namespace {

enum class ConnectionState { CONNECTING, LIVE, STALE, RECONNECTING };
enum class PageId { STATUS, SOOT, DISTANCE, REGEN_COUNT, EGT, COUNT };

struct MetricValue {
  String shown = "--";
  float numeric = 0.0f;
  bool has_numeric = false;
  bool valid = false;
  uint32_t updated_ms = 0;
};

struct RuntimeData {
  ConnectionState conn_state = ConnectionState::CONNECTING;
  bool showed_ok_screen = false;
  bool is_connected = false;
  uint32_t last_connect_attempt_ms = 0;
  uint32_t connect_success_ms = 0;
  uint32_t last_any_data_ms = 0;
  uint32_t last_poll_ms = 0;
  uint32_t last_page_switch_ms = 0;
  uint32_t data_display_until_ms = 0;
  PageId page = PageId::STATUS;

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

struct ScrollState {
  PageId page = PageId::COUNT;
  String text = "";
  int16_t x = 0;
  uint32_t last_step_ms = 0;
} g_scroll;

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

void drawScreen(const String& title,
                const String& value,
                uint32_t background,
                uint32_t text = WHITE) {
  M5.Display.fillScreen(background);
  M5.Display.setTextColor(text, background);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(1);
  M5.Display.drawString(title, M5.Display.width() / 2, 6);
  M5.Display.drawString(value, M5.Display.width() / 2, M5.Display.height() / 2);
}

String buildTickerText(const char* label,
                       const MetricValue& metric,
                       const char* unit,
                       uint8_t decimals) {
  String value = "--";
  if (metric.valid && metric.has_numeric) {
    if (decimals == 0) {
      value = String(static_cast<int32_t>(lroundf(metric.numeric)));
    } else {
      value = String(metric.numeric, decimals);
    }
  }
  String out = String(label) + "=" + value;
  if (strlen(unit) > 0) {
    out += unit;
  }
  return out;
}

String tickerTextForPage(PageId page) {
  switch (page) {
    case PageId::SOOT:
      return buildTickerText("SOOT", g_data.dpf_soot, obd_config::kDpfSoot.unit, 1);
    case PageId::DISTANCE:
      return buildTickerText("DIST", g_data.distance_since_regen,
                             obd_config::kDistanceSinceRegen.unit, 1);
    case PageId::REGEN_COUNT:
      return buildTickerText("CNT", g_data.regen_counter,
                             obd_config::kRegenCounter.unit, 0);
    case PageId::EGT:
      return buildTickerText("EGT", g_data.egt, obd_config::kEgt.unit, 0);
    default:
      break;
  }
  return "STATUS=OK";
}

void drawScrollingMetricPage(PageId page, uint32_t background, uint32_t text = WHITE) {
  const String ticker = tickerTextForPage(page);
  M5.Display.fillScreen(background);
  M5.Display.setTextColor(text, background);
  const uint8_t text_size =
      obd_config::kScrollTextSize == 0 ? 1 : obd_config::kScrollTextSize;
  M5.Display.setTextSize(text_size);

  if (!obd_config::kScrollEnabled) {
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(ticker, M5.Display.width() / 2, M5.Display.height() / 2);
    return;
  }

  M5.Display.setTextDatum(top_left);
  const uint32_t now = millis();
  if (g_scroll.page != page || g_scroll.text != ticker) {
    g_scroll.page = page;
    g_scroll.text = ticker;
    g_scroll.x = M5.Display.width();
    g_scroll.last_step_ms = now;
  }

  if ((now - g_scroll.last_step_ms) >= obd_config::kScrollIntervalMs) {
    g_scroll.last_step_ms = now;
    const int16_t step = obd_config::kScrollStepPx == 0 ? 1 : obd_config::kScrollStepPx;
    g_scroll.x -= step;
  }

  const int16_t text_w = M5.Display.textWidth(g_scroll.text);
  const int16_t reset_threshold =
      -text_w - static_cast<int16_t>(obd_config::kScrollGapPx);
  if (g_scroll.x < reset_threshold) {
    g_scroll.x = M5.Display.width();
  }

  const int16_t y = (M5.Display.height() - (8 * text_size)) / 2;
  M5.Display.drawString(g_scroll.text, g_scroll.x, y);
}

void renderUi() {
  const uint32_t status_color = currentStatusColor();
  if (!isDataDisplayActive()) {
    drawColorOnlyStatus(status_color);
    return;
  }

  if (g_data.is_connected &&
      (millis() - g_data.connect_success_ms) < obd_config::kScreenDurationMs) {
    drawScreen("CONN", "OK", status_color, BLACK);
    return;
  }

  if (!g_data.is_connected) {
    drawScreen("BLE", "RECONNECT", status_color, BLACK);
    return;
  }

  if (isDataStale()) {
    drawScreen("OBD", "DATA LOST", status_color, BLACK);
    return;
  }

  if (isRegenActive()) {
    drawScreen("DPF", "REGEN ON", status_color, WHITE);
    return;
  }

  switch (g_data.page) {
    case PageId::STATUS:
      drawScreen("STATUS", "OK", status_color, BLACK);
      break;
    case PageId::SOOT:
      drawScrollingMetricPage(PageId::SOOT, status_color, BLACK);
      break;
    case PageId::DISTANCE:
      drawScrollingMetricPage(PageId::DISTANCE, status_color, BLACK);
      break;
    case PageId::REGEN_COUNT:
      drawScrollingMetricPage(PageId::REGEN_COUNT, status_color, BLACK);
      break;
    case PageId::EGT:
      drawScrollingMetricPage(PageId::EGT, status_color, BLACK);
      break;
    case PageId::COUNT:
      break;
  }
}

void advancePage() {
  const uint8_t current = static_cast<uint8_t>(g_data.page);
  const uint8_t next =
      (current + 1U) % static_cast<uint8_t>(PageId::COUNT);
  g_data.page = static_cast<PageId>(next);
  g_data.last_page_switch_ms = millis();
  g_scroll.page = PageId::COUNT;
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

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(simulator_config::kSerialBaud);
  Serial.setTimeout(simulator_config::kReadLineTimeoutMs);
  M5.Display.setRotation(0);
  M5.Display.clear();
  M5.Display.setTextFont(1);
  const uint8_t clamped_brightness_percent =
      static_cast<uint8_t>(min<uint8_t>(obd_config::kScreenBrightnessPercent, 100U));
  const uint8_t brightness_level =
      static_cast<uint8_t>((static_cast<uint16_t>(clamped_brightness_percent) * 255U) / 100U);
  M5.Display.setBrightness(brightness_level);

  if constexpr (!simulator_config::kEnableSimulator) {
    g_obd.begin();
  } else {
    g_sim.connected = simulator_config::kStartConnected;
    Serial.println("Simulator mode enabled.");
  }
  wakeDataDisplayFor(obd_config::kDataDisplayBootOnMs);
  g_data.last_page_switch_ms = millis();
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
    if (!isRegenActive() && !isDataStale()) {
      advancePage();
    }
  } else if (isDataDisplayActive() && !isRegenActive() && !isDataStale() &&
             (millis() - g_data.last_page_switch_ms) >=
                 obd_config::kScreenDurationMs) {
    advancePage();
  }

  renderUi();
  delay(20);
}