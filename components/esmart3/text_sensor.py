import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import CONF_ESMART3_ID, ESMART3_COMPONENT_SCHEMA

DEPENDENCIES = ["esmart3"]

CONF_CHARGE_MODE = "charge_mode"
CONF_ERRORS = "errors"
CONF_MODEL = "model"
CONF_SERIAL_NUMBER = "serial_number"
CONF_FIRMWARE = "firmware"

TEXT_SENSORS = {
    CONF_CHARGE_MODE: text_sensor.text_sensor_schema(icon="mdi:solar-power"),
    CONF_ERRORS: text_sensor.text_sensor_schema(icon="mdi:alert-circle-outline"),
    CONF_MODEL: text_sensor.text_sensor_schema(icon="mdi:chip"),
    CONF_SERIAL_NUMBER: text_sensor.text_sensor_schema(icon="mdi:barcode"),
    CONF_FIRMWARE: text_sensor.text_sensor_schema(icon="mdi:numeric"),
}

CONFIG_SCHEMA = ESMART3_COMPONENT_SCHEMA.extend(
    {cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()}
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ESMART3_ID])
    for key in TEXT_SENSORS:
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(hub, f"set_{key}_text_sensor")(sens))
