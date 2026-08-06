#pragma once

#include "ups_hid.h"
#include "constants_ups.h"

#include <string>

namespace esphome {
namespace ups_hid {

/**
 * @brief CyberPower HID Protocol Implementation
 */
class CyberPowerProtocol : public UpsProtocolBase {
 public:
  explicit CyberPowerProtocol(UpsHidComponent *parent) : UpsProtocolBase(parent) {}

  bool detect() override;
  bool initialize() override;
  bool read_data(UpsData &data) override;
  DeviceInfo::DetectedProtocol get_protocol_type() const override {
    return DeviceInfo::PROTOCOL_CYBERPOWER_HID;
  }
  std::string get_protocol_name() const override { return "CyberPower HID"; }

  // Timer polling method for real-time countdown
  bool read_timer_data(UpsData &data) override;

 private:
  bool detected_{false};

  bool read_single_report(uint8_t report_id, std::vector<uint8_t> &report_data,
                          uint32_t timeout_ms = 1000);
};

// Factory function to create a CyberPower protocol instance
std::unique_ptr<UpsProtocolBase> create_cyberpower_protocol(
    UpsHidComponent *parent);

}  // namespace ups_hid
}  // namespace esphome
