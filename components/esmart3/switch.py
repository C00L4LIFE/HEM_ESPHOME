import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from . import CONF_ESMART3_ID, ESMART3_COMPONENT_SCHEMA, esmart3_ns

DEPENDENCIES = ["esmart3"]

ESmart3LoadSwitch = esmart3_ns.class_("ESmart3LoadSwitch", switch.Switch)

CONF_LOAD = "load"

CONFIG_SCHEMA = ESMART3_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_LOAD): switch.switch_schema(
            ESmart3LoadSwitch,
            icon="mdi:power-plug",
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    if CONF_LOAD in config:
        var = await switch.new_switch(config[CONF_LOAD])
        await cg.register_parented(var, config[CONF_ESMART3_ID])
        cg.add(hub.set_load_switch(var))
