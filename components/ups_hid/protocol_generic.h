#pragma once

#include "ups_hid.h"
#include "constants_ups.h"

#include <string>

namespace esphome {
namespace ups_hid {

/**
 * @brief Generic HID Protocol Implementation (fallback)
 */
class GenericHidProtocol : public UpsProtocolBase {
 public:
  explicit GenericHidProtocol(UpsHidComponent *parent) : UpsProtocolBase(parent) {}

  bool detect() override;
  bool initialize() override;
  bool read_data(UpsData &data) override;
  DeviceInfo::DetectedProtocol get_protocol_type() const override {
    return DeviceInfo::PROTOCOL_GENERIC_HID;
  }
  std::string get_protocol_name() const override { return "Generic HID"; }

  // Timer polling method for real-time countdown
  bool read_timer_data(UpsData &data) override;

 private:
  bool detected_{false};
};

// Factory function to create a generic HID protocol instance
std::unique_ptr<UpsProtocolBase> create_generic_protocol(
    UpsHidComponent *parent);

}  // namespace ups_hid
}  // namespace esphome
