#!/usr/bin/env bash
set -euo pipefail

file="src/modules/midi_fx/param_lfo/dsp/param_lfo.c"
meta="src/modules/midi_fx/param_lfo/module.json"

if ! rg -q 'WAVE_RANDOM' "$file"; then
  echo "FAIL: random waveform enum is missing" >&2
  exit 1
fi
if ! rg -q 'WAVE_DRUNK' "$file"; then
  echo "FAIL: drunk waveform enum is missing" >&2
  exit 1
fi
if ! rg -q 'phase \+ inst->phase_offset' "$file"; then
  echo "FAIL: phase offset is not applied to waveform phase" >&2
  exit 1
fi
if ! rg -q 'strcmp\(key, "phase"\)' "$file"; then
  echo "FAIL: phase parameter handler is missing" >&2
  exit 1
fi
if ! rg -q 'waveform_rate_multiplier' "$file"; then
  echo "FAIL: waveform rate multiplier helper is missing" >&2
  exit 1
fi
if ! rg -q 'inst->rate_hz \* waveform_rate_multiplier\(inst\)' "$file"; then
  echo "FAIL: phase increment is not scaled by waveform rate multiplier" >&2
  exit 1
fi
if ! rg -q '"key\\":\\"waveform\\"' "$file" || ! rg -q 'random' "$file" || ! rg -q 'drunk' "$file"; then
  echo "FAIL: chain params missing random/drunk waveform options" >&2
  exit 1
fi
if ! rg -q '"key\\":\\"phase\\"' "$file"; then
  echo "FAIL: chain params missing phase metadata" >&2
  exit 1
fi
if ! rg -q '"options": \["sine", "triangle", "square", "saw_up", "random", "drunk"\]' "$meta"; then
  echo "FAIL: module metadata missing random/drunk waveform options" >&2
  exit 1
fi
if ! rg -q '"key": "phase"' "$meta"; then
  echo "FAIL: module metadata missing phase parameter" >&2
  exit 1
fi

echo "PASS: param_lfo phase/random/drunk hooks and drunk rate scaling are present"
