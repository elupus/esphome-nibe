from operator import xor
from functools import reduce

import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import (
    CONF_ID,
    CONF_KEY,
    CONF_PORT,
)
from esphome import pins
from esphome.components.network import IPAddress
from enum import IntEnum, Enum
from esphome.components import noise, uart, socket
from esphome.types import ConfigType

DEPENDENCIES = ["logger"]

nibegw_ns = cg.esphome_ns.namespace("nibegw")
NibeGwComponent = nibegw_ns.class_("NibeGwComponent", cg.Component, uart.UARTDevice)
NibeGwTransport = nibegw_ns.class_("NibeGwTransport")
NibeGwUdpTransport = nibegw_ns.class_("NibeGwUdpTransport", NibeGwTransport)
NibeGwUdpNoiseTransport = nibegw_ns.class_("NibeGwUdpNoiseTransport", NibeGwTransport)

CONF_DIR_PIN = "dir_pin"
CONF_TARGET = "target"
CONF_TARGET_PORT = "port"
CONF_TARGET_IP = "ip"
CONF_ACKNOWLEDGE = "acknowledge"
CONF_UDP = "udp"
CONF_UDP_NOISE = "udp_noise"
CONF_REQUESTS = "requests"


def AUTO_LOAD(config: ConfigType) -> list[str]:
    components = ["sensor", "climate"]

    # Encryption is only needed for udp_noise. ESPHome probes with an
    # empty config to find all possible dependencies.
    if not config or CONF_UDP_NOISE in config:
        components.append("noise")

    return components


CONF_ACKNOWLEDGE_MODBUS40 = "modbus40"
CONF_ACKNOWLEDGE_RMU40 = "rmu40"
CONF_ACKNOWLEDGE_SMS40 = "sms40"
CONF_READ_PORT = "read_port"
CONF_WRITE_PORT = "write_port"
CONF_PORTS = "ports"
CONF_SOURCE = "source"
CONF_ADDRESS = "address"
CONF_TOKEN = "token"
CONF_COMMAND = "command"
CONF_DATA = "data"
CONF_CONSTANTS = "constants"


class Addresses(IntEnum):
    AXC40 = 0x05
    MODBUS40 = 0x20
    SMS40 = 0x16
    RMU40_S1 = 0x19
    RMU40_S2 = 0x1A
    RMU40_S3 = 0x1B
    RMU40_S4 = 0x1C
    DEH500 = 0x27
    EME20 = 0xA4


class Token(IntEnum):
    MODBUS_READ = 0x69
    MODBUS_WRITE = 0x6B
    RMU_WRITE = 0x60
    RMU_DATA = 0x63
    ACCESSORY = 0xEE


def addresses_string(value):
    try:
        return Addresses[value].value
    except KeyError:
        raise ValueError(f"{value} is not a valid member of Address")


def real_enum(enum: Enum):
    return cv.enum({i.name: i.value for i in enum})


def _consume_nibegw_sockets(config: ConfigType) -> ConfigType:
    """Register socket needs for nibegw component."""
    # MQTT needs 1 socket for the broker connection
    if udp := config.get(CONF_UDP):
        socket_count = len(udp[CONF_PORTS])
    else:
        socket_count = 1
    socket.consume_sockets(socket_count, "nibegw")(config)
    return config


def _upgrade_ports(config: ConfigType) -> ConfigType:
    udp = config.get(CONF_UDP)
    if udp is None:
        return config
    if port_number := udp[CONF_WRITE_PORT]:
        udp[CONF_PORTS].insert(
            0,
            PORTS_SCHEMA(
                {
                    CONF_PORT: port_number,
                    CONF_ADDRESS: Addresses.MODBUS40.value,
                    CONF_TOKEN: Token.MODBUS_WRITE.value,
                }
            ),
        )
    if port_number := udp[CONF_READ_PORT]:
        udp[CONF_PORTS].insert(
            0,
            PORTS_SCHEMA(
                {
                    CONF_PORT: port_number,
                    CONF_ADDRESS: Addresses.MODBUS40.value,
                    CONF_TOKEN: Token.MODBUS_READ.value,
                }
            ),
        )

    return config


CONSTANTS_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ADDRESS): cv.Any(real_enum(Addresses), int),
        cv.Required(CONF_TOKEN): cv.Any(real_enum(Token), int),
        cv.Optional(CONF_COMMAND): cv.Any(real_enum(Token), int),
        cv.Required(CONF_DATA): [int],
    }
)

TARGET_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TARGET_IP): cv.ipv4address,
        cv.Optional(CONF_TARGET_PORT, default=9999): cv.port,
    }
)

PORTS_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_ADDRESS): cv.Any(real_enum(Addresses), int),
        cv.Required(CONF_TOKEN): cv.Any(real_enum(Token), int),
    }
)

UDP_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NibeGwUdpTransport),
        cv.Optional(CONF_TARGET, []): cv.ensure_list(TARGET_SCHEMA),
        cv.Optional(CONF_READ_PORT, default=9999): cv.port,
        cv.Optional(CONF_WRITE_PORT, default=10000): cv.port,
        cv.Optional(CONF_SOURCE, []): cv.ensure_list(cv.ipv4address),
        cv.Optional(CONF_PORTS, []): cv.ensure_list(PORTS_SCHEMA),
    }
)

REQUEST_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ADDRESS): cv.Any(real_enum(Addresses), int),
        cv.Required(CONF_TOKEN): cv.Any(real_enum(Token), int),
    }
)


UDP_NOISE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NibeGwUdpNoiseTransport),
        cv.Required(CONF_KEY): cv.sensitive(noise.validate_encryption_key),
        cv.Optional(CONF_PORT, default=9999): cv.port,
        cv.Optional(
            CONF_REQUESTS,
            default=[
                {CONF_ADDRESS: "MODBUS40", CONF_TOKEN: "MODBUS_READ"},
                {CONF_ADDRESS: "MODBUS40", CONF_TOKEN: "MODBUS_WRITE"},
            ],
        ): cv.ensure_list(REQUEST_SCHEMA),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(NibeGwComponent),
            cv.Optional(CONF_ACKNOWLEDGE, default=[]): [
                cv.Any(addresses_string, cv.Coerce(int))
            ],
            cv.Optional(CONF_UDP): UDP_SCHEMA,
            cv.Optional(CONF_UDP_NOISE): UDP_NOISE_SCHEMA,
            cv.Optional(CONF_DIR_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_CONSTANTS, default=[]): cv.ensure_list(CONSTANTS_SCHEMA),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    cv.has_exactly_one_key(CONF_UDP, CONF_UDP_NOISE),
    _upgrade_ports,
    _consume_nibegw_sockets,
)


async def to_code(config):
    if dir_pin := config.get(CONF_DIR_PIN):
        dir_pin_data = await cg.gpio_pin_expression(dir_pin)
    else:
        dir_pin_data = 0

    var = cg.new_Pvariable(
        config[CONF_ID],
        dir_pin_data,
    )
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if udp := config.get(CONF_UDP):
        transport = cg.new_Pvariable(udp[CONF_ID])
        cg.add(var.set_transport(transport))

        for target in udp[CONF_TARGET]:
            cg.add(
                transport.add_target(
                    IPAddress(str(target[CONF_TARGET_IP])), target[CONF_TARGET_PORT]
                )
            )

        for port in udp[CONF_PORTS]:
            cg.add(
                transport.add_socket_request(
                    port[CONF_ADDRESS], port[CONF_TOKEN], port[CONF_PORT]
                )
            )

        for source in udp[CONF_SOURCE]:
            cg.add(transport.add_source_ip(IPAddress(str(source))))

    if udp_noise := config.get(CONF_UDP_NOISE):
        transport = cg.new_Pvariable(udp_noise[CONF_ID])
        cg.add(var.set_transport(transport))
        cg.add(transport.set_psk(noise.new_psk_progmem(udp_noise[CONF_KEY])))
        cg.add(transport.set_port(udp_noise[CONF_PORT]))

        for request in udp_noise[CONF_REQUESTS]:
            cg.add(
                transport.add_allowed_request(
                    request[CONF_ADDRESS], request[CONF_TOKEN]
                )
            )

    if config[CONF_ACKNOWLEDGE]:
        for address in config[CONF_ACKNOWLEDGE]:
            cg.add(var.add_acknowledge(address))

    def xor8(data: bytes) -> int:
        chksum = reduce(xor, data)
        if chksum == 0x5C:
            chksum = 0xC5
        return chksum

    def generate_request(command: int, data: list[int]) -> list[int]:
        packet = [0xC0, command, len(data), *data]
        packet.append(xor8(packet))
        return packet

    for request in config[CONF_CONSTANTS]:
        data = generate_request(
            request.get(CONF_COMMAND, request[CONF_TOKEN]).enum_value,
            request[CONF_DATA],
        )
        cg.add(var.set_request(request[CONF_ADDRESS], request[CONF_TOKEN], data))
