#!/usr/bin/env bash
set -euo pipefail

shim="src/move_anything_shim.c"
inject_mod="src/modules/sound_generators/midi_inject_test/midi_inject_test.c"
inject_shm="src/host/shadow_midi_to_move_shm.h"

if ! command -v rg >/dev/null 2>&1; then
  echo "rg is required to run this test" >&2
  exit 1
fi

# Deterministic insertion policy: append after mailbox tail, then wrap.
if ! rg -n "last_non_empty_slot" "$shim" >/dev/null 2>&1; then
  echo "FAIL: Missing mailbox tail scan for deterministic insertion" >&2
  exit 1
fi

if ! rg -n "search_start = j \\+ 4" "$shim" >/dev/null 2>&1; then
  echo "FAIL: Missing contiguous-prefix append search logic" >&2
  exit 1
fi

# External-inject safety: if contiguous prefix is non-empty, defer injection.
guard_ctx=$(sed -n '/In external-source injection mode/,/return 2;/p' "$shim")
if ! echo "$guard_ctx" | rg -q "search_start > 0"; then
  echo "FAIL: External-inject busy guard must trigger on any non-empty prefix" >&2
  exit 1
fi

# Internal mode parity: source mode should expose an internal guard flag too.
if ! rg -n "SHADOW_MIDI_TO_MOVE_MODE_INTERNAL" "$inject_shm" >/dev/null 2>&1; then
  echo "FAIL: Missing SHADOW_MIDI_TO_MOVE_MODE_INTERNAL shared mode flag" >&2
  exit 1
fi
if ! rg -n "SHADOW_MIDI_TO_MOVE_MODE_INTERNAL" "$inject_mod" >/dev/null 2>&1; then
  echo "FAIL: midi_inject_test does not publish internal mode guard flag" >&2
  exit 1
fi
if ! echo "$guard_ctx" | rg -q "shadow_inject_guard_mode_enabled"; then
  echo "FAIL: Busy guard does not apply to internal inject mode" >&2
  exit 1
fi

# Busy behavior: keep packet queued for next cycle (do not dequeue/drop on busy).
busy_line=$(rg -n "if \\(insert_rc == 2\\)" "$shim" | head -n 1 | cut -d: -f1 || true)
if [ -z "${busy_line}" ]; then
  echo "FAIL: Missing busy return handling for injector" >&2
  exit 1
fi
busy_end=$((busy_line + 6))
busy_ctx=$(sed -n "${busy_line},${busy_end}p" "$shim")
if ! echo "$busy_ctx" | rg -q "break;"; then
  echo "FAIL: Busy injector path should defer by breaking (retry next cycle)" >&2
  exit 1
fi
if echo "$busy_ctx" | rg -q "read_idx\\+\\+"; then
  echo "FAIL: Busy injector path should not advance read_idx (no busy drop)" >&2
  exit 1
fi

# Duplicate-edge coalescing: avoid repeated note on/off transitions.
if ! rg -n "shadow_midi_to_move_is_duplicate_edge" "$shim" >/dev/null 2>&1; then
  echo "FAIL: Missing duplicate-edge coalescing helper" >&2
  exit 1
fi

# Diagnostic hook for mailbox occupancy and insertion position.
if ! rg -n "midi_to_move diag" "$shim" >/dev/null 2>&1; then
  echo "FAIL: Missing midi_to_move diagnostic telemetry log" >&2
  exit 1
fi

echo "PASS: MIDI-to-Move stability guards present"
