#!/usr/bin/env bash
set -euo pipefail

cc -std=c11 -Wall -Wextra \
  -Isrc -I. \
  tests/host/test_shadow_led_queue_input_suppress.c \
  src/host/shadow_led_queue.c \
  -o /tmp/test_shadow_led_queue_input_suppress

/tmp/test_shadow_led_queue_input_suppress
echo "PASS: shadow LED queue suppresses input-mode-owned Move pad LEDs"
