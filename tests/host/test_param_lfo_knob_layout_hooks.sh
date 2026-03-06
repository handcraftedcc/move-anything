#!/usr/bin/env bash
set -euo pipefail

meta="src/modules/midi_fx/param_lfo/module.json"

if ! rg -q '"knobs": \["enable", "waveform", "rate_hz", "phase", "depth", "offset", "polarity", "retrigger"\]' "$meta"; then
  echo "FAIL: param_lfo knob layout is not mapped to all non-target parameters" >&2
  exit 1
fi
if rg -q '"knobs": \[[^]]*"target_component"' "$meta" || rg -q '"knobs": \[[^]]*"target_param"' "$meta"; then
  echo "FAIL: param_lfo knob layout must exclude target selection parameters" >&2
  exit 1
fi

echo "PASS: param_lfo knob layout maps all non-target parameters to 8 knobs"
