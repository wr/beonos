#!/usr/bin/env bash
# Builds and runs the host tests for the beonos component's pure code.
# Needs a C++17 compiler (clang++ or g++). Usage: tests/run.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cxx="${CXX:-c++}"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

# test name, then the component sources it covers
build_and_run() {
  local name="$1"
  shift
  local sources=()
  for src in "$@"; do sources+=("$root/components/beonos/$src"); done
  "$cxx" -std=c++17 -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$root/components/beonos" \
    "$root/tests/$name.cpp" "${sources[@]}" \
    -o "$out/$name"
  echo "== $name"
  "$out/$name"
}

build_and_run test_sonos_parse sonos_parse.cpp
build_and_run test_setup_wifi setup_wifi.cpp
build_and_run test_command_queue command_queue.cpp
