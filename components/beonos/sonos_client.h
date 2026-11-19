// Beonos: all network traffic to Sonos speakers, run in its own FreeRTOS task
// so a slow or unplugged speaker never stalls the main loop (and with it the
// turntable button presses).
//
// The main loop talks to it only through the thread-safe methods below.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_ESP32

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "sonos_parse.h"

namespace beonos {

enum class Command : uint8_t { PLAY, PAUSE, SWITCH_TO_LINE_IN };

struct SearchResult {
  // At least one speaker answered.
  bool found_any{false};
  std::vector<sonos::Group> groups;
  std::map<std::string, sonos::DeviceInfo> devices;
  // When searching a hand-typed address: the speaker found there, if any.
  std::string manual_uuid;
};

struct PollResult {
  bool ok{false};
  std::string transport_state;
  std::string current_uri;
};

class SonosClient {
 public:
  bool start();

  void set_network_up(bool up) { this->network_up_ = up; }
  // Where to send commands and polls. An empty control_ip stops polling.
  void set_target(const std::string &control_ip, const std::string &line_in_uuid);
  // Runs a search: SSDP, or only the given address when manual_ip isn't empty.
  void request_search(const std::string &manual_ip);
  void send(Command command);

  bool take_search_result(SearchResult &out);
  bool take_poll_result(PollResult &out);

  uint32_t consecutive_failures() const { return this->failures_; }
  bool is_searching() const { return this->searching_; }

 protected:
  static void task_entry_(void *arg);
  void run_();
  void do_search_(const std::string &manual_ip);
  void do_poll_(const std::string &ip);
  void do_command_(Command command, const std::string &ip, const std::string &line_in_uuid);

  std::vector<std::string> ssdp_search_();
  bool fetch_description_(const std::string &ip, sonos::DeviceInfo &out);
  bool fetch_topology_(const std::string &ip, std::vector<sonos::Group> &out);
  bool soap_(const std::string &ip, const char *path, const char *service, const char *action,
             const std::string &args, std::string &response, size_t max_len);
  bool http_(const std::string &url, const char *soap_action, const std::string &body, std::string &response,
             size_t max_len);

  void record_poll_failure_();

  TaskHandle_t task_{nullptr};
  std::mutex mutex_;

  // Inputs, guarded by mutex_.
  std::string control_ip_;
  std::string line_in_uuid_;
  bool search_requested_{false};
  std::string search_manual_ip_;
  std::deque<Command> commands_;
  bool poll_now_{false};
  bool target_changed_{false};

  // Outputs, guarded by mutex_.
  bool has_search_result_{false};
  SearchResult search_result_;
  bool has_poll_result_{false};
  PollResult poll_result_;

  std::atomic<bool> network_up_{false};
  std::atomic<bool> searching_{false};
  std::atomic<uint32_t> failures_{0};

  // Task-only state.
  uint32_t next_poll_ms_{0};
  uint32_t backoff_ms_{0};
};

}  // namespace beonos

#endif  // USE_ESP32
