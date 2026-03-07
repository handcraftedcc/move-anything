#!/usr/bin/env bash
set -euo pipefail

file="src/modules/midi_fx/param_lfo/dsp/param_lfo.c"

if ! rg -q 'status == 0xFA .*status == 0xFC' "$file"; then
  echo "FAIL: missing MIDI start/stop phase reset handling" >&2
  exit 1
fi
if ! rg -q 'handle_note_on\(' "$file"; then
  echo "FAIL: missing note-on gate handling for retrigger" >&2
  exit 1
fi
if ! rg -q 'inst->held_count == 0' "$file" || ! rg -q 'inst->lanes\[i\]\.retrigger' "$file"; then
  echo "FAIL: retrigger gate condition not implemented" >&2
  exit 1
fi
if ! rg -q 'strcmp\(subkey, "retrigger"\)' "$file"; then
  echo "FAIL: retrigger key handler missing" >&2
  exit 1
fi
if ! rg -q '"key": "lfo1_retrigger"' src/modules/midi_fx/param_lfo/module.json || ! rg -q '"key": "lfo3_retrigger"' src/modules/midi_fx/param_lfo/module.json; then
  echo "FAIL: retrigger not exposed in module metadata" >&2
  exit 1
fi

echo "PASS: param_lfo transport reset and retrigger hooks are present"
