from operator import xor
from functools import reduce

import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import (
    CONF_ID,
    CONF_PORT,
)
from esphome import pins
from esphome.components.network import IPAddress
from enum import IntEnum, Enum
from esphome.components import uart, socket, sensor, number
from esphome.types import ConfigType

AUTO_LOAD = ["sensor", "climate", "number"]
DEPENDENCIES = ["logger"]

nibegw_ns = cg.esphome_ns.namespace("nibegw")
NibeGwComponent = nibegw_ns.class_("NibeGwComponent", cg.Component, uart.UARTDevice)
NibeGwCoilPoller = nibegw_ns.class_("NibeGwCoilPoller", cg.Component)
NibeGwCoilSensor = nibegw_ns.class_("NibeGwCoilSensor", sensor.Sensor, cg.Component)
NibeGwCoilNumber = nibegw_ns.class_("NibeGwCoilNumber", number.Number, cg.Component)

CONF_DIR_PIN = "dir_pin"
CONF_TARGET = "target"
CONF_TARGET_PORT = "port"
CONF_TARGET_IP = "ip"
CONF_ACKNOWLEDGE = "acknowledge"
CONF_UDP = "udp"
CONF_ENABLED = "enabled"

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

CONF_SENSORS = "sensors"
CONF_COILS = "coils"
CONF_WRITABLE = "writable"
CONF_SIZE = "size"
CONF_FACTOR = "factor"
CONF_POLL_INTERVAL = "poll_interval"
CONF_BUFFER_SIZE = "buffer_size"
CONF_BUFFER_MODE = "buffer_mode"
CONF_MIN_VALUE = "min_value"
CONF_MAX_VALUE = "max_value"
CONF_STEP = "step"
CONF_POLLER_ID = "poller_id"
CONF_POLL_GROUPS = "poll_groups"
CONF_POLL_GROUP = "poll_group"
CONF_INTERVAL = "interval"
CONF_GROUP_ID = "id"


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
    udp = config.get(CONF_UDP, {})
    if udp.get(CONF_ENABLED, False):
        socket_count = len(udp.get(CONF_PORTS, []))
        socket.consume_sockets(socket_count, "nibegw")(config)
    return config


def _upgrade_ports(config: ConfigType) -> ConfigType:
    udp = config.get(CONF_UDP, {})
    if not udp.get(CONF_ENABLED, False):
        return config
    if port_number := udp.get(CONF_WRITE_PORT):
        udp.setdefault(CONF_PORTS, []).insert(
            0,
            PORTS_SCHEMA(
                {
                    CONF_PORT: port_number,
                    CONF_ADDRESS: Addresses.MODBUS40.value,
                    CONF_TOKEN: Token.MODBUS_WRITE.value,
                }
            ),
        )
    if port_number := udp.get(CONF_READ_PORT):
        udp.setdefault(CONF_PORTS, []).insert(
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


def _validate_config(config: ConfigType) -> ConfigType:
    """Validate that at least one feature is enabled."""
    udp_enabled = config.get(CONF_UDP, {}).get(CONF_ENABLED, True)
    sensors_enabled = config.get(CONF_SENSORS, {}).get(CONF_ENABLED, False)
    if not udp_enabled and not sensors_enabled:
        raise cv.Invalid(
            "At least one of 'udp.enabled' or 'sensors.enabled' must be true"
        )
    return config


COIL_SIZES = {
    "u8": 0,
    "u16": 1,
    "s16": 2,
    "u32": 3,
    "s32": 4,
    "s8": 5,
}

BUFFER_MODES = {
    "off": 0,
    "latest_only": 1,
    "history": 2,
}

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
        cv.Optional(CONF_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_TARGET, []): cv.ensure_list(TARGET_SCHEMA),
        cv.Optional(CONF_READ_PORT, default=9999): cv.port,
        cv.Optional(CONF_WRITE_PORT, default=10000): cv.port,
        cv.Optional(CONF_SOURCE, []): cv.ensure_list(cv.ipv4address),
        cv.Optional(CONF_PORTS, []): cv.ensure_list(PORTS_SCHEMA),
    }
)

POLL_GROUP_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_GROUP_ID): cv.All(cv.string_strict, cv.Length(min=1)),
        cv.Required(CONF_INTERVAL): cv.positive_time_period_milliseconds,
    }
)

COIL_SENSOR_SCHEMA = sensor.sensor_schema(NibeGwCoilSensor).extend(
    {
        cv.Required(CONF_ADDRESS): cv.uint16_t,
        cv.Required(CONF_SIZE): cv.enum(COIL_SIZES, lower=True),
        cv.Optional(CONF_FACTOR, default=1): cv.positive_int,
        cv.Optional(CONF_POLL_GROUP, default="default"): cv.string_strict,
    }
).extend(cv.COMPONENT_SCHEMA)

COIL_NUMBER_SCHEMA = number.number_schema(NibeGwCoilNumber).extend(
    {
        cv.Required(CONF_ADDRESS): cv.uint16_t,
        cv.Required(CONF_SIZE): cv.enum(COIL_SIZES, lower=True),
        cv.Optional(CONF_FACTOR, default=1): cv.positive_int,
        cv.Required(CONF_MIN_VALUE): cv.float_,
        cv.Required(CONF_MAX_VALUE): cv.float_,
        cv.Optional(CONF_STEP, default=1.0): cv.positive_float,
        cv.Optional(CONF_POLL_GROUP, default="default"): cv.string_strict,
    }
).extend(cv.COMPONENT_SCHEMA)

SENSORS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ENABLED, default=False): cv.boolean,
        cv.GenerateID(CONF_POLLER_ID): cv.declare_id(NibeGwCoilPoller),
        cv.Optional(CONF_POLL_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_POLL_GROUPS, default=[]): cv.ensure_list(POLL_GROUP_SCHEMA),
        cv.Optional(CONF_BUFFER_SIZE, default=4096): cv.int_range(min=0, max=65536),
        cv.Optional(CONF_BUFFER_MODE, default="latest_only"): cv.enum(BUFFER_MODES, lower=True),
        cv.Optional(CONF_COILS, default=[]): cv.ensure_list(COIL_SENSOR_SCHEMA),
        cv.Optional(CONF_WRITABLE, default=[]): cv.ensure_list(COIL_NUMBER_SCHEMA),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(NibeGwComponent),
            cv.Optional(CONF_ACKNOWLEDGE, default=[]): [
                cv.Any(addresses_string, cv.Coerce(int))
            ],
            cv.Optional(CONF_UDP, default={}): UDP_SCHEMA,
            cv.Optional(CONF_SENSORS, default={}): SENSORS_SCHEMA,
            cv.Optional(CONF_DIR_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_CONSTANTS, default=[]): cv.ensure_list(CONSTANTS_SCHEMA),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    _upgrade_ports,
    _consume_nibegw_sockets,
    _validate_config,
)


async def to_code(config: ConfigType) -> None:
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

    # ── UDP Gateway ──
    udp = config.get(CONF_UDP, {})
    if udp.get(CONF_ENABLED, False):
        for target in udp.get(CONF_TARGET, []):
            cg.add(
                var.add_target(
                    IPAddress(str(target[CONF_TARGET_IP])), target[CONF_TARGET_PORT]
                )
            )

        for port in udp.get(CONF_PORTS, []):
            cg.add(
                var.add_socket_request(
                    port[CONF_ADDRESS], port[CONF_TOKEN], port[CONF_PORT]
                )
            )

        for source in udp.get(CONF_SOURCE, []):
            cg.add(var.add_source_ip(IPAddress(str(source))))

    # ── Acknowledge ──
    if config[CONF_ACKNOWLEDGE]:
        for address in config[CONF_ACKNOWLEDGE]:
            cg.add(var.add_acknowledge(address))

    # ── Constants ──
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

    # ── Sensors (direct coil polling) ──
    sensors_conf = config.get(CONF_SENSORS, {})
    if sensors_conf.get(CONF_ENABLED, False):
        poller = cg.new_Pvariable(sensors_conf[CONF_POLLER_ID])
        await cg.register_component(poller, sensors_conf)
        cg.add(poller.set_gw(var))
        cg.add(poller.set_poll_interval(sensors_conf[CONF_POLL_INTERVAL]))
        cg.add(poller.set_buffer_size(sensors_conf[CONF_BUFFER_SIZE]))
        cg.add(poller.set_buffer_mode(sensors_conf[CONF_BUFFER_MODE]))

        # Register explicit poll groups
        for group_conf in sensors_conf.get(CONF_POLL_GROUPS, []):
            cg.add(poller.add_poll_group(group_conf[CONF_GROUP_ID], group_conf[CONF_INTERVAL]))

        for coil_conf in sensors_conf.get(CONF_COILS, []):
            sens = await sensor.new_sensor(coil_conf)
            await cg.register_component(sens, coil_conf)
            cg.add(sens.set_poller(poller))
            cg.add(sens.set_address(coil_conf[CONF_ADDRESS]))
            cg.add(sens.set_coil_size(coil_conf[CONF_SIZE]))
            cg.add(sens.set_factor(coil_conf[CONF_FACTOR]))
            cg.add(sens.set_poll_group(coil_conf[CONF_POLL_GROUP]))

        for num_conf in sensors_conf.get(CONF_WRITABLE, []):
            num = await number.new_number(
                num_conf,
                min_value=num_conf[CONF_MIN_VALUE],
                max_value=num_conf[CONF_MAX_VALUE],
                step=num_conf[CONF_STEP],
            )
            await cg.register_component(num, num_conf)
            cg.add(num.set_gw(var))
            cg.add(num.set_poller(poller))
            cg.add(num.set_address(num_conf[CONF_ADDRESS]))
            cg.add(num.set_coil_size(num_conf[CONF_SIZE]))
            cg.add(num.set_factor(num_conf[CONF_FACTOR]))
            cg.add(num.set_poll_group(num_conf[CONF_POLL_GROUP]))
            cg.add(poller.register_writable(num))
