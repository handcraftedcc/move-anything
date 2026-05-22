#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

bin="build/tests/test_input_mode_core"
modroot="build/tests/input_modes"
mkdir -p "$(dirname "$bin")"
mkdir -p "$modroot/drum32" "$modroot/chromatic" "$modroot/chord-pads" "$modroot/v2-fixture"

cc -std=c11 -Wall -Wextra -Werror -shared -fPIC -Isrc \
  src/modules/input_modes/drum32/dsp/drum32.c \
  -o "$modroot/drum32/dsp.so"
cc -std=c11 -Wall -Wextra -Werror -shared -fPIC -Isrc \
  src/modules/input_modes/chromatic/dsp/chromatic.c \
  -o "$modroot/chromatic/dsp.so"
cc -std=c11 -Wall -Wextra -Werror -shared -fPIC -Isrc \
  src/modules/input_modes/chord-pads/dsp/chord_pads.c \
  -o "$modroot/chord-pads/dsp.so"
cc -std=c11 -Wall -Wextra -Werror -shared -fPIC -Isrc \
  tests/host/fixtures/input_mode_v2_fixture.c \
  -o "$modroot/v2-fixture/dsp.so"

cc -std=c11 -Wall -Wextra -Werror \
  -Isrc \
  tests/host/test_input_mode_core.c \
  src/host/input_mode.c \
  -o "$bin" \
  -ldl

"$bin" "$modroot"
