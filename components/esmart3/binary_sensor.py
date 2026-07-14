import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_CONNECTIVITY, DEVICE_CLASS_PROBLEM

from . import CONF_ESMART3_ID, ESMART3_COMPONENT_SCHEMA

DEPENDENCIES = ["esmart3"]

CONF_ONLINE_STATUS = "online_status"
CONF_FAULT_ACTIVE = "fault_active"

BINARY_SENSORS = {
    CONF_ONLINE_STATUS: binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_CONNECTIVITY,
    ),
    CONF_FAULT_ACTIVE: binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_PROBLEM,
    ),
}

CONFIG_SCHEMA = ESMART3_COMPONENT_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in BINARY_SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    for key in BINARY_SENSORS:
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_binary_sensor")(sens))
