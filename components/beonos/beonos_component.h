// Beonos: the ESPHome component that ties the board to a Sonos speaker.
//
// - Finds the speaker the turntable is plugged into and remembers it.
// - Exposes play / pause / switch-to-line-in for the bridge scripts in YAML,
//   and the speaker's state as binary sensors.
// - Serves the Beonos setup page on the setup network and closes setup a
//   while after power-up once the board has Wi-Fi.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#include "setup_wifi.h"
#include "sonos_client.h"
#include "sonos_parse.h"

namespace beonos {

class BeonosComponent;

class SpeakerSelect : public esphome::select::Select, public esphome::Parented<BeonosComponent> {
 protected:
  void control(const std::string &value) override;
};

class ManualAddressText : public esphome::text::Text, public esphome::Parented<BeonosComponent> {
 protected:
  void control(const std::string &value) override;
};

class SetupPageHandler;

class BeonosComponent : public esphome::Component {
 public:
  // Runs before Wi-Fi so the setup page handler is in place before the
  // captive portal registers its own.
  float get_setup_priority() const override { return esphome::setup_priority::WIFI + 5.0f; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Used by the bridge scripts.
  void play() { this->client_.send(Command::PLAY); }
  void pause() { this->client_.send(Command::PAUSE); }
  void switch_to_line_in() { this->client_.send(Command::SWITCH_TO_LINE_IN); }
  void search_now();
  bool is_playing() const { return this->playing_; }
  bool is_line_in() const { return this->line_in_; }
  bool has_speaker() const { return !this->target_.control_ip.empty(); }

  // Entities, set from codegen.
  void set_speaker_select(SpeakerSelect *s) { this->speaker_select_ = s; }
  void set_manual_address_text(ManualAddressText *t) { this->manual_address_text_ = t; }
  void set_status_sensor(esphome::text_sensor::TextSensor *s) { this->status_sensor_ = s; }
  void set_transport_sensor(esphome::text_sensor::TextSensor *s) { this->transport_sensor_ = s; }
  void set_uri_sensor(esphome::text_sensor::TextSensor *s) { this->uri_sensor_ = s; }
  void set_playing_sensor(esphome::binary_sensor::BinarySensor *s) { this->playing_sensor_ = s; }
  void set_line_in_sensor(esphome::binary_sensor::BinarySensor *s) { this->line_in_sensor_ = s; }
  void set_auto_update_switch(esphome::switch_::Switch *s) { this->auto_update_switch_ = s; }
  void set_setup_window(uint32_t ms) { this->setup_window_ms_ = ms; }
  void set_setup_ssid_prefix(const std::string &p) { this->setup_ssid_prefix_ = p; }

  // From the entities and the setup page.
  void on_speaker_picked(const std::string &room);
  void on_manual_address(const std::string &address);
  void on_setup_auto_update(bool on);
  // Called from the web server's task when the setup page saves Wi-Fi:
  // hands over to the main loop.
  void queue_setup_submitted(bool has_auto, bool auto_on) {
    this->defer([this, has_auto, auto_on]() { this->on_setup_submitted_(has_auto, auto_on); });
  }
  // What the setup page shows after Connect; safe to read from any task.
  const char *setup_wifi_state() const;
  bool auto_update_option() const { return this->auto_update_switch_ != nullptr; }
  bool auto_update_state() const;

 protected:
  struct SavedSpeaker {
    char uuid[48];
    char room[64];
  };
  struct SavedAddress {
    char address[16];
  };

  void apply_search_(const SearchResult &result);
  void apply_poll_(const PollResult &result);
  void apply_choice_();
  void rebuild_options_();
  void publish_status_(const std::string &status);
  void save_speaker_(const std::string &uuid, const std::string &room);
  void close_setup_window_();
  void on_setup_submitted_(bool has_auto, bool auto_on);
  void update_setup_wifi_state_();
  static setup::Drop classify_drop_(uint16_t reason);
  static void on_wifi_joined_(void *arg, const char *base, int32_t id, void *data);
  static void on_wifi_disconnected_(void *arg, const char *base, int32_t id, void *data);

  SonosClient client_;
  SetupPageHandler *setup_handler_{nullptr};

  SpeakerSelect *speaker_select_{nullptr};
  ManualAddressText *manual_address_text_{nullptr};
  esphome::text_sensor::TextSensor *status_sensor_{nullptr};
  esphome::text_sensor::TextSensor *transport_sensor_{nullptr};
  esphome::text_sensor::TextSensor *uri_sensor_{nullptr};
  esphome::binary_sensor::BinarySensor *playing_sensor_{nullptr};
  esphome::binary_sensor::BinarySensor *line_in_sensor_{nullptr};
  esphome::switch_::Switch *auto_update_switch_{nullptr};

  esphome::ESPPreferenceObject speaker_pref_;
  esphome::ESPPreferenceObject address_pref_;

  std::string saved_uuid_;
  std::string saved_room_;
  std::string manual_address_;

  // From the last search.
  bool searched_once_{false};
  std::vector<sonos::Group> groups_;
  std::vector<sonos::Candidate> candidates_;
  sonos::Choice choice_{sonos::Choice::NONE_FOUND};
  bool manual_failed_{false};

  // Room names shown in the select, and the speaker each one means. The
  // select keeps pointers into option_labels_, so it is only replaced
  // together with a new set_options() call.
  std::vector<std::string> option_labels_;
  std::vector<std::string> option_uuids_;

  sonos::Target target_;
  std::string learned_line_in_;

  bool playing_{false};
  bool line_in_{false};
  bool reachable_{false};
  std::string last_transport_;
  std::string last_uri_;

  bool network_was_up_{false};
  uint32_t network_up_since_{0};
  uint32_t last_search_{0};
  bool first_search_done_{false};

  // Result of the last Wi-Fi save from the setup page: a setup::WifiResult,
  // or SETUP_IDLE before the first save.
  static constexpr uint8_t SETUP_IDLE = 0xFF;
  std::atomic<uint8_t> setup_result_{SETUP_IDLE};
  uint32_t setup_attempt_started_{0};
  uint32_t joins_at_attempt_{0};
  uint32_t disconnects_at_attempt_{0};
  bool joined_since_attempt_{false};
  uint32_t joined_at_{0};
  uint32_t disconnects_at_join_{0};
  // Counted by the Wi-Fi driver's event task.
  std::atomic<uint32_t> joins_{0};
  std::atomic<uint32_t> disconnects_{0};
  std::atomic<uint16_t> last_disconnect_reason_{0};

  uint32_t setup_window_ms_{15 * 60 * 1000};
  bool setup_window_closed_{false};
  std::string setup_ssid_prefix_{"Beonos Setup"};
};

}  // namespace beonos
