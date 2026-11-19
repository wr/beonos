// SPDX-License-Identifier: GPL-3.0-or-later
#include "beonos_component.h"

#include <algorithm>
#include <cstring>

#include "esphome/components/captive_portal/captive_portal.h"
#include "esphome/components/network/util.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32_BLE
#include "esphome/components/esp32_ble/ble.h"
#endif

#include "setup_page.h"

namespace beonos {

using namespace esphome;

static const char *const TAG = "beonos";

static constexpr uint32_t FIRST_SEARCH_DELAY_MS = 5000;
static constexpr uint32_t SEARCH_INTERVAL_MS = 10 * 60 * 1000;
static constexpr uint32_t MIN_SEARCH_GAP_MS = 60 * 1000;
static constexpr uint32_t FAILURES_BEFORE_SEARCH = 5;

static const char *const OPTION_SEARCHING = "Searching\xE2\x80\xA6";
static const char *const OPTION_CHOOSE = "Choose\xE2\x80\xA6";
static const char *const OPTION_NONE = "None found";

static const char *const STATUS_SEARCHING = "Searching for Sonos speakers\xE2\x80\xA6";
static const char *const STATUS_NONE =
    "No Sonos speaker with a line-in found. If you have one, check that UPnP is on in the Sonos app "
    "(Account > Privacy & Security).";
static const char *const STATUS_CHOOSE = "More than one speaker has a line-in. Choose yours under Sonos speaker.";

// ── Setup page ──────────────────────────────────────────────────────────────

class SetupPageHandler : public AsyncWebHandler {
 public:
  explicit SetupPageHandler(BeonosComponent *parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    auto *portal = captive_portal::global_captive_portal;
    if (portal == nullptr || !portal->is_active() || request->method() != HTTP_GET)
      return false;
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    // The captive portal keeps answering its own network list.
    return request->url_to(url_buf) != "/config.json";
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buf);
    if (url == "/wifisave") {
      if (request->hasArg("auto")) {
        bool on = request->arg("auto") == "1";
        this->parent_->queue_setup_auto_update(on);
      }
      captive_portal::global_captive_portal->handle_wifisave(request);
      return;
    }
    if (url == "/beonos.json") {
      std::string json = std::string("{\"auto_update_option\":") +
                         (this->parent_->auto_update_option() ? "true" : "false") +
                         ",\"auto_update\":" + (this->parent_->auto_update_state() ? "true" : "false") + "}";
      request->send(200, "application/json", json.c_str());
      return;
    }
    auto *response = request->beginResponse(200, "text/html", reinterpret_cast<const uint8_t *>(SETUP_PAGE),
                                            sizeof(SETUP_PAGE) - 1);
    response->addHeader("cache-control", "no-store");
    request->send(response);
  }

 protected:
  BeonosComponent *parent_;
};

// ── Entities ────────────────────────────────────────────────────────────────

void SpeakerSelect::control(const std::string &value) { this->parent_->on_speaker_picked(value); }

void ManualAddressText::control(const std::string &value) { this->parent_->on_manual_address(value); }

// ── Component ───────────────────────────────────────────────────────────────

void BeonosComponent::setup() {
  this->speaker_pref_ = global_preferences->make_preference<SavedSpeaker>(fnv1_hash("beonos_speaker_v1"), true);
  this->address_pref_ = global_preferences->make_preference<SavedAddress>(fnv1_hash("beonos_address_v1"), true);

  SavedSpeaker speaker{};
  if (this->speaker_pref_.load(&speaker)) {
    speaker.uuid[sizeof(speaker.uuid) - 1] = '\0';
    speaker.room[sizeof(speaker.room) - 1] = '\0';
    this->saved_uuid_ = speaker.uuid;
    this->saved_room_ = speaker.room;
  }
  SavedAddress address{};
  if (this->address_pref_.load(&address)) {
    address.address[sizeof(address.address) - 1] = '\0';
    this->manual_address_ = address.address;
  }
  if (this->manual_address_text_ != nullptr)
    this->manual_address_text_->publish_state(this->manual_address_);

  // "Beonos Setup 80a7fc": the same six characters as the board's name.
  if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->has_ap()) {
    uint8_t mac[6];
    get_mac_address_raw(mac);
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    auto ap = wifi::global_wifi_component->get_ap();
    ap.set_ssid(this->setup_ssid_prefix_ + " " + suffix);
    wifi::global_wifi_component->set_ap(ap);
  }

  if (web_server_base::global_web_server_base != nullptr) {
    this->setup_handler_ = new SetupPageHandler(this);  // NOLINT(cppcoreguidelines-owning-memory)
    web_server_base::global_web_server_base->add_handler_without_auth(this->setup_handler_);
  }

  this->publish_status_(STATUS_SEARCHING);
  this->rebuild_options_();
  if (this->playing_sensor_ != nullptr)
    this->playing_sensor_->publish_initial_state(false);
  if (this->line_in_sensor_ != nullptr)
    this->line_in_sensor_->publish_initial_state(false);

  if (!this->client_.start()) {
    ESP_LOGE(TAG, "Couldn't start the Sonos task");
    this->mark_failed();
  }
}

void BeonosComponent::loop() {
  const uint32_t now = millis();

  bool up = network::is_connected();
  this->client_.set_network_up(up);
  if (up && !this->network_was_up_)
    this->network_up_since_ = now;
  this->network_was_up_ = up;

  if (up) {
    bool due = false;
    if (!this->first_search_done_) {
      due = now - this->network_up_since_ > FIRST_SEARCH_DELAY_MS;
    } else if (now - this->last_search_ > SEARCH_INTERVAL_MS) {
      due = true;
    } else if (this->client_.consecutive_failures() >= FAILURES_BEFORE_SEARCH &&
               now - this->last_search_ > MIN_SEARCH_GAP_MS) {
      due = true;  // the speaker may have a new address
    }
    if (due && !this->client_.is_searching()) {
      this->first_search_done_ = true;
      this->last_search_ = now;
      this->client_.request_search(this->manual_address_);
    }
  }

  SearchResult search;
  if (this->client_.take_search_result(search))
    this->apply_search_(search);
  PollResult poll;
  if (this->client_.take_poll_result(poll))
    this->apply_poll_(poll);

  if (!this->setup_window_closed_ && now > this->setup_window_ms_ && wifi::global_wifi_component != nullptr &&
      wifi::global_wifi_component->has_sta()) {
    this->close_setup_window_();
  }
}

void BeonosComponent::search_now() {
  this->last_search_ = millis();
  this->first_search_done_ = true;
  this->publish_status_(STATUS_SEARCHING);
  this->client_.request_search(this->manual_address_);
}

void BeonosComponent::close_setup_window_() {
  this->setup_window_closed_ = true;
  // No fallback setup network from now on. One that is already up (the board
  // never reached its Wi-Fi since power-up) stays until the board connects.
  wifi::global_wifi_component->set_ap_timeout(0);
#ifdef USE_ESP32_BLE
  if (esp32_ble::global_ble != nullptr)
    esp32_ble::global_ble->disable();
#endif
  ESP_LOGI(TAG, "Setup window closed; switch the turntable off and on to reopen it");
}

void BeonosComponent::apply_search_(const SearchResult &result) {
  this->searched_once_ = true;
  this->groups_ = result.groups;

  if (!this->manual_address_.empty()) {
    this->manual_failed_ = result.manual_uuid.empty();
    this->candidates_.clear();
    if (!this->manual_failed_) {
      std::string room;
      for (const auto &g : result.groups)
        for (const auto &m : g.members)
          if (m.uuid == result.manual_uuid)
            room = m.room;
      this->candidates_.push_back(sonos::Candidate{result.manual_uuid, room, this->manual_address_});
      this->choice_ = sonos::Choice::KEEP_SAVED;
    } else {
      this->choice_ = sonos::Choice::NONE_FOUND;
    }
  } else {
    this->manual_failed_ = false;
    this->candidates_ = sonos::line_in_candidates(result.groups, result.devices);
    std::string picked;
    this->choice_ = sonos::choose_speaker(this->candidates_, this->saved_uuid_, picked);
    if (this->choice_ == sonos::Choice::AUTO_PICKED) {
      for (const auto &c : this->candidates_)
        if (c.uuid == picked)
          this->save_speaker_(c.uuid, c.room);
      ESP_LOGI(TAG, "Picked the only speaker with a line-in: %s", this->saved_room_.c_str());
    }
    // Keep the saved room name current if the speaker was renamed.
    for (const auto &c : this->candidates_)
      if (c.uuid == this->saved_uuid_ && c.room != this->saved_room_)
        this->save_speaker_(c.uuid, c.room);
  }
  ESP_LOGI(TAG, "Search done: %u candidate(s)", static_cast<unsigned>(this->candidates_.size()));
  this->rebuild_options_();
  this->apply_choice_();
}

void BeonosComponent::apply_choice_() {
  std::string chosen = this->manual_address_.empty()
                           ? this->saved_uuid_
                           : (this->candidates_.empty() ? std::string() : this->candidates_[0].uuid);
  sonos::Target target;
  if (!chosen.empty() && sonos::resolve_target(this->groups_, chosen, target)) {
    this->target_ = target;
  } else if (!this->manual_address_.empty() && !chosen.empty()) {
    this->target_ = sonos::Target{this->manual_address_, chosen};
  } else if (chosen.empty() || this->target_.line_in_uuid != chosen) {
    // Nothing chosen, or a new choice that this search didn't see: wait for
    // the next search. An unchanged choice keeps its last known address.
    this->target_ = sonos::Target{};
  }

  // A learned line-in (another member of the chosen speaker's pair or group)
  // wins over the chosen speaker's own one.
  std::string line_in = this->target_.line_in_uuid;
  if (!this->learned_line_in_.empty()) {
    for (const auto &g : this->groups_) {
      bool has_chosen = false, has_learned = false;
      for (const auto &m : g.members) {
        has_chosen |= m.uuid == chosen;
        has_learned |= m.uuid == this->learned_line_in_;
      }
      if (has_chosen && has_learned)
        line_in = this->learned_line_in_;
    }
  }
  this->client_.set_target(this->target_.control_ip, line_in);

  if (this->target_.control_ip.empty()) {
    this->reachable_ = false;
    if (this->playing_) {
      this->playing_ = false;
      if (this->playing_sensor_ != nullptr)
        this->playing_sensor_->publish_state(false);
    }
    if (this->line_in_) {
      this->line_in_ = false;
      if (this->line_in_sensor_ != nullptr)
        this->line_in_sensor_->publish_state(false);
    }
  }

  // Status line.
  if (!this->manual_address_.empty()) {
    this->publish_status_(this->manual_failed_ ? "No Sonos speaker answers at " + this->manual_address_ + "."
                                               : "Using " + this->candidates_[0].room + " at " +
                                                     this->manual_address_ + ".");
  } else if (!this->saved_uuid_.empty()) {
    bool seen = std::any_of(this->candidates_.begin(), this->candidates_.end(),
                            [this](const sonos::Candidate &c) { return c.uuid == this->saved_uuid_; });
    this->publish_status_(seen || this->reachable_ ? "Using " + this->saved_room_ + "."
                                                   : this->saved_room_ + " isn't answering. Beonos keeps trying.");
  } else if (this->choice_ == sonos::Choice::NEEDS_USER) {
    this->publish_status_(STATUS_CHOOSE);
  } else {
    this->publish_status_(STATUS_NONE);
  }
}

void BeonosComponent::apply_poll_(const PollResult &result) {
  bool was_reachable = this->reachable_;
  this->reachable_ = result.ok;
  if (!result.ok) {
    // Keep the last known state: a network blip must not look like Sonos
    // pausing, which would stop the record.
    if (was_reachable && !this->saved_room_.empty() && this->manual_address_.empty())
      this->publish_status_(this->saved_room_ + " isn't answering. Beonos keeps trying.");
    return;
  }
  if (!was_reachable && this->searched_once_)
    this->apply_choice_();

  if (result.transport_state != this->last_transport_) {
    this->last_transport_ = result.transport_state;
    if (this->transport_sensor_ != nullptr)
      this->transport_sensor_->publish_state(result.transport_state);
  }
  if (result.current_uri != this->last_uri_) {
    this->last_uri_ = result.current_uri;
    if (this->uri_sensor_ != nullptr)
      this->uri_sensor_->publish_state(result.current_uri);

    // Someone chose a line-in in the Sonos app: if it belongs to the chosen
    // speaker's pair or group, that's where the turntable is plugged in.
    std::string source = sonos::line_in_source(result.current_uri);
    if (!source.empty() && source != this->learned_line_in_) {
      this->learned_line_in_ = source;
      this->apply_choice_();
    }
    // The polled speaker now follows another room: find the new leader.
    if (result.current_uri.compare(0, 9, "x-rincon:") == 0 && !this->client_.is_searching() &&
        millis() - this->last_search_ > 10000) {
      this->last_search_ = millis();
      this->client_.request_search(this->manual_address_);
    }
  }

  bool playing = result.transport_state == "PLAYING";
  bool line_in = sonos::is_line_in_uri(result.current_uri);
  if (line_in != this->line_in_) {
    this->line_in_ = line_in;
    if (this->line_in_sensor_ != nullptr)
      this->line_in_sensor_->publish_state(line_in);
  }
  if (playing != this->playing_) {
    this->playing_ = playing;
    if (this->playing_sensor_ != nullptr)
      this->playing_sensor_->publish_state(playing);
  }
}

void BeonosComponent::rebuild_options_() {
  if (this->speaker_select_ == nullptr)
    return;
  std::vector<std::string> labels, uuids;
  size_t selected = 0;

  if (!this->searched_once_) {
    if (!this->saved_room_.empty() && this->manual_address_.empty()) {
      labels.push_back(this->saved_room_);
      uuids.push_back(this->saved_uuid_);
    } else {
      labels.push_back(OPTION_SEARCHING);
      uuids.emplace_back();
    }
  } else {
    if (this->manual_address_.empty() && this->saved_uuid_.empty() && this->candidates_.size() > 1) {
      labels.push_back(OPTION_CHOOSE);
      uuids.emplace_back();
    }
    for (const auto &c : this->candidates_) {
      std::string label = c.room.empty() ? c.uuid : c.room;
      // Two rooms with the same name still need to be told apart.
      int n = 2;
      std::string unique = label;
      while (std::find(labels.begin(), labels.end(), unique) != labels.end())
        unique = label + " (" + std::to_string(n++) + ")";
      labels.push_back(unique);
      uuids.push_back(c.uuid);
    }
    if (this->manual_address_.empty() && !this->saved_uuid_.empty() &&
        std::find(uuids.begin(), uuids.end(), this->saved_uuid_) == uuids.end()) {
      labels.push_back(this->saved_room_.empty() ? this->saved_uuid_ : this->saved_room_);
      uuids.push_back(this->saved_uuid_);
    }
    if (labels.empty()) {
      labels.push_back(OPTION_NONE);
      uuids.emplace_back();
    }
  }

  std::string chosen = this->manual_address_.empty() ? this->saved_uuid_
                                                     : (uuids.empty() ? std::string() : uuids.front());
  for (size_t i = 0; i < uuids.size(); i++)
    if (!chosen.empty() && uuids[i] == chosen)
      selected = i;

  this->option_labels_ = std::move(labels);
  this->option_uuids_ = std::move(uuids);
  FixedVector<const char *> options;
  options.init(this->option_labels_.size());
  for (const auto &l : this->option_labels_)
    options.push_back(l.c_str());
  this->speaker_select_->traits.set_options(options);
  this->speaker_select_->publish_state(selected);
}

void BeonosComponent::on_speaker_picked(const std::string &room) {
  for (size_t i = 0; i < this->option_labels_.size(); i++) {
    if (this->option_labels_[i] != room)
      continue;
    if (this->option_uuids_[i].empty() || !this->manual_address_.empty())
      break;  // a placeholder, or the address box decides
    ESP_LOGI(TAG, "Speaker chosen: %s", room.c_str());
    this->save_speaker_(this->option_uuids_[i], room);
    this->learned_line_in_.clear();
    this->rebuild_options_();
    this->apply_choice_();
    return;
  }
  this->rebuild_options_();
}

void BeonosComponent::on_manual_address(const std::string &address) {
  std::string a = address;
  a.erase(std::remove_if(a.begin(), a.end(), [](char c) { return c == ' '; }), a.end());
  bool valid = a.empty() || (a.size() < sizeof(SavedAddress::address) &&
                             std::all_of(a.begin(), a.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; }) &&
                             std::count(a.begin(), a.end(), '.') == 3);
  if (!valid) {
    ESP_LOGW(TAG, "Ignoring speaker address '%s'", address.c_str());
    if (this->manual_address_text_ != nullptr)
      this->manual_address_text_->publish_state(this->manual_address_);
    return;
  }
  this->manual_address_ = a;
  SavedAddress saved{};
  strncpy(saved.address, a.c_str(), sizeof(saved.address) - 1);
  this->address_pref_.save(&saved);
  if (this->manual_address_text_ != nullptr)
    this->manual_address_text_->publish_state(a);
  this->learned_line_in_.clear();
  this->search_now();
}

void BeonosComponent::on_setup_auto_update(bool on) {
  if (this->auto_update_switch_ == nullptr)
    return;
  ESP_LOGI(TAG, "Automatic updates %s from setup", on ? "on" : "off");
  if (on)
    this->auto_update_switch_->turn_on();
  else
    this->auto_update_switch_->turn_off();
}

bool BeonosComponent::auto_update_state() const {
  return this->auto_update_switch_ == nullptr || this->auto_update_switch_->state;
}

void BeonosComponent::save_speaker_(const std::string &uuid, const std::string &room) {
  this->saved_uuid_ = uuid;
  this->saved_room_ = room;
  SavedSpeaker saved{};
  strncpy(saved.uuid, uuid.c_str(), sizeof(saved.uuid) - 1);
  strncpy(saved.room, room.c_str(), sizeof(saved.room) - 1);
  this->speaker_pref_.save(&saved);
}

void BeonosComponent::publish_status_(const std::string &status) {
  if (this->status_sensor_ != nullptr && this->status_sensor_->get_state() != status)
    this->status_sensor_->publish_state(status);
}

void BeonosComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Beonos:\n"
                "  Saved speaker: %s\n"
                "  Speaker address (advanced): %s\n"
                "  Setup window: %" PRIu32 " s\n"
                "  Automatic update option: %s",
                this->saved_room_.empty() ? "(none)" : this->saved_room_.c_str(),
                this->manual_address_.empty() ? "(none)" : this->manual_address_.c_str(),
                this->setup_window_ms_ / 1000, YESNO(this->auto_update_switch_ != nullptr));
}

}  // namespace beonos
