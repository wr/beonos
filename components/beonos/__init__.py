# Beonos: Sonos speaker search and control, and the Beonos setup page.
#
# SPDX-License-Identifier: GPL-3.0-or-later
import esphome.codegen as cg
from esphome.components import binary_sensor, select, switch, text, text_sensor
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@wr"]
DEPENDENCIES = ["esp32", "wifi", "captive_portal", "network"]
AUTO_LOAD = ["binary_sensor", "select", "text", "text_sensor", "web_server_base"]

CONF_SPEAKER = "speaker"
CONF_MANUAL_ADDRESS = "manual_address"
CONF_STATUS = "status"
CONF_TRANSPORT_STATE = "transport_state"
CONF_CURRENT_URI = "current_uri"
CONF_PLAYING = "playing"
CONF_LINE_IN = "line_in"
CONF_SPEAKERS_FOUND = "speakers_found"
CONF_SETUP_WINDOW = "setup_window"
CONF_SETUP_SSID_PREFIX = "setup_ssid_prefix"
CONF_AUTO_UPDATE_SWITCH = "auto_update_switch"

beonos_ns = cg.global_ns.namespace("beonos")
BeonosComponent = beonos_ns.class_("BeonosComponent", cg.Component)
SpeakerSelect = beonos_ns.class_(
    "SpeakerSelect", select.Select, cg.Parented.template(BeonosComponent)
)
ManualAddressText = beonos_ns.class_(
    "ManualAddressText", text.Text, cg.Parented.template(BeonosComponent)
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BeonosComponent),
        cv.Required(CONF_SPEAKER): select.select_schema(
            SpeakerSelect, entity_category="config", icon="mdi:speaker"
        ),
        cv.Required(CONF_MANUAL_ADDRESS): text.text_schema(
            ManualAddressText, entity_category="config", icon="mdi:ip-network", mode="TEXT"
        ),
        cv.Required(CONF_STATUS): text_sensor.text_sensor_schema(icon="mdi:speaker-wireless"),
        cv.Optional(CONF_SPEAKERS_FOUND): text_sensor.text_sensor_schema(icon="mdi:speaker-multiple"),
        cv.Optional(CONF_TRANSPORT_STATE): text_sensor.text_sensor_schema(
            entity_category="diagnostic"
        ),
        cv.Optional(CONF_CURRENT_URI): text_sensor.text_sensor_schema(
            entity_category="diagnostic"
        ),
        cv.Required(CONF_PLAYING): binary_sensor.binary_sensor_schema(),
        cv.Required(CONF_LINE_IN): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_SETUP_WINDOW, default="15min"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SETUP_SSID_PREFIX, default="Beonos Setup"): cv.All(
            cv.string, cv.Length(max=25)
        ),
        cv.Optional(CONF_AUTO_UPDATE_SWITCH): cv.use_id(switch.Switch),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_http_client")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    sel = await select.new_select(config[CONF_SPEAKER], options=["Searching…"])
    await cg.register_parented(sel, var)
    cg.add(var.set_speaker_select(sel))

    txt = await text.new_text(config[CONF_MANUAL_ADDRESS], min_length=0, max_length=15)
    await cg.register_parented(txt, var)
    cg.add(var.set_manual_address_text(txt))

    sens = await text_sensor.new_text_sensor(config[CONF_STATUS])
    cg.add(var.set_status_sensor(sens))
    if CONF_SPEAKERS_FOUND in config:
        sens = await text_sensor.new_text_sensor(config[CONF_SPEAKERS_FOUND])
        cg.add(var.set_found_sensor(sens))
    if CONF_TRANSPORT_STATE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TRANSPORT_STATE])
        cg.add(var.set_transport_sensor(sens))
    if CONF_CURRENT_URI in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CURRENT_URI])
        cg.add(var.set_uri_sensor(sens))

    bs = await binary_sensor.new_binary_sensor(config[CONF_PLAYING])
    cg.add(var.set_playing_sensor(bs))
    bs = await binary_sensor.new_binary_sensor(config[CONF_LINE_IN])
    cg.add(var.set_line_in_sensor(bs))

    cg.add(var.set_setup_window(config[CONF_SETUP_WINDOW]))
    cg.add(var.set_setup_ssid_prefix(config[CONF_SETUP_SSID_PREFIX]))
    if CONF_AUTO_UPDATE_SWITCH in config:
        sw = await cg.get_variable(config[CONF_AUTO_UPDATE_SWITCH])
        cg.add(var.set_auto_update_switch(sw))
