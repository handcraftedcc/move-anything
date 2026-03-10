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

if ! echo "$ctx" | rg -q "inst_midi_source_allowed\\(inst, source\\)"; then
  echo "FAIL: v2_on_midi missing per-instance source gate call" >&2
  exit 1
fi

if ! echo "$ctx" | rg -q "inst_midi_inject_test_source_allowed\\(inst, source\\)"; then
  echo "FAIL: v2_on_midi missing midi_inject_test pre-MIDI-FX source gate call" >&2
  exit 1
fi

helper=$(sed -n '/static int inst_midi_source_allowed/,/static void v2_on_midi/p' "$file")

if ! echo "$helper" | rg -q "inst->midi_input == MIDI_INPUT_PADS"; then
  echo "FAIL: helper missing MIDI_INPUT_PADS source gate" >&2
  exit 1
fi

if ! echo "$helper" | rg -q "inst->midi_input == MIDI_INPUT_EXTERNAL"; then
  echo "FAIL: helper missing MIDI_INPUT_EXTERNAL source gate" >&2
  exit 1
fi

if ! echo "$helper" | rg -q "source == MOVE_MIDI_SOURCE_HOST"; then
  echo "FAIL: helper must allow host-generated MIDI through source gate" >&2
  exit 1
fi

if ! echo "$helper" | rg -q "strcmp\\(inst->current_synth_module, \"midi_inject_test\"\\)"; then
  echo "FAIL: helper missing midi_inject_test module check" >&2
  exit 1
fi

if ! echo "$helper" | rg -q "get_param\\("; then
  echo "FAIL: helper missing synth get_param call" >&2
  exit 1
fi

if ! echo "$helper" | rg -q "\"source_mode\""; then
  echo "FAIL: helper missing source_mode lookup from synth instance" >&2
  exit 1
fi

if ! echo "$helper" | rg -q "source == MOVE_MIDI_SOURCE_INTERNAL"; then
  echo "FAIL: helper missing internal source-mode allow rule" >&2
  exit 1
fi

if ! echo "$helper" | rg -q "source == MOVE_MIDI_SOURCE_EXTERNAL"; then
  echo "FAIL: helper missing external source-mode allow rule" >&2
  exit 1
fi

echo "PASS: v2_on_midi enforces per-instance MIDI source gate"
