#!/usr/bin/env bash
set -euo pipefail

file="src/modules/chain/dsp/chain_host.c"

if ! command -v rg >/dev/null 2>&1; then
  echo "rg is required to run this test" >&2
  exit 1
fi

start=$(rg -n "static void v2_on_midi\\(void \\*instance, const uint8_t \\*msg, int len, int source\\)" "$file" | head -n 1 | cut -d: -f1 || true)
end=$(rg -n "^/\\* V2 set_param handler \\*/" "$file" | head -n 1 | cut -d: -f1 || true)

if [ -z "${start}" ] || [ -z "${end}" ]; then
  echo "FAIL: could not locate v2_on_midi boundaries in ${file}" >&2
  exit 1
fi

ctx=$(sed -n "${start},${end}p" "$file")

if ! echo "$ctx" | rg -q "is_external_transport_start_stop\\(msg, len\\)"; then
  echo "FAIL: pre-FX source guard must only trigger on external transport start/continue/stop" >&2
  exit 1
fi

if echo "$ctx" | rg -q "source == MOVE_MIDI_SOURCE_EXTERNAL[[:space:]]*&&[[:space:]]*midi_inject_source_mode_internal_active\\(inst\\)"; then
  echo "FAIL: pre-FX guard must not block all external-source packets in internal mode" >&2
  exit 1
fi

helper=$(sed -n '/static int is_external_transport_start_stop/,/static void v2_midi_debug_maybe_flush/p' "$file")
for status in "0xFAu" "0xFBu" "0xFCu"; do
  if ! echo "$helper" | rg -q "$status"; then
    echo "FAIL: external transport helper missing ${status} handling" >&2
    exit 1
  fi
done

echo "PASS: midi_inject pre-FX guard is transport-only in internal mode"
