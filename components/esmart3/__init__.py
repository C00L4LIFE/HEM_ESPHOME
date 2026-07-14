import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_FLOW_CONTROL_PIN, CONF_ID

CODEOWNERS = ["@alain"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

CONF_ESMART3_ID = "esmart3_id"

esmart3_ns = cg.esphome_ns.namespace("esmart3")
ESmart3Component = esmart3_ns.class_(
    "ESmart3Component", cg.PollingComponent, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ESmart3Component),
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

ESMART3_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ESMART3_ID): cv.use_id(ESmart3Component),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))
