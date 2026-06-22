#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace axp2101_batt {

// Read-only AXP2101 battery percentage. Issues only I2C reads; never writes a
// register, so the PMU power rails (which feed the display) are never touched.
class AXP2101Batt : public PollingComponent, public i2c::I2CDevice {
 public:
  void set_battery_level_sensor(sensor::Sensor *s) { this->battery_level_ = s; }
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  sensor::Sensor *battery_level_{nullptr};
};

}  // namespace axp2101_batt
}  // namespace esphome
