import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import (
    CONF_ID,
    CONF_RX_PIN,
    CONF_TX_PIN,
    CONF_UART_ID,
    CONF_DEBUG,
)
from esphome import pins
from enum import IntEnum

DEPENDENCIES = ["logger"]

NibeGwComponent = cg.global_ns.class_("NibeGwComponent", cg.Component)

CONF_DIR_PIN = "dir_pin"
CONF_TARGET_PORT = "target_port"
CONF_TARGET_IP = "target_ip"
CONF_ACKNOWLEDGE = "acknowledge"
CONF_UDP = "udp"

CONF_ACKNOWLEDGE_MODBUS40 = "modbus40"
CONF_ACKNOWLEDGE_RMU40 = "rmu40"
CONF_ACKNOWLEDGE_SMS40 = "sms40"
CONF_READ_PORT = "read_port"
CONF_WRITE_PORT = "write_port"

class Addresses(IntEnum):
    MODBUS40 = 0x20
    SMS40 = 0x16
    RMU40_S1 = 0x19
    RMU40_S2 = 0x1A
    RMU40_S3 = 0x1B
    RMU40_S4 = 0x1C


def addresses_string(value):
    try:
        return Addresses[value].value
    except KeyError:
        raise ValueError(f"{value} is not a valid member of Address")


UDP_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TARGET_IP): cv.ipv4,
        cv.Optional(CONF_TARGET_PORT, default=9999): cv.port,
        cv.Optional(CONF_READ_PORT, default=9999): cv.port,
        cv.Optional(CONF_WRITE_PORT, default=10000): cv.port,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NibeGwComponent),
        cv.Optional(CONF_ACKNOWLEDGE, default=[]): [cv.Any(addresses_string, cv.Coerce(int))],
        cv.Required(CONF_UDP): UDP_SCHEMA,
        cv.Optional(CONF_RX_PIN): pins.internal_gpio_input_pin_number,
        cv.Optional(CONF_TX_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_DIR_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_UART_ID, default=2): int,
        cv.Optional(CONF_DEBUG, default=False): cv.boolean
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

    if config[CONF_DEBUG]:
        cg.add_build_flag("-DENABLE_NIBE_DEBUG")

    cg.add_library("WiFi", None)
    cg.add_library("WiFiUdp", None)

    if udp := config.get(CONF_UDP):
        cg.add(var.set_target_ip(str(udp[CONF_TARGET_IP])))
        cg.add(var.set_target_port(udp[CONF_TARGET_PORT]))
        cg.add(var.set_read_port(udp[CONF_READ_PORT]))
        cg.add(var.set_write_port(udp[CONF_WRITE_PORT]))

    if config[CONF_ACKNOWLEDGE]:
        cg.add(var.gw().setSendAcknowledge(1))
        for address in config[CONF_ACKNOWLEDGE]:
            cg.add(
                var.gw().setAcknowledge(address, True)
            )
    else:
        cg.add(var.gw().setSendAcknowledge(0))
