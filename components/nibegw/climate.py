import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor
from esphome.const import CONF_ID, CONF_SENSOR
from . import NibeGwComponent, nibegw_ns, real_enum, Addresses

AUTO_LOAD = ["sensor"]

NibeGwClimate = nibegw_ns.class_("NibeGwClimate", climate.Climate, cg.Component)

CONF_GATEWAY = "gateway"
CONF_ADDRESS = "address"

CONFIG_SCHEMA = cv.All(
    climate.CLIMATE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(NibeGwClimate),
            cv.GenerateID(CONF_GATEWAY): cv.use_id(NibeGwComponent),
            cv.Optional(CONF_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_ADDRESS): cv.Any(real_enum(Addresses), int),
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    gw = await cg.get_variable(config[CONF_GATEWAY])
    cg.add(var.set_gw(gw))
    cg.add(var.set_address(config[CONF_ADDRESS]))

    if CONF_SENSOR in config:
        sens = await cg.get_variable(config[CONF_SENSOR])
        cg.add(var.set_sensor(sens))
