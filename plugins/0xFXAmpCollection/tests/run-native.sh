#!/bin/sh
# Run from anywhere: sh plugins/0xFXAmpCollection/tests/run-native.sh
set -eu
src=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc=${CC:-cc}
cxx=${CXX:-c++}
"$cc" -std=c99 -Wall -Wextra -O2 -I "$src/vendor/0xfx" \
  -c "$src/vendor/0xfx/amp.c" -o "$tmp/amp.o"
"$cc" -std=c99 -Wall -Wextra -O2 -I "$src/vendor/0xfx" \
  -c "$src/vendor/0xfx/biquad.c" -o "$tmp/biquad.o"
"$cxx" -std=c++17 -Wall -Wextra -O2 -DWYLDTONE_NATIVE_TEST \
  -I "$src/tests/include" -c "$src/plugin.cpp" -o "$tmp/plugin.o"
"$cxx" -std=c++17 -O2 "$src/tests/native_amp_test.cpp" \
  "$tmp/plugin.o" "$tmp/amp.o" "$tmp/biquad.o" -lm -o "$tmp/amp-test"
"$tmp/amp-test"
