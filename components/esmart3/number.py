import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_CONFIG,
    UNIT_AMPERE,
    UNIT_MINUTE,
    UNIT_VOLT,
)

from . import CONF_ESMART3_ID, ESMART3_COMPONENT_SCHEMA, esmart3_ns

DEPENDENCIES = ["esmart3"]

ESmart3Number = esmart3_ns.class_("ESmart3Number", number.Number)

CONF_MAX_CHARGE_CURRENT = "max_charge_current"
CONF_MAX_LOAD_CURRENT = "max_load_current"
CONF_BULK_VOLTAGE = "bulk_voltage"
CONF_FLOAT_VOLTAGE = "float_voltage"
CONF_EQUALIZE_VOLTAGE = "equalize_voltage"
CONF_EQUALIZE_TIME = "equalize_time"
CONF_LOAD_OVP = "load_ovp"
CONF_LOAD_UVP = "load_uvp"
CONF_BATTERY_OVP = "battery_ovp"
CONF_BATTERY_OVP_RECOVER = "battery_ovp_recover"
CONF_BATTERY_UVP = "battery_uvp"
CONF_BATTERY_UVP_RECOVER = "battery_uvp_recover"


def current_schema():
    return number.number_schema(
        ESmart3Number,
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon="mdi:current-dc",
    )


def voltage_schema():
    return number.number_schema(
        ESmart3Number,
        unit_of_measurement=UNIT_VOLT,
        device_class=DEVICE_CLASS_VOLTAGE,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon="mdi:sine-wave",
    )


def minutes_schema():
    return number.number_schema(
        ESmart3Number,
        unit_of_measurement=UNIT_MINUTE,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon="mdi:timer-outline",
    )


# clé -> (purpose côté C++ (ESmart3Component::PendingKind), min, max, step, schéma)
# Bornes reprises de la doc Joba_ESmart3 (defaults typiques 12V/24V lead-acid) avec marge.
NUMBERS = {
    CONF_MAX_CHARGE_CURRENT: (1, 0.0, 60.0, 0.1, current_schema),
    CONF_MAX_LOAD_CURRENT: (2, 0.0, 40.0, 0.1, current_schema),
    CONF_BULK_VOLTAGE: (4, 8.0, 32.0, 0.1, voltage_schema),
    CONF_FLOAT_VOLTAGE: (5, 8.0, 32.0, 0.1, voltage_schema),
    CONF_EQUALIZE_VOLTAGE: (6, 8.0, 32.0, 0.1, voltage_schema),
    CONF_EQUALIZE_TIME: (7, 0.0, 180.0, 1.0, minutes_schema),
    CONF_LOAD_OVP: (9, 8.0, 35.0, 0.1, voltage_schema),
    CONF_LOAD_UVP: (10, 5.0, 20.0, 0.1, voltage_schema),
    CONF_BATTERY_OVP: (11, 8.0, 35.0, 0.1, voltage_schema),
    CONF_BATTERY_OVP_RECOVER: (12, 8.0, 35.0, 0.1, voltage_schema),
    CONF_BATTERY_UVP: (13, 5.0, 20.0, 0.1, voltage_schema),
    CONF_BATTERY_UVP_RECOVER: (14, 5.0, 20.0, 0.1, voltage_schema),
}

CONFIG_SCHEMA = ESMART3_COMPONENT_SCHEMA.extend(
    {cv.Optional(key): schema_fn() for key, (_, _, _, _, schema_fn) in NUMBERS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    for key, (purpose, min_v, max_v, step, _schema_fn) in NUMBERS.items():
        if key in config:
            var = await number.new_number(
                config[key], min_value=min_v, max_value=max_v, step=step
            )
            await cg.register_parented(var, config[CONF_ESMART3_ID])
            cg.add(var.set_purpose(purpose))
            cg.add(getattr(hub, f"set_{key}_number")(var))
