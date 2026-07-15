import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import CONF_ESMART3_ID, ESMART3_COMPONENT_SCHEMA, esmart3_ns

DEPENDENCIES = ["esmart3"]

ESmart3ResetEnergyButton = esmart3_ns.class_("ESmart3ResetEnergyButton", button.Button)

CONF_RESET_ENERGY = "reset_energy"

CONFIG_SCHEMA = ESMART3_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_RESET_ENERGY): button.button_schema(
            ESmart3ResetEnergyButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:restart",
        ),
    }
)


async def to_code(config):
    if CONF_RESET_ENERGY in config:
        var = await button.new_button(config[CONF_RESET_ENERGY])
        await cg.register_parented(var, config[CONF_ESMART3_ID])
