#pragma once

#include "ups_hid.h"
#include "constants_ups.h"

#include <string>

namespace esphome {
namespace ups_hid {

/**
 * @brief APC HID Protocol Implementation
 */
class ApcProtocol : public UpsProtocolBase {
 public:
  explicit ApcProtocol(UpsHidComponent *parent) : UpsProtocolBase(parent) {}

  bool detect() override;
  bool initialize() override;
  bool read_data(UpsData &data) override;
  DeviceInfo::DetectedProtocol get_protocol_type() const override {
    return DeviceInfo::PROTOCOL_APC_HID;
  }
  std::string get_protocol_name() const override { return "APC HID"; }

  // Beeper control methods
  bool beeper_enable() override;
  bool beeper_disable() override;
  bool beeper_mute() override;
  bool beeper_test() override;

  // UPS and battery test methods
  bool start_battery_test_quick() override;
  bool start_battery_test_deep() override;
  bool stop_battery_test() override;
  bool start_ups_test() override;
  bool stop_ups_test() override;

  // Timer polling method for real-time countdown
  bool read_timer_data(UpsData &data) override;

  // Delay configuration methods
  bool set_shutdown_delay(int seconds) override;
  bool set_start_delay(int seconds) override;
  bool set_reboot_delay(int seconds) override;

 private:
  bool detected_{false};

  bool send_simple_command(const std::vector<uint8_t> &command,
                           std::vector<uint8_t> &response,
                           uint32_t timeout_ms = 1000);

  bool read_single_report(uint8_t report_id, std::vector<uint8_t> &report_data,
                          uint32_t timeout_ms = 1000);

  bool read_status_reports(UpsData &data);
};

// Factory function to create an APC protocol instance
std::unique_ptr<UpsProtocolBase> create_apc_protocol(UpsHidComponent *parent);

}  // namespace ups_hid
}  // namespace esphome
