// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_wifi.h"

namespace beonos {
namespace setup {

WifiResult judge_attempt(const Attempt &attempt) {
  if (attempt.has_address)
    return WifiResult::CONNECTED;
  if (attempt.joined) {
    // The password is right, whatever the drops look like.
    return attempt.joined_for_ms > NO_ADDRESS_AFTER_MS ? WifiResult::NO_ADDRESS : WifiResult::CONNECTING;
  }
  if (attempt.password_drops >= DROPS_FOR_VERDICT)
    return WifiResult::WRONG_PASSWORD;
  if (attempt.not_found_drops >= DROPS_FOR_VERDICT)
    return WifiResult::NOT_FOUND;
  if (attempt.elapsed_ms > GIVE_UP_AFTER_MS)
    return WifiResult::FAILED;
  return WifiResult::CONNECTING;
}

const char *result_name(WifiResult result) {
  switch (result) {
    case WifiResult::CONNECTED:
      return "connected";
    case WifiResult::WRONG_PASSWORD:
      return "wrong_password";
    case WifiResult::NOT_FOUND:
      return "not_found";
    case WifiResult::NO_ADDRESS:
      return "no_address";
    case WifiResult::FAILED:
      return "failed";
    case WifiResult::CONNECTING:
    default:
      return "connecting";
  }
}

}  // namespace setup
}  // namespace beonos
