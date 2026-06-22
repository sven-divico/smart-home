# Minimal READ-ONLY battery reader for the AXP2101 PMU.
#
# Reads only the fuel-gauge percentage register (0xA4). It NEVER writes any
# register, so it cannot touch the power rails that feed the display — unlike the
# full M5Core2-oriented AXP2101 components which reconfigure every rail in setup().
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c, sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_BATTERY,
    ICON_BATTERY,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
)

DEPENDENCIES = ["i2c"]

axp2101_batt_ns = cg.esphome_ns.namespace("axp2101_batt")
AXP2101Batt = axp2101_batt_ns.class_(
    "AXP2101Batt", cg.PollingComponent, i2c.I2CDevice
)

CONF_BATTERY_LEVEL = "battery_level"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AXP2101Batt),
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon=ICON_BATTERY,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x34))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    if CONF_BATTERY_LEVEL in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_level_sensor(sens))
