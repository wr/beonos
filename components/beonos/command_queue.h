// Beonos: commands waiting to go to the Sonos speaker.
//
// Pure code with no ESPHome or ESP-IDF dependencies, so it can be
// unit-tested on a computer (see tests/).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace beonos {

enum class Command : uint8_t { PLAY, PAUSE, SWITCH_TO_LINE_IN };

// The latest intent wins: Play replaces a waiting Pause and the other way
// round, and a repeated command moves to the back. Commands that waited
// longer than TTL_MS (the speaker was away, or Wi-Fi was down) are dropped,
// so a press nobody remembers doesn't fire minutes later.
class CommandQueue {
 public:
  static constexpr uint32_t TTL_MS = 10000;
  static constexpr size_t MAX_COMMANDS = 4;

  void push(Command command, uint32_t now_ms);
  // Drops expired commands, then takes the oldest remaining one.
  bool pop(uint32_t now_ms, Command &out);
  void drop_expired(uint32_t now_ms);
  size_t size() const { return this->entries_.size(); }

 protected:
  struct Entry {
    Command command;
    uint32_t at_ms;
  };
  std::deque<Entry> entries_;
};

}  // namespace beonos
