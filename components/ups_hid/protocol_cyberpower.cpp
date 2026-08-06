#include "protocol_cyberpower.h"
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

static const char *const CYBERPOWER_HID_TAG = "cyberpower_hid";

// HID report IDs used by CyberPower devices
namespace report_id {
static constexpr uint8_t BATTERY_STATUS = 0x01;
static constexpr uint8_t POWER_STATUS   = 0x02;
static constexpr uint8_t CONFIG_STATUS  = 0x03;
static constexpr uint8_t DEVICE_STATUS  = 0x04;
}  // namespace report_id

// CyberPower HID vendor IDs
namespace vendor_id {
static constexpr uint16_t CYBERPOWER = 0x0764;
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

// Helper class to parse CyberPower HID reports
class CyberPowerReportParser {
 public:
  static void parse_battery_report(const HidReport &report, UpsData &data);
  static void parse_power_report(const HidReport &report, UpsData &data);
  static void parse_config_report(const HidReport &report, UpsData &data);
  static void parse_device_report(const HidReport &report, UpsData &data);
};

void CyberPowerReportParser::parse_battery_report(const HidReport &report,
                                                  UpsData &data) {
  if (report.data.size() < 8) {
    ESP_LOGW(CYBERPOWER_HID_TAG, "Battery report too short: %zu bytes",
             report.data.size());
    return;
  }

  uint8_t battery_level        = report.data[0];
  data.battery.level           = static_cast<float>(battery_level);
  data.battery.voltage         = read_float(report.data, 1, 0.1f);
  data.battery.voltage_nominal = read_float(report.data, 3, 0.1f);

  uint32_t runtime_raw = read_u32(report.data, 5);
  if (runtime_raw > 0) {
    data.battery.runtime_minutes = runtime_raw / 60.0f;
  }

  ESP_LOGD(CYBERPOWER_HID_TAG,
           "Battery report: level=%u%%, voltage=%.1f V, nominal=%.1f V, runtime=%" PRIu32 " s",
           battery_level, data.battery.voltage, data.battery.voltage_nominal,
           runtime_raw);

  // Nessun flag 'valid' esplicito nel modello nuovo.
}

void CyberPowerReportParser::parse_power_report(const HidReport &report,
                                                UpsData &data) {
  if (report.data.size() < 12) {
    ESP_LOGW(CYBERPOWER_HID_TAG, "Power report too short: %zu bytes",
             report.data.size());
    return;
  }

  data.power.input_voltage         = read_float(report.data, 0, 0.1f);
  data.power.output_voltage        = read_float(report.data, 2, 0.1f);
  data.power.input_voltage_nominal = read_float(report.data, 4, 0.1f);
  data.power.load_percent          = read_float(report.data, 6, 1.0f);
  data.power.frequency             = read_float(report.data, 8, 0.1f);

  ESP_LOGD(CYBERPOWER_HID_TAG,
           "Power report: Vin=%.1f V, Vout=%.1f V, nominal=%.1f V, load=%.1f%%, freq=%.1f Hz",
           data.power.input_voltage, data.power.output_voltage,
           data.power.input_voltage_nominal, data.power.load_percent,
           data.power.frequency);

  // Usa il flag diretto presente in PowerData.
  //data.power.input_voltage_valid = (data.power.input_voltage > 0.0f);
}

void CyberPowerReportParser::parse_config_report(const HidReport &report,
                                                 UpsData &data) {
  if (report.data.size() < 4) {
    ESP_LOGW(CYBERPOWER_HID_TAG, "Config report too short: %zu bytes",
             report.data.size());
    return;
  }

  data.config.delay_shutdown = static_cast<int>(report.data[0]);
  data.config.delay_start    = static_cast<int>(report.data[1]);
  data.config.delay_reboot   = static_cast<int>(report.data[2]);

  ESP_LOGD(CYBERPOWER_HID_TAG,
           "Config report: shutdown=%d s, start=%d s, reboot=%d s",
           data.config.delay_shutdown, data.config.delay_start,
           data.config.delay_reboot);
}

void CyberPowerReportParser::parse_device_report(const HidReport &report,
                                                 UpsData &data) {
  if (report.data.size() < 8) {
    ESP_LOGW(CYBERPOWER_HID_TAG, "Device report too short: %zu bytes",
             report.data.size());
    return;
  }

  uint16_t realpower_nominal = read_u16(report.data, 0);
  data.power.realpower_nominal = static_cast<float>(realpower_nominal);

  ESP_LOGD(CYBERPOWER_HID_TAG, "Device report: nominal=%u W", realpower_nominal);
}

}  // namespace

// CyberPowerProtocol implementation

bool CyberPowerProtocol::detect() {
  // For now, rely on vendor ID detection in factory
  detected_ = true;
  return true;
}

bool CyberPowerProtocol::initialize() {
  if (!detected_) {
    if (!detect())
      return false;
  }

  ESP_LOGI(CYBERPOWER_HID_TAG, "CyberPower HID protocol initialized");
  return true;
}

bool CyberPowerProtocol::read_data(UpsData &data) {
  if (!parent_ || !parent_->is_connected()) {
    ESP_LOGW(CYBERPOWER_HID_TAG, "Cannot read data: parent not connected");
    return false;
  }

  bool success = false;
  std::vector<uint8_t> report_data;

  // Battery report
  if (read_single_report(report_id::BATTERY_STATUS, report_data)) {
    HidReport report{report_id::BATTERY_STATUS, report_data};
    CyberPowerReportParser::parse_battery_report(report, data);
    success = true;
  }

  // Power report
  if (read_single_report(report_id::POWER_STATUS, report_data)) {
    HidReport report{report_id::POWER_STATUS, report_data};
    CyberPowerReportParser::parse_power_report(report, data);
    success = true;
  }

  // Config report
  if (read_single_report(report_id::CONFIG_STATUS, report_data)) {
    HidReport report{report_id::CONFIG_STATUS, report_data};
    CyberPowerReportParser::parse_config_report(report, data);
    success = true;
  }

  // Device report
  if (read_single_report(report_id::DEVICE_STATUS, report_data)) {
    HidReport report{report_id::DEVICE_STATUS, report_data};
    CyberPowerReportParser::parse_device_report(report, data);
    success = true;
  }

  return success;
}

bool CyberPowerProtocol::read_timer_data(UpsData &data) {
  // For now, reuse the config report for timer data
  std::vector<uint8_t> report_data;
  if (!read_single_report(report_id::CONFIG_STATUS, report_data)) {
    return false;
  }

  HidReport report{report_id::CONFIG_STATUS, report_data};
  CyberPowerReportParser::parse_config_report(report, data);
  return true;
}

bool CyberPowerProtocol::read_single_report(uint8_t report_id,
                                            std::vector<uint8_t> &report_data,
                                            uint32_t timeout_ms) {
  if (!parent_) {
    ESP_LOGE(CYBERPOWER_HID_TAG, "Cannot read report: parent is null");
    return false;
  }

  uint8_t data[64] = {0};
  size_t data_len  = sizeof(data);

  if (parent_->hid_get_report(0x01, report_id, data, &data_len, timeout_ms) !=
      ESP_OK) {
    ESP_LOGW(CYBERPOWER_HID_TAG, "Failed to read HID report (report_id=0x%02X)",
             report_id);
    return false;
  }

  report_data.assign(data, data + data_len);
  return true;
}

std::unique_ptr<UpsProtocolBase> create_cyberpower_protocol(
    UpsHidComponent *parent) {
  return std::make_unique<CyberPowerProtocol>(parent);
}

}  // namespace ups_hid
}  // namespace esphome
