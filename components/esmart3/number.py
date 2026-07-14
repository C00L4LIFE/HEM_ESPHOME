import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    ENTITY_CATEGORY_CONFIG,
    UNIT_AMPERE,
)

from . import CONF_ESMART3_ID, ESMART3_COMPONENT_SCHEMA, esmart3_ns

DEPENDENCIES = ["esmart3"]

ESmart3Number = esmart3_ns.class_("ESmart3Number", number.Number)

CONF_MAX_CHARGE_CURRENT = "max_charge_current"
CONF_MAX_LOAD_CURRENT = "max_load_current"

# (purpose, min, max, step)
NUMBERS = {
    CONF_MAX_CHARGE_CURRENT: (1, 0.0, 60.0, 0.1),
    CONF_MAX_LOAD_CURRENT: (2, 0.0, 40.0, 0.1),
}

CONFIG_SCHEMA = ESMART3_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(key): number.number_schema(
            ESmart3Number,
            unit_of_measurement=UNIT_AMPERE,
            device_class=DEVICE_CLASS_CURRENT,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:current-dc",
        )
        for key in NUMBERS
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    for key, (purpose, min_v, max_v, step) in NUMBERS.items():
        if key in config:
            var = await number.new_number(
                config[key], min_value=min_v, max_value=max_v, step=step
            )
            await cg.register_parented(var, config[CONF_ESMART3_ID])
            cg.add(var.set_purpose(purpose))
            cg.add(getattr(hub, f"set_{key}_number")(var))
