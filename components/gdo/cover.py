import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import binary_sensor, cover
from esphome.const import (
    CONF_CLOSE_DURATION,
    CONF_CLOSE_ENDSTOP,
    CONF_ID,
    CONF_OPEN_DURATION,
    CONF_OPEN_ENDSTOP,
)

gdo_ns = cg.esphome_ns.namespace("gdo")
GdoCover = gdo_ns.class_("GdoCover", cover.Cover, cg.Component)

CONF_SINGLE_PRESS_ACTION = "single_press_action"
CONF_DOUBLE_PRESS_ACTION = "double_press_action"
# How long double_press_action's own internal gap between its two relay
# clicks is. The door is mechanically stationary for this whole window (the
# first click just stops it; the second, later click is what actually starts
# it moving the new direction) -- so position tracking needs to sit out this
# same window, or it credits movement that hasn't physically happened yet.
# Keep this in sync with the delay you put inside double_press_action itself
# (plus a little for the relay's own press duration); it isn't derived
# automatically because the component can't introspect your actions.
CONF_DOUBLE_PRESS_SETTLE = "double_press_settle"
# Optional: a three-click automation for the one case double_press_action
# can't cover -- resuming the SAME direction the door was already moving in
# before an explicit stop. On a stop/reverse toggle motor that needs
# stop -> reverse -> stop -> reverse (3 presses), not stop -> reverse (2);
# a plain double press for that case just leaves the door stopped again
# after a brief flinch the wrong way, instead of actually continuing.
# If omitted, that edge case falls back to double_press_action (the old,
# incomplete behavior) with a logged warning.
CONF_TRIPLE_PRESS_ACTION = "triple_press_action"
# Same idea as double_press_settle, but for triple_press_action's two
# internal gaps -- the door isn't genuinely moving in the requested
# direction until the third click, so position tracking sits out until then.
CONF_TRIPLE_PRESS_SETTLE = "triple_press_settle"

CONFIG_SCHEMA = cv.All(
    cover.cover_schema(GdoCover)
    .extend(
        {
            cv.Optional(CONF_OPEN_ENDSTOP): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_OPEN_DURATION): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CLOSE_ENDSTOP): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_CLOSE_DURATION): cv.positive_time_period_milliseconds,
            cv.Required(CONF_SINGLE_PRESS_ACTION): automation.validate_automation(
                single=True
            ),
            cv.Required(CONF_DOUBLE_PRESS_ACTION): automation.validate_automation(
                single=True
            ),
            cv.Optional(
                CONF_DOUBLE_PRESS_SETTLE, default="0ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TRIPLE_PRESS_ACTION): automation.validate_automation(
                single=True
            ),
            cv.Optional(
                CONF_TRIPLE_PRESS_SETTLE, default="0ms"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    # With no endstop at all this degrades to a plain time-based cover that can
    # never correct its drift, which is almost certainly a config mistake.
    cv.has_at_least_one_key(CONF_OPEN_ENDSTOP, CONF_CLOSE_ENDSTOP),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)

    if CONF_OPEN_ENDSTOP in config:
        endstop = await cg.get_variable(config[CONF_OPEN_ENDSTOP])
        cg.add(var.set_open_endstop(endstop))
    cg.add(var.set_open_duration(config[CONF_OPEN_DURATION]))
    if CONF_CLOSE_ENDSTOP in config:
        endstop = await cg.get_variable(config[CONF_CLOSE_ENDSTOP])
        cg.add(var.set_close_endstop(endstop))
    cg.add(var.set_close_duration(config[CONF_CLOSE_DURATION]))
    cg.add(var.set_double_press_settle(config[CONF_DOUBLE_PRESS_SETTLE]))

    await automation.build_automation(
        var.get_single_press_trigger(), [], config[CONF_SINGLE_PRESS_ACTION]
    )
    await automation.build_automation(
        var.get_double_press_trigger(), [], config[CONF_DOUBLE_PRESS_ACTION]
    )

    if CONF_TRIPLE_PRESS_ACTION in config:
        cg.add(var.set_has_triple_press_action(True))
        cg.add(var.set_triple_press_settle(config[CONF_TRIPLE_PRESS_SETTLE]))
        await automation.build_automation(
            var.get_triple_press_trigger(), [], config[CONF_TRIPLE_PRESS_ACTION]
        )
