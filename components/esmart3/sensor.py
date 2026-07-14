import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_WEIGHT,
    ICON_COUNTER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_KILOGRAM,
    UNIT_KILOWATT_HOURS,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)

from . import CONF_ESMART3_ID, ESMART3_COMPONENT_SCHEMA

DEPENDENCIES = ["esmart3"]

CONF_PV_VOLTAGE = "pv_voltage"
CONF_PV_CURRENT = "pv_current"
CONF_PV_POWER = "pv_power"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_CHARGE_CURRENT = "charge_current"
CONF_CHARGE_POWER = "charge_power"
CONF_BATTERY_SOC = "battery_soc"
CONF_BATTERY_TEMPERATURE = "battery_temperature"
CONF_CONTROLLER_TEMPERATURE = "controller_temperature"
CONF_LOAD_VOLTAGE = "load_voltage"
CONF_LOAD_CURRENT = "load_current"
CONF_LOAD_POWER = "load_power"
CONF_CO2_SAVED = "co2_saved"
CONF_FAULT_COUNT = "fault_count"
CONF_FAULT_BITMASK = "fault_bitmask"
CONF_ENERGY_TODAY = "energy_today"
CONF_ENERGY_MONTH = "energy_month"
CONF_ENERGY_TOTAL = "energy_total"
CONF_LOAD_ENERGY_TODAY = "load_energy_today"
CONF_LOAD_ENERGY_MONTH = "load_energy_month"
CONF_LOAD_ENERGY_TOTAL = "load_energy_total"
CONF_BULK_VOLTAGE = "bulk_voltage"
CONF_FLOAT_VOLTAGE = "float_voltage"
CONF_EQUALIZE_VOLTAGE = "equalize_voltage"
CONF_MAX_CHARGE_CURRENT = "max_charge_current"
CONF_MAX_DISCHARGE_CURRENT = "max_discharge_current"
CONF_BATTERY_OVP = "battery_ovp"
CONF_BATTERY_UVP = "battery_uvp"


def voltage_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def current_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def power_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def temperature_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


def energy_wh_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT_HOURS,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    )


def energy_kwh_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        accuracy_decimals=2,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    )


SENSORS = {
    CONF_PV_VOLTAGE: voltage_schema(),
    CONF_PV_CURRENT: current_schema(),
    CONF_PV_POWER: power_schema(),
    CONF_BATTERY_VOLTAGE: voltage_schema(),
    CONF_CHARGE_CURRENT: current_schema(),
    CONF_CHARGE_POWER: power_schema(),
    CONF_BATTERY_SOC: sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    CONF_BATTERY_TEMPERATURE: temperature_schema(),
    CONF_CONTROLLER_TEMPERATURE: temperature_schema(),
    CONF_LOAD_VOLTAGE: voltage_schema(),
    CONF_LOAD_CURRENT: current_schema(),
    CONF_LOAD_POWER: power_schema(),
    CONF_CO2_SAVED: sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOGRAM,
        accuracy_decimals=2,
        device_class=DEVICE_CLASS_WEIGHT,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    ),
    CONF_FAULT_COUNT: sensor.sensor_schema(
        icon=ICON_COUNTER,
        accuracy_decimals=0,
    ),
    CONF_FAULT_BITMASK: sensor.sensor_schema(
        icon="mdi:alert-circle-outline",
        accuracy_decimals=0,
    ),
    CONF_ENERGY_TODAY: energy_wh_schema(),
    CONF_ENERGY_MONTH: energy_kwh_schema(),
    CONF_ENERGY_TOTAL: energy_kwh_schema(),
    CONF_LOAD_ENERGY_TODAY: energy_wh_schema(),
    CONF_LOAD_ENERGY_MONTH: energy_kwh_schema(),
    CONF_LOAD_ENERGY_TOTAL: energy_kwh_schema(),
    CONF_BULK_VOLTAGE: voltage_schema(),
    CONF_FLOAT_VOLTAGE: voltage_schema(),
    CONF_EQUALIZE_VOLTAGE: voltage_schema(),
    CONF_MAX_CHARGE_CURRENT: current_schema(),
    CONF_MAX_DISCHARGE_CURRENT: current_schema(),
    CONF_BATTERY_OVP: voltage_schema(),
    CONF_BATTERY_UVP: voltage_schema(),
}

CONFIG_SCHEMA = ESMART3_COMPONENT_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    for key in SENSORS:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
