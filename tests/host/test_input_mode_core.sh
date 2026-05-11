#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

bin="build/tests/test_input_mode_core"
mkdir -p "$(dirname "$bin")"

cc -std=c11 -Wall -Wextra -Werror \
  -Isrc \
  tests/host/test_input_mode_core.c \
  src/host/input_mode.c \
  -o "$bin"

"$bin"
