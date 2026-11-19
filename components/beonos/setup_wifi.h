// Beonos: what the setup page tells someone after they tap Connect.
//
// A pure function with no ESPHome or ESP-IDF dependencies, so it can be
// unit-tested on a computer (see tests/). The component gathers what the
// Wi-Fi driver reported since the save and asks judge_attempt() what that
// means.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace beonos {
namespace setup {

// Why the board's last attempt to join the network ended.
enum class Drop : uint8_t {
  NONE,
  PASSWORD,   // the router turned the password down, or never finished the handshake
  NOT_FOUND,  // the network isn't in range (or isn't 2.4 GHz)
  OTHER,
};

enum class WifiResult : uint8_t {
  CONNECTING,
  CONNECTED,
  WRONG_PASSWORD,
  NOT_FOUND,
  NO_ADDRESS,  // the password worked, but the router gave the board no IP address
  FAILED,
};

// What the board has seen since the setup page saved new Wi-Fi details.
// One failed attempt proves little (a router can time out a correct
// password once), so a verdict about the password or the network needs two.
struct Attempt {
  uint32_t elapsed_ms{0};       // since the save
  bool has_address{false};      // on the network with an IP address
  bool joined{false};           // the router accepted the password at least once
  uint32_t joined_for_ms{0};    // since the first time it did
  uint32_t password_drops{0};   // attempts that ended as Drop::PASSWORD
  uint32_t not_found_drops{0};  // attempts that ended as Drop::NOT_FOUND
};

constexpr uint32_t DROPS_FOR_VERDICT = 2;

// Getting an address normally takes a second or two after joining.
constexpr uint32_t NO_ADDRESS_AFTER_MS = 20000;
// With no clearer answer, the page gives up after this long.
constexpr uint32_t GIVE_UP_AFTER_MS = 45000;

WifiResult judge_attempt(const Attempt &attempt);

// The name /beonos.json reports to the setup page.
const char *result_name(WifiResult result);

}  // namespace setup
}  // namespace beonos
