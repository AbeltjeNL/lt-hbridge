from esphome import pins
import esphome.codegen as cg
from esphome.components import libretiny, light
from esphome.components.libretiny.const import FAMILY_BK7231N
import esphome.config_validation as cv
from esphome.const import (
    CONF_CONSTANT_BRIGHTNESS,
    CONF_FREQUENCY,
    CONF_INVERTED,
    CONF_NUMBER,
    CONF_OUTPUT_ID,
    CONF_PIN_A,
    CONF_PIN_B,
)

from .. import lt_hbridge_ns

CODEOWNERS = ["@LeoDJ"]
DEPENDENCIES = ["libretiny"]

CONF_DEAD_TIME = "dead_time"

LTHBridgeLightOutput = lt_hbridge_ns.class_(
    "LTHBridgeLightOutput", cg.Component, light.LightOutput
)

# BK7231N: GPIO -> PWM channel (same table as LibreTiny's wiring_analog.c).
# Two channels form one PWM group: channel // 2 == group.
PWM_CHANNELS = {6: 0, 7: 1, 8: 2, 9: 3, 24: 4, 26: 5}


def _validate_hbridge(config):
    channels = []
    for key in (CONF_PIN_A, CONF_PIN_B):
        pin = config[key]
        number = pin[CONF_NUMBER]
        if number not in PWM_CHANNELS:
            raise cv.Invalid(
                f"P{number} is not a PWM capable pin, use one of "
                + ", ".join(f"P{n}" for n in PWM_CHANNELS),
                path=[key],
            )
        if pin.get(CONF_INVERTED, False):
            raise cv.Invalid(
                "Inverted pins are not supported by lt_hbridge", path=[key, CONF_INVERTED]
            )
        channels.append(PWM_CHANNELS[number])

    if channels[0] // 2 == channels[1] // 2:
        raise cv.Invalid(
            "pin_a and pin_b must be in different PWM groups "
            "(G0: P6/P7, G1: P8/P9, G2: P24/P26)",
            path=[CONF_PIN_B],
        )

    period_us = 1e6 / config[CONF_FREQUENCY]
    dead_time_us = config[CONF_DEAD_TIME].total_microseconds
    if 2 * dead_time_us >= period_us / 2:
        raise cv.Invalid(
            f"dead_time ({dead_time_us}us) is too long for {config[CONF_FREQUENCY]:.0f}Hz, "
            f"it must be shorter than {period_us / 4:.0f}us",
            path=[CONF_DEAD_TIME],
        )
    return config


CONFIG_SCHEMA = cv.All(
    light.light_schema(LTHBridgeLightOutput, light.LightType.RGB)
    .extend(
        {
            cv.Required(CONF_PIN_A): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_PIN_B): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_FREQUENCY, default="1kHz"): cv.All(
                cv.frequency, cv.float_range(min=100, max=20000)
            ),
            # 10us on each side keeps the original 98% maximum duty at 1kHz
            # must be > 0: otherwise pin_b switches on in the same cycle pin_a switches off
            cv.Optional(CONF_DEAD_TIME, default="10us"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(min=cv.TimePeriod(microseconds=1)),
            ),
            cv.Optional(CONF_CONSTANT_BRIGHTNESS, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    libretiny.only_on_family(supported=FAMILY_BK7231N),
    _validate_hbridge,
)


async def to_code(config):
    gpio_a = await cg.gpio_pin_expression(config[CONF_PIN_A])
    gpio_b = await cg.gpio_pin_expression(config[CONF_PIN_B])
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], gpio_a, gpio_b)
    await cg.register_component(var, config)
    await light.register_light(var, config)

    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
    cg.add(var.set_dead_time(config[CONF_DEAD_TIME].total_microseconds))
    cg.add(var.set_constant_brightness(config[CONF_CONSTANT_BRIGHTNESS]))
