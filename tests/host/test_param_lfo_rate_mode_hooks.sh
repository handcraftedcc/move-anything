#!/usr/bin/env bash
set -euo pipefail

file="src/modules/midi_fx/param_lfo/dsp/param_lfo.c"
meta="src/modules/midi_fx/param_lfo/module.json"

if ! rg -q 'RATE_MODE_FREE' "$file" || ! rg -q 'RATE_MODE_SYNC' "$file"; then
  echo "FAIL: rate mode enum is missing free/sync modes" >&2
  exit 1
fi
if ! rg -q 'strcmp\(subkey, "rate_mode"\)' "$file"; then
  echo "FAIL: rate_mode parameter handler is missing" >&2
  exit 1
fi
if ! rg -q 'status == 0xF8' "$file"; then
  echo "FAIL: missing MIDI clock (0xF8) sync handling" >&2
  exit 1
fi
if ! rg -q 'lfo%d_rate_mode' "$file"; then
  echo "FAIL: runtime chain_params missing lfo rate_mode metadata" >&2
  exit 1
fi
if ! rg -q 'lfo%d_rate_hz' "$file" || ! rg -q '\"8 bars\"' "$file" || ! rg -q '\"1/64\"' "$file"; then
  echo "FAIL: runtime chain_params missing full synced division range (8 bars .. 1/64)" >&2
  exit 1
fi
if ! rg -q '"key": "lfo1_rate_mode"' "$meta" || ! rg -q '"key": "lfo3_rate_mode"' "$meta"; then
  echo "FAIL: module metadata missing per-LFO rate_mode params" >&2
  exit 1
fi
if ! rg -q '"options": \["free", "sync"\]' "$meta"; then
  echo "FAIL: module metadata missing free/sync rate mode options" >&2
  exit 1
fi

echo "PASS: param_lfo rate mode hooks and sync-rate metadata are present"
