// Host tests for components/beonos/command_queue.cpp. Run with tests/run.sh.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_queue.h"

#include <cstdio>
#include <functional>
#include <vector>

using namespace beonos;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    ++g_checks;                                                        \
    if (!(cond)) {                                                     \
      ++g_failures;                                                    \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                  \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

static std::vector<Command> drain(CommandQueue &q, uint32_t now) {
  std::vector<Command> out;
  Command c;
  while (q.pop(now, c))
    out.push_back(c);
  return out;
}

static void test_switch_then_play_keep_order() {
  CommandQueue q;
  q.push(Command::SWITCH_TO_LINE_IN, 0);
  q.push(Command::PLAY, 0);
  CHECK((drain(q, 10) == std::vector<Command>{Command::SWITCH_TO_LINE_IN, Command::PLAY}));
}

static void test_latest_play_or_pause_wins() {
  // Play, then Pause, then Play again before anything went out: the speaker
  // should end up playing.
  CommandQueue q;
  q.push(Command::PLAY, 0);
  q.push(Command::PAUSE, 10);
  q.push(Command::PLAY, 20);
  CHECK((drain(q, 30) == std::vector<Command>{Command::PLAY}));
}

static void test_pause_cancels_waiting_play_but_keeps_switch() {
  CommandQueue q;
  q.push(Command::SWITCH_TO_LINE_IN, 0);
  q.push(Command::PLAY, 0);
  q.push(Command::PAUSE, 5);
  CHECK((drain(q, 10) == std::vector<Command>{Command::SWITCH_TO_LINE_IN, Command::PAUSE}));
}

static void test_repeat_moves_to_back() {
  CommandQueue q;
  q.push(Command::SWITCH_TO_LINE_IN, 0);
  q.push(Command::PLAY, 0);
  q.push(Command::SWITCH_TO_LINE_IN, 5);
  CHECK((drain(q, 10) == std::vector<Command>{Command::PLAY, Command::SWITCH_TO_LINE_IN}));
}

static void test_old_commands_expire() {
  CommandQueue q;
  q.push(Command::SWITCH_TO_LINE_IN, 1000);
  q.push(Command::PLAY, 1000);
  CHECK_EQ(q.size(), 2u);
  // Still there just inside the limit...
  q.drop_expired(1000 + CommandQueue::TTL_MS);
  CHECK_EQ(q.size(), 2u);
  // ...gone after it: the speaker came back hours later and nothing fires.
  Command c;
  CHECK(!q.pop(1000 + CommandQueue::TTL_MS + 1, c));
  CHECK_EQ(q.size(), 0u);
}

static void test_expiry_survives_clock_wrap() {
  // millis() wraps after 49.7 days; age must still be measured right.
  CommandQueue q;
  const uint32_t before_wrap = 0xFFFFFF00u;
  q.push(Command::PLAY, before_wrap);
  Command c;
  CHECK(q.pop(before_wrap + 500, c));  // 500 ms later, past the wrap
  CHECK(c == Command::PLAY);
  q.push(Command::PAUSE, before_wrap);
  CHECK(!q.pop(before_wrap + CommandQueue::TTL_MS + 1, c));
}

static void test_never_holds_more_than_max() {
  CommandQueue q;
  q.push(Command::SWITCH_TO_LINE_IN, 0);
  q.push(Command::PLAY, 0);
  q.push(Command::SWITCH_TO_LINE_IN, 1);
  q.push(Command::PAUSE, 2);
  CHECK(q.size() <= CommandQueue::MAX_COMMANDS);
}

int main() {
  const std::vector<std::pair<const char *, std::function<void()>>> tests = {
      {"switch_then_play_keep_order", test_switch_then_play_keep_order},
      {"latest_play_or_pause_wins", test_latest_play_or_pause_wins},
      {"pause_cancels_waiting_play_but_keeps_switch", test_pause_cancels_waiting_play_but_keeps_switch},
      {"repeat_moves_to_back", test_repeat_moves_to_back},
      {"old_commands_expire", test_old_commands_expire},
      {"expiry_survives_clock_wrap", test_expiry_survives_clock_wrap},
      {"never_holds_more_than_max", test_never_holds_more_than_max},
  };
  for (auto &t : tests) {
    int before = g_failures;
    t.second();
    std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.first);
  }
  if (g_failures) {
    std::printf("%d of %d checks failed\n", g_failures, g_checks);
    return 1;
  }
  std::printf("all %zu tests passed (%d checks)\n", tests.size(), g_checks);
  return 0;
}
