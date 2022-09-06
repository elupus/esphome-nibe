import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import (
    CONF_ID,
    CONF_RX_PIN,
    CONF_TX_PIN,
    CONF_UART_ID,
)
from esphome import pins

DEPENDENCIES = ["logger"]

NibeGwComponent = cg.global_ns.class_("NibeGwComponent", cg.Component)

CONF_DIR_PIN = "dir_pin"
CONF_TARGET_PORT = "target_port"
CONF_TARGET_IP = "target_ip"
CONF_ACKNOWLEDGE = "acknowledge"

CONF_ACKNOWLEDGE_MODBUS40 = "modbus40"
CONF_ACKNOWLEDGE_RMU40 = "rmu40"
CONF_ACKNOWLEDGE_SMS40 = "sms40"

ACKNOWLEDGE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ACKNOWLEDGE_MODBUS40): cv.boolean,
        cv.Optional(CONF_ACKNOWLEDGE_RMU40): cv.boolean,
        cv.Optional(CONF_ACKNOWLEDGE_SMS40): cv.boolean,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NibeGwComponent),
        cv.Required(CONF_TARGET_IP): cv.ipv4,
        cv.Optional(CONF_TARGET_PORT, default=9999): cv.port,
        cv.Optional(CONF_ACKNOWLEDGE, default={}): ACKNOWLEDGE_SCHEMA,
        cv.Optional(CONF_RX_PIN): pins.internal_gpio_input_pin_number,
        cv.Optional(CONF_TX_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_DIR_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_UART_ID, default=2): int,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(
        config[CONF_ID],
        config[CONF_UART_ID],
        config[CONF_DIR_PIN],
        config[CONF_RX_PIN],
        config[CONF_TX_PIN],
    )
    await cg.register_component(var, config)

    cg.add_build_flag("-DENABLE_NIBE_DEBUG")
    cg.add_library("WiFi", None)
    cg.add_library("WiFiUdp", None)

    cg.add(var.set_target_ip(str(config[CONF_TARGET_IP])))
    cg.add(var.set_target_port(config[CONF_TARGET_PORT]))

    if config[CONF_ACKNOWLEDGE]:
        cg.add(var.gw().setSendAcknowledge(1))
        cg.add(
            var.gw().setAckModbus40Address(
                int(config[CONF_ACKNOWLEDGE].get(CONF_ACKNOWLEDGE_MODBUS40, False))
            )
        )
        cg.add(
            var.gw().setAckRmu40Address(
                int(config[CONF_ACKNOWLEDGE].get(CONF_ACKNOWLEDGE_RMU40, False))
            )
        )
        cg.add(
            var.gw().setAckSms40Address(
                int(config[CONF_ACKNOWLEDGE].get(CONF_ACKNOWLEDGE_SMS40, False))
            )
        )
    else:
        cg.add(var.gw().setSendAcknowledge(0))
