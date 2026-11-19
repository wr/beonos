// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_queue.h"

#include <algorithm>

namespace beonos {

void CommandQueue::push(Command command, uint32_t now_ms) {
  auto cancels = [command](const Entry &e) {
    if (e.command == command)
      return true;
    return (command == Command::PLAY && e.command == Command::PAUSE) ||
           (command == Command::PAUSE && e.command == Command::PLAY);
  };
  this->entries_.erase(std::remove_if(this->entries_.begin(), this->entries_.end(), cancels), this->entries_.end());
  if (this->entries_.size() >= MAX_COMMANDS)
    this->entries_.pop_front();
  this->entries_.push_back(Entry{command, now_ms});
}

void CommandQueue::drop_expired(uint32_t now_ms) {
  // Wrap-safe: compares the age, not the timestamps.
  while (!this->entries_.empty() && now_ms - this->entries_.front().at_ms > TTL_MS)
    this->entries_.pop_front();
}

bool CommandQueue::pop(uint32_t now_ms, Command &out) {
  this->drop_expired(now_ms);
  if (this->entries_.empty())
    return false;
  out = this->entries_.front().command;
  this->entries_.pop_front();
  return true;
}

}  // namespace beonos
