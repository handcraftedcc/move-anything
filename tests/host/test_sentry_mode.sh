#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."

bin="build/tests/test_sentry_mode"
mkdir -p "$(dirname "$bin")"

cc -std=c11 -Wall -Wextra -Werror \
  -Isrc \
  tests/host/test_sentry_mode.c \
  src/host/sentry_mode.c \
  -o "$bin"

"$bin"
