#include "axp2101_batt.h"
#include "esphome/core/log.h"

namespace esphome {
namespace axp2101_batt {

static const char *const TAG = "axp2101_batt";
// AXP2101 fuel-gauge battery percentage, 0..100. Read-only.
static const uint8_t REG_BAT_PERCENT = 0xA4;

void AXP2101Batt::update() {
  uint8_t pct = 0;
  // READ ONLY — read_register writes only the register pointer for the read
  // transaction; it does not modify any AXP2101 configuration/rail register.
  if (this->read_register(REG_BAT_PERCENT, &pct, 1) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Failed to read battery percent register");
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();
  ESP_LOGD(TAG, "battery percent raw=%u", pct);
  // 0xFF / >100 means the fuel gauge isn't producing a valid reading yet.
  if (this->battery_level_ != nullptr && pct <= 100) {
    this->battery_level_->publish_state(pct);
  }
}

void AXP2101Batt::dump_config() {
  ESP_LOGCONFIG(TAG, "AXP2101 battery (read-only):");
  LOG_I2C_DEVICE(this);
  LOG_SENSOR("  ", "Battery level", this->battery_level_);
}

}  // namespace axp2101_batt
}  // namespace esphome
