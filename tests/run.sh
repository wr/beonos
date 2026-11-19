#!/usr/bin/env bash
# Builds and runs the host tests for the beonos component's parsing code.
# Needs a C++17 compiler (clang++ or g++). Usage: tests/run.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cxx="${CXX:-c++}"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

"$cxx" -std=c++17 -Wall -Wextra -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$root/components/beonos" \
  "$root/tests/test_sonos_parse.cpp" "$root/components/beonos/sonos_parse.cpp" \
  -o "$out/test_sonos_parse"

"$out/test_sonos_parse"
