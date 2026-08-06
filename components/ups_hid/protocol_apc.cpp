#include "protocol_apc.h"
#include "ups_hid.h"
#include "constants_ups.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cinttypes>
#include <map>
#include <vector>

namespace esphome {
namespace ups_hid {

static const char *const APC_HID_TAG = "apc_hid";

// HID report IDs used by APC devices
namespace report_id {
static constexpr uint8_t BATTERY_STATUS = 0x01;
static constexpr uint8_t POWER_STATUS = 0x02;
static constexpr uint8_t TEST_STATUS = 0x03;
static constexpr uint8_t CONFIG_STATUS = 0x04;
static constexpr uint8_t DEVICE_STATUS = 0x05;
}  // namespace report_id

// APC HID vendor IDs
namespace vendor_id {
static constexpr uint16_t APC = 0x051D;
}  // namespace vendor_id

namespace {

// Helper structure to represent a HID report
struct HidReport {
  uint8_t report_id;
  std::vector<uint8_t> data;
};

// Helper functions for parsing HID data
static uint16_t read_u16(const std::vector<uint8_t> &data, size_t offset) {
  if (offset + 1 >= data.size())
    return 0;
  return static_cast<uint16_t>(data[offset]) |
         (static_cast<uint16_t>(data[offset + 1]) << 8);
}

static int16_t read_s16(const std::vector<uint8_t> &data, size_t offset) {
  return static_cast<int16_t>(read_u16(data, offset));
}

static uint32_t read_u32(const std::vector<uint8_t> &data, size_t offset) {
  if (offset + 3 >= data.size())
    return 0;
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

static float read_float(const std::vector<uint8_t> &data, size_t offset,
                        float scale = 1.0f) {
  return static_cast<float>(read_s16(data, offset)) * scale;
}

// Convert APC date (YYYYMMDD) from integer to string
static std::string convert_apc_date(uint32_t apc_date) {
  if (apc_date == 0)
    return "";

  uint32_t year = apc_date / 10000;
  uint32_t month = (apc_date / 100) % 100;
  uint32_t day = apc_date % 100;

  char buffer[11];  // YYYY-MM-DD
  snprintf(buffer, sizeof(buffer), "%04" PRIu32 "-%02" PRIu32 "-%02" PRIu32,
           year, month, day);
  return std::string(buffer);
}

// Helper class to parse APC HID reports
class ApcReportParser {
 public:
  static void parse_battery_report(const HidReport &report, UpsData &data);
  static void parse_power_report(const HidReport &report, UpsData &data);
  static void parse_test_report(const HidReport &report, UpsData &data);
  static void parse_config_report(const HidReport &report, UpsData &data);
  static void parse_device_report(const HidReport &report, UpsData &data);
};

void ApcReportParser::parse_battery_report(const HidReport &report, UpsData &data) {
  if (report.data.size() < 12) {
    ESP_LOGW(APC_HID_TAG, "Battery report too short: %zu bytes",
             report.data.size());
    return;
  }

  uint8_t battery_level = report.data[0];
  data.battery.level = static_cast<float>(battery_level);
  data.battery.voltage = read_float(report.data, 1, 0.1f);
  data.battery.voltage_nominal = read_float(report.data, 3, 0.1f);

  uint32_t runtime_raw = read_u32(report.data, 5);
  if (runtime_raw > 0) {
    data.battery.runtime_minutes = runtime_raw / 60.0f;
  }

  ESP_LOGD(APC_HID_TAG,
           "Battery report: level=%u%%, voltage=%.1f V, nominal=%.1f V, runtime=%" PRIu32 " s",
           battery_level, data.battery.voltage, data.battery.voltage_nominal,
           runtime_raw);

  data.battery.valid = true;
}

void ApcReportParser::parse_power_report(const HidReport &report, UpsData &data) {
  if (report.data.size() < 12) {
    ESP_LOGW(APC_HID_TAG, "Power report too short: %zu bytes", report.data.size());
    return;
  }

  data.power.input_voltage = read_float(report.data, 0, 0.1f);
  data.power.output_voltage = read_float(report.data, 2, 0.1f);
  data.power.input_voltage_nominal = read_float(report.data, 4, 0.1f);
  data.power.load_percent = read_float(report.data, 6, 1.0f);
  data.power.frequency = read_float(report.data, 8, 0.1f);

  ESP_LOGD(APC_HID_TAG,
           "Power report: Vin=%.1f V, Vout=%.1f V, nominal=%.1f V, load=%.1f%%, freq=%.1f Hz",
           data.power.input_voltage, data.power.output_voltage,
           data.power.input_voltage_nominal, data.power.load_percent,
           data.power.frequency);

  data.power.set_input_voltage_valid(data.power.input_voltage > 0.0f);
}

void ApcReportParser::parse_test_report(const HidReport &report, UpsData &data) {
  if (report.data.size() < 4) {
    ESP_LOGW(APC_HID_TAG, "Test report too short: %zu bytes", report.data.size());
    return;
  }

  uint8_t test_status = report.data[0];
  uint8_t test_result = report.data[1];

  static const std::map<uint8_t, const char *> status_map = {
      {0x00, "No test in progress"},
      {0x01, "Quick battery test in progress"},
      {0x02, "Deep battery test in progress"}};

  static const std::map<uint8_t, const char *> result_map = {
      {0x00, "No test result"},
      {0x01, "Test passed"},
      {0x02, "Test failed"}};

  auto status_it = status_map.find(test_status);
  auto result_it = result_map.find(test_result);

  data.test.ups_test_result.clear();
  if (status_it != status_map.end()) {
    data.test.ups_test_result += status_it->second;
  }
  if (result_it != result_map.end()) {
    if (!data.test.ups_test_result.empty())
      data.test.ups_test_result += "; ";
    data.test.ups_test_result += result_it->second;
  }

  data.test.timer_shutdown = static_cast<int>(report.data[2]);
  data.test.timer_start = static_cast<int>(report.data[3]);

  ESP_LOGD(APC_HID_TAG,
           "Test report: status=0x%02X, result=0x%02X, shutdown=%d s, start=%d s",
           test_status, test_result, data.test.timer_shutdown,
           data.test.timer_start);
}

void ApcReportParser::parse_config_report(const HidReport &report, UpsData &data) {
  if (report.data.size() < 8) {
    ESP_LOGW(APC_HID_TAG, "Config report too short: %zu bytes", report.data.size());
    return;
  }

  data.config.delay_shutdown = static_cast<int>(report.data[0]);
  data.config.delay_start = static_cast<int>(report.data[1]);
  data.config.delay_reboot = static_cast<int>(report.data[2]);

  uint8_t beeper_status = report.data[3];
  static const std::map<uint8_t, const char *> beeper_map = {
      {0x00, "Disabled"}, {0x01, "Enabled"}, {0x02, "Muted"}};
  auto it = beeper_map.find(beeper_status);
  if (it != beeper_map.end()) {
    data.config.beeper_status = it->second;
  }

  ESP_LOGD(APC_HID_TAG,
           "Config report: shutdown=%d s, start=%d s, reboot=%d s, beeper=%s",
           data.config.delay_shutdown, data.config.delay_start,
           data.config.delay_reboot, data.config.beeper_status.c_str());
}

void ApcReportParser::parse_device_report(const HidReport &report, UpsData &data) {
  if (report.data.size() < 16) {
    ESP_LOGW(APC_HID_TAG, "Device report too short: %zu bytes", report.data.size());
    return;
  }

  // For now, only parse firmware version and nominal power
  uint16_t realpower_nominal = read_u16(report.data, 0);
  data.power.realpower_nominal = static_cast<float>(realpower_nominal);

  uint32_t firmware_raw = read_u32(report.data, 2);
  data.device.firmware_version = convert_apc_date(firmware_raw);

  ESP_LOGD(APC_HID_TAG, "Device report: nominal=%u W, firmware=%s",
           realpower_nominal, data.device.firmware_version.c_str());
}

}  // namespace

// ApcProtocol implementation

bool ApcProtocol::detect() {
  // For now, rely on vendor ID detection in factory
  detected_ = true;
  return true;
}

bool ApcProtocol::initialize() {
  if (!detected_) {
    if (!detect())
      return false;
  }

  ESP_LOGI(APC_HID_TAG, "APC HID protocol initialized");
  return true;
}

bool ApcProtocol::read_data(UpsData &data) {
  if (!parent_ || !parent_->is_connected()) {
    ESP_LOGW(APC_HID_TAG, "Cannot read data: parent not connected");
    return false;
  }

  // Try reading all known report types
  bool success = false;

  std::vector<uint8_t> report_data;

  // Battery report
  if (read_single_report(report_id::BATTERY_STATUS, report_data)) {
    HidReport report{report_id::BATTERY_STATUS, report_data};
    ApcReportParser::parse_battery_report(report, data);
    success = true;
  }

  // Power report
  if (read_single_report(report_id::POWER_STATUS, report_data)) {
    HidReport report{report_id::POWER_STATUS, report_data};
    ApcReportParser::parse_power_report(report, data);
    success = true;
  }

  // Test report
  if (read_single_report(report_id::TEST_STATUS, report_data)) {
    HidReport report{report_id::TEST_STATUS, report_data};
    ApcReportParser::parse_test_report(report, data);
    success = true;
  }

  // Config report
  if (read_single_report(report_id::CONFIG_STATUS, report_data)) {
    HidReport report{report_id::CONFIG_STATUS, report_data};
    ApcReportParser::parse_config_report(report, data);
    success = true;
  }

  // Device report
  if (read_single_report(report_id::DEVICE_STATUS, report_data)) {
    HidReport report{report_id::DEVICE_STATUS, report_data};
    ApcReportParser::parse_device_report(report, data);
    success = true;
  }

  return success;
}

bool ApcProtocol::beeper_enable() {
  // Implementation depends on specific APC HID commands
  ESP_LOGW(APC_HID_TAG, "Beeper enable not implemented yet");
  return false;
}

bool ApcProtocol::beeper_disable() {
  ESP_LOGW(APC_HID_TAG, "Beeper disable not implemented yet");
  return false;
}

bool ApcProtocol::beeper_mute() {
  ESP_LOGW(APC_HID_TAG, "Beeper mute not implemented yet");
  return false;
}

bool ApcProtocol::beeper_test() {
  ESP_LOGW(APC_HID_TAG, "Beeper test not implemented yet");
  return false;
}

bool ApcProtocol::start_battery_test_quick() {
  ESP_LOGW(APC_HID_TAG, "Quick battery test not implemented yet");
  return false;
}

bool ApcProtocol::start_battery_test_deep() {
  ESP_LOGW(APC_HID_TAG, "Deep battery test not implemented yet");
  return false;
}

bool ApcProtocol::stop_battery_test() {
  ESP_LOGW(APC_HID_TAG, "Stop battery test not implemented yet");
  return false;
}

bool ApcProtocol::start_ups_test() {
  ESP_LOGW(APC_HID_TAG, "UPS test not implemented yet");
  return false;
}

bool ApcProtocol::stop_ups_test() {
  ESP_LOGW(APC_HID_TAG, "Stop UPS test not implemented yet");
  return false;
}

bool ApcProtocol::read_timer_data(UpsData &data) {
  // For now, reuse the test report parsing for timer data
  std::vector<uint8_t> report_data;
  if (!read_single_report(report_id::TEST_STATUS, report_data)) {
    return false;
  }

  HidReport report{report_id::TEST_STATUS, report_data};
  ApcReportParser::parse_test_report(report, data);
  return true;
}

bool ApcProtocol::set_shutdown_delay(int seconds) {
  ESP_LOGW(APC_HID_TAG, "Set shutdown delay not implemented yet");
  return false;
}

bool ApcProtocol::set_start_delay(int seconds) {
  ESP_LOGW(APC_HID_TAG, "Set start delay not implemented yet");
  return false;
}

bool ApcProtocol::set_reboot_delay(int seconds) {
  ESP_LOGW(APC_HID_TAG, "Set reboot delay not implemented yet");
  return false;
}

bool ApcProtocol::send_simple_command(const std::vector<uint8_t> &command,
                                      std::vector<uint8_t> &response,
                                      uint32_t timeout_ms) {
  if (!parent_) {
    ESP_LOGE(APC_HID_TAG, "Cannot send command: parent is null");
    return false;
  }

  uint8_t report_id = command.empty() ? 0 : command[0];
  uint8_t data[64] = {0};
  size_t data_len = command.size();

  if (data_len > sizeof(data)) {
    ESP_LOGE(APC_HID_TAG, "Command too long: %zu bytes", data_len);
    return false;
  }

  std::copy(command.begin(), command.end(), data);

  if (parent_->hid_set_report(0x02, report_id, data, data_len, timeout_ms) !=
      ESP_OK) {
    ESP_LOGE(APC_HID_TAG, "Failed to send HID command (report_id=0x%02X)",
             report_id);
    return false;
  }

  uint8_t response_data[64] = {0};
  size_t response_len = sizeof(response_data);

  if (parent_->hid_get_report(0x01, report_id, response_data, &response_len,
                              timeout_ms) != ESP_OK) {
    ESP_LOGE(APC_HID_TAG, "Failed to read HID response (report_id=0x%02X)",
             report_id);
    return false;
  }

  response.assign(response_data, response_data + response_len);
  return true;
}

bool ApcProtocol::read_single_report(uint8_t report_id,
                                     std::vector<uint8_t> &report_data,
                                     uint32_t timeout_ms) {
  if (!parent_) {
    ESP_LOGE(APC_HID_TAG, "Cannot read report: parent is null");
    return false;
  }

  uint8_t data[64] = {0};
  size_t data_len = sizeof(data);

  if (parent_->hid_get_report(0x01, report_id, data, &data_len, timeout_ms) !=
      ESP_OK) {
    ESP_LOGW(APC_HID_TAG, "Failed to read HID report (report_id=0x%02X)",
             report_id);
    return false;
  }

  report_data.assign(data, data + data_len);
  return true;
}

std::unique_ptr<UpsProtocolBase> create_apc_protocol(
    UpsHidComponent *parent) {
  return std::make_unique<ApcProtocol>(parent);
}

}  // namespace ups_hid
}  // namespace esphome
