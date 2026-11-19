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
  a.password_drops = 3;  // earlier drops don't matter once it's on
  CHECK_EQ(judge_attempt(a), WifiResult::CONNECTED);
}

static void test_one_password_drop_isnt_a_verdict() {
  // Seen on the bench: one "Auth Expired" with a correct password.
  Attempt a = at(6000);
  a.password_drops = 1;
  CHECK_EQ(judge_attempt(a), WifiResult::CONNECTING);
}

static void test_two_password_drops_before_joining_is_wrong_password() {
  Attempt a = at(9000);
  a.password_drops = 2;
  CHECK_EQ(judge_attempt(a), WifiResult::WRONG_PASSWORD);
}

static void test_joined_means_the_password_worked() {
  Attempt a = at(8000);
  a.joined = true;
  a.joined_for_ms = 3000;
  a.password_drops = 2;
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

static void test_bench_case_joined_but_no_address() {
  // Seen on the bench, 2026-11-18: the router logged the board as connected
  // for ~40 s at a time but never gave it an address, and one try ended in
  // "Auth Expired". The page must not blame the password.
  Attempt a = at(42000);
  a.joined = true;
  a.joined_for_ms = 40000;
  a.password_drops = 1;
  CHECK_EQ(judge_attempt(a), WifiResult::NO_ADDRESS);
}

static void test_network_not_found_needs_two() {
  Attempt a = at(7000);
  a.not_found_drops = 1;
  CHECK_EQ(judge_attempt(a), WifiResult::CONNECTING);
  a.not_found_drops = 2;
  CHECK_EQ(judge_attempt(a), WifiResult::NOT_FOUND);
}

static void test_other_drops_wait_then_fail() {
  Attempt a = at(GIVE_UP_AFTER_MS - 1000);
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
      {"one_password_drop_isnt_a_verdict", test_one_password_drop_isnt_a_verdict},
      {"two_password_drops_before_joining_is_wrong_password", test_two_password_drops_before_joining_is_wrong_password},
      {"joined_means_the_password_worked", test_joined_means_the_password_worked},
      {"joined_without_address_for_too_long", test_joined_without_address_for_too_long},
      {"bench_case_joined_but_no_address", test_bench_case_joined_but_no_address},
      {"network_not_found_needs_two", test_network_not_found_needs_two},
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
