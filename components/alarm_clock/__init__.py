"""Alarm clock: two alarms set on the glass, a popup with Snooze and Stop when one goes off.

The alarms live in ESPHome entities of plugin.yaml (a time, a switch and a text of days for each), so they are kept on
the screen, and Home Assistant reads and sets the same values the card on the glass does. alarm_clock.cpp watches the
screen's clock, rings, and draws the tile, the card and the popup. register_plugin() takes the id, version, tile memory
and the screen's texts from the manifest and translations/ beside this folder.

On a screen with a speaker the part parts/sound.yaml hands it over as `speaker`, with its switch `sound_on`: an alarm
then rings with sounds/alarm.wav, built in by smart_display.sound() and played by the core's sound player.
"""
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, datetime, number, smart_display, speaker, switch, text, text_sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display", "binary_sensor", "datetime", "number", "switch", "text", "text_sensor"]

CONF_MEDIA_PLAYER = "media_player"
CONF_RINGING = "ringing"
CONF_NEXT = "next"
CONF_SNOOZE = "snooze"
CONF_SNOOZE_MINUTES = "snooze_minutes"
CONF_RING_MINUTES = "ring_minutes"
CONF_MEDIA = "media"
CONF_MEDIA_VOLUME = "media_volume"
CONF_ALARMS = "alarms"
CONF_TIME = "time"
CONF_ENABLED = "enabled"
CONF_DAYS = "days"
CONF_SPEAKER = "speaker"
CONF_SOUND_ON = "sound_on"
ALARM = Path(__file__).parent / "sounds" / "alarm.wav"

alarm_clock_ns = cg.esphome_ns.namespace("alarm_clock")
AlarmClock = alarm_clock_ns.class_("AlarmClock", cg.Component)


def player_name(value):
    """The media player's entity id, "" for none (an input nobody filled in arrives as nothing)."""
    return "" if value is None else cv.string(value)


def check_sound(config):
    if CONF_SPEAKER in config:
        smart_display.read_sound(ALARM)
    return config


ALARM_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TIME): cv.use_id(datetime.TimeEntity),
        cv.Required(CONF_ENABLED): cv.use_id(switch.Switch),
        cv.Required(CONF_DAYS): cv.use_id(text.Text),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AlarmClock),
            cv.Optional(CONF_MEDIA_PLAYER, default=""): player_name,
            cv.Required(CONF_RINGING): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_NEXT): cv.use_id(text_sensor.TextSensor),
            cv.Required(CONF_SNOOZE): cv.use_id(switch.Switch),
            cv.Required(CONF_SNOOZE_MINUTES): cv.use_id(number.Number),
            cv.Required(CONF_RING_MINUTES): cv.use_id(number.Number),
            cv.Required(CONF_MEDIA): cv.use_id(text.Text),
            cv.Required(CONF_MEDIA_VOLUME): cv.use_id(number.Number),
            cv.Required(CONF_ALARMS): cv.All(cv.ensure_list(ALARM_SCHEMA), cv.Length(min=2, max=2)),
            cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
            cv.Optional(CONF_SOUND_ON): cv.use_id(switch.Switch),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    check_sound,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    cg.add(var.set_media_player(config[CONF_MEDIA_PLAYER]))
    cg.add(var.set_ringing(await cg.get_variable(config[CONF_RINGING])))
    cg.add(var.set_next(await cg.get_variable(config[CONF_NEXT])))
    cg.add(var.set_snooze(await cg.get_variable(config[CONF_SNOOZE])))
    cg.add(var.set_snooze_minutes(await cg.get_variable(config[CONF_SNOOZE_MINUTES])))
    cg.add(var.set_ring_minutes(await cg.get_variable(config[CONF_RING_MINUTES])))
    cg.add(var.set_media(await cg.get_variable(config[CONF_MEDIA])))
    cg.add(var.set_media_volume(await cg.get_variable(config[CONF_MEDIA_VOLUME])))
    for index, alarm in enumerate(config[CONF_ALARMS]):
        cg.add(
            var.set_alarm(
                index,
                await cg.get_variable(alarm[CONF_TIME]),
                await cg.get_variable(alarm[CONF_ENABLED]),
                await cg.get_variable(alarm[CONF_DAYS]),
            )
        )
    if CONF_SPEAKER in config:
        cg.add(var.set_speaker(await cg.get_variable(config[CONF_SPEAKER])))
        cg.add(var.set_sound(smart_display.sound(config[CONF_ID], "alarm", ALARM)))
        if CONF_SOUND_ON in config:
            cg.add(var.set_sound_on(await cg.get_variable(config[CONF_SOUND_ON])))
