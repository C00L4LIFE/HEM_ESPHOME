import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import CONF_ESMART3_ID, ESMART3_COMPONENT_SCHEMA, esmart3_ns

DEPENDENCIES = ["esmart3"]

ESmart3BatteryTypeSelect = esmart3_ns.class_("ESmart3BatteryTypeSelect", select.Select)

CONF_BATTERY_TYPE = "battery_type"

# Doit correspondre à BATTERY_TYPE_NAMES dans esmart3.cpp (index = valeur wBatType)
BATTERY_TYPES = ["Utilisateur", "Plomb-acide", "Gel", "AGM"]

CONFIG_SCHEMA = ESMART3_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_BATTERY_TYPE): select.select_schema(
            ESmart3BatteryTypeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:car-battery",
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    if CONF_BATTERY_TYPE in config:
        var = await select.new_select(config[CONF_BATTERY_TYPE], options=BATTERY_TYPES)
        await cg.register_parented(var, config[CONF_ESMART3_ID])
        cg.add(hub.set_battery_type_select(var))
