import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor
from esphome.const import CONF_SENSOR
from . import NibeGwComponent, nibegw_ns

AUTO_LOAD = ["sensor"]

NibeGwClimate = nibegw_ns.class_("NibeGwClimate", climate.Climate, cg.Component)

CONF_GATEWAY = "gateway"
CONF_SYSTEM = "system"

CONFIG_SCHEMA = (
    climate.climate_schema(NibeGwClimate)
    .extend(
        {
            cv.GenerateID(CONF_GATEWAY): cv.use_id(NibeGwComponent),
            cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_SYSTEM): cv.int_range(min=1, max=4),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    gw = await cg.get_variable(config[CONF_GATEWAY])
    cg.add(var.set_gw(gw))
    cg.add(var.set_system(config[CONF_SYSTEM]))

    sens = await cg.get_variable(config[CONF_SENSOR])
    cg.add(var.set_sensor(sens))
