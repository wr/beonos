// Host tests for components/beonos/setup_wifi.cpp. Run with tests/run.sh.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_wifi.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

using namespace beonos::setup;

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

static Attempt at(uint32_t elapsed_ms) {
  Attempt a;
  a.elapsed_ms = elapsed_ms;
  return a;
}

static void test_address_means_connected() {
  Attempt a = at(5000);
  a.has_address = true;
  a.joined = true;
  a.last_drop = Drop::PASSWORD;  // an earlier attempt's drop doesn't matter
  CHECK_EQ(judge_attempt(a), WifiResult::CONNECTED);
}

static void test_password_drop_before_joining_is_wrong_password() {
  Attempt a = at(6000);
  a.last_drop = Drop::PASSWORD;
  CHECK_EQ(judge_attempt(a), WifiResult::WRONG_PASSWORD);
}

static void test_joined_means_the_password_worked() {
  // The router accepted the password, so a later password-looking drop is
  // something else.
  Attempt a = at(8000);
  a.joined = true;
  a.joined_for_ms = 3000;
  a.last_drop = Drop::PASSWORD;
  CHECK_EQ(judge_attempt(a), WifiResult::CONNECTING);
}

static void test_joined_without_address_for_too_long() {
  Attempt a = at(30000);
  a.joined = true;
  a.joined_for_ms = NO_ADDRESS_AFTER_MS - 1000;
  CHECK_EQ(judge_attempt(a), WifiResult::CONNECTING);
  a.joined_for_ms = NO_ADDRESS_AFTER_MS + 1;
  CHECK_EQ(judge_attempt(a), WifiResult::NO_ADDRESS);
}

static void test_dropped_after_joining_is_no_address() {
  Attempt a = at(9000);
  a.joined = true;
  a.joined_for_ms = 4000;
  a.dropped_after_join = true;
  a.last_drop = Drop::OTHER;
  CHECK_EQ(judge_attempt(a), WifiResult::NO_ADDRESS);
}

static void test_bench_case_auth_expired_after_join() {
  // Seen on the bench, 2026-11-18: the router logged the board as connected,
  // the board timed out waiting for an address and left, and its next try
  // ended in "Auth Expired". The page must not blame the password.
  Attempt a = at(42000);
  a.joined = true;
  a.joined_for_ms = 40000;
  a.dropped_after_join = true;
  a.last_drop = Drop::PASSWORD;
  CHECK_EQ(judge_attempt(a), WifiResult::NO_ADDRESS);
}

static void test_network_not_found() {
  Attempt a = at(7000);
  a.last_drop = Drop::NOT_FOUND;
  CHECK_EQ(judge_attempt(a), WifiResult::NOT_FOUND);
}

static void test_other_drops_wait_then_fail() {
  Attempt a = at(GIVE_UP_AFTER_MS - 1000);
  a.last_drop = Drop::OTHER;
  CHECK_EQ(judge_attempt(a), WifiResult::CONNECTING);
  a.elapsed_ms = GIVE_UP_AFTER_MS + 1;
  CHECK_EQ(judge_attempt(a), WifiResult::FAILED);
}

static void test_nothing_happens_then_fail() {
  CHECK_EQ(judge_attempt(at(1000)), WifiResult::CONNECTING);
  CHECK_EQ(judge_attempt(at(GIVE_UP_AFTER_MS + 1)), WifiResult::FAILED);
}

static void test_result_names() {
  CHECK(std::strcmp(result_name(WifiResult::CONNECTING), "connecting") == 0);
  CHECK(std::strcmp(result_name(WifiResult::CONNECTED), "connected") == 0);
  CHECK(std::strcmp(result_name(WifiResult::WRONG_PASSWORD), "wrong_password") == 0);
  CHECK(std::strcmp(result_name(WifiResult::NOT_FOUND), "not_found") == 0);
  CHECK(std::strcmp(result_name(WifiResult::NO_ADDRESS), "no_address") == 0);
  CHECK(std::strcmp(result_name(WifiResult::FAILED), "failed") == 0);
}

int main() {
  const std::vector<std::pair<const char *, std::function<void()>>> tests = {
      {"address_means_connected", test_address_means_connected},
      {"password_drop_before_joining_is_wrong_password", test_password_drop_before_joining_is_wrong_password},
      {"joined_means_the_password_worked", test_joined_means_the_password_worked},
      {"joined_without_address_for_too_long", test_joined_without_address_for_too_long},
      {"dropped_after_joining_is_no_address", test_dropped_after_joining_is_no_address},
      {"bench_case_auth_expired_after_join", test_bench_case_auth_expired_after_join},
      {"network_not_found", test_network_not_found},
      {"other_drops_wait_then_fail", test_other_drops_wait_then_fail},
      {"nothing_happens_then_fail", test_nothing_happens_then_fail},
      {"result_names", test_result_names},
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
