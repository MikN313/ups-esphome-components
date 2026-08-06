#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace esphome {
namespace ups_hid {

class UpsHidComponent;
class UpsProtocolBase;

/**
 * @brief Factory class for creating UPS protocol instances.
 *
 * This class provides a flexible registry-based mechanism to:
 * - Register specific protocols for particular vendors
 * - Register fallback protocols for unknown vendors
 * - Create protocol instances based on vendor ID or protocol name
 * - Query supported protocols and vendor support
 */
class ProtocolFactory {
 public:
  // Information about a protocol implementation
  struct ProtocolInfo {
    // Protocol name (for UI/logging, e.g., "APC HID")
    std::string name;
    // Short identifier (optional, can be same as name or empty)
    std::string identifier;
    // Human-readable description
    std::string description;
    // Protocol priority (higher value = preferred)
    int priority = 0;
    // Creator function for protocol instances
    std::function<std::unique_ptr<UpsProtocolBase>(UpsHidComponent *parent)> creator;
  };

  // Register a protocol for a specific vendor ID
  static void register_protocol_for_vendor(uint16_t vendor_id,
                                           const ProtocolInfo &info);

  // Register a fallback protocol (used when no vendor-specific protocol matches)
  static void register_fallback_protocol(const ProtocolInfo &info);

  // Create a protocol instance for a given vendor ID
  static std::unique_ptr<UpsProtocolBase> create_for_vendor(uint16_t vendor_id,
                                                            UpsHidComponent *parent);

  // Create a protocol instance by name (case-insensitive, substring match)
  static std::unique_ptr<UpsProtocolBase> create_by_name(const std::string &protocol_name,
                                                         UpsHidComponent *parent);

  // Check if there is support for the given vendor ID
  static bool has_vendor_support(uint16_t vendor_id);

  // Get a list of all registered protocols for a vendor (including fallbacks)
  static std::vector<ProtocolInfo> get_protocols_for_vendor(uint16_t vendor_id);

  // Get a list of all protocols (vendor-specific and fallbacks)
  static std::vector<std::pair<uint16_t, ProtocolInfo>> get_all_protocols();

 private:
  // Vendor-specific protocol registry: vendor_id -> list of protocols
  static std::unordered_map<uint16_t, std::vector<ProtocolInfo>> &get_vendor_registry();

  // Fallback protocol registry (used when no vendor-specific protocol matches)
  static std::vector<ProtocolInfo> &get_fallback_registry();

  // Ensure registries are initialized (optional explicit initialization)
  static void ensure_initialized();
};

}  // namespace ups_hid
}  // namespace esphome
