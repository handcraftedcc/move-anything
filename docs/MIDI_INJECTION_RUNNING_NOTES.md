# MIDI Injection Running Notes

Purpose: append-only notes for debugging `midi_to_move` injection stability in `src/move_anything_shim.c`.

## 2026-03-10

### Evidence observed
- Crash window showed mailbox occupancy spikes (`occ=3..5`, including slot 4) while injector activity continued.
- Crashes included assertion signal for event ordering and an additional assertion in `EventBuffer.hpp:233` (out-of-range event times).
- In external-injection mode, injector busy/interleave conditions were still causing queue drops.

### Change implemented
- External busy guard now blocks injection whenever the contiguous prefix is non-empty (`search_start > 0`), not only when 2+ slots are occupied.
- Busy return (`insert_rc == 2`) now defers queue processing to next cycle (keeps packet queued) instead of dequeueing/dropping.

### Tests
- Updated `tests/shadow/test_midi_to_move_injection_stability.sh` to assert:
  - guard condition is `search_start > 0`
  - busy path breaks/defer-retries
  - busy path does not advance `read_idx`
- Test result: PASS.

### Commit and deploy
- Commit: `835f51d` (`shim: defer midi injection when external mode sees busy prefix`)
- Branch pushed: `origin/MidiInTesting`
- Build/deploy artifact MD5: `5014374e4aaaa56066d3b2e5b6366dbd`
- Installed with: `./scripts/install.sh local --skip-confirmation --skip-modules`

## 2026-03-10 (internal mode parity)

### Evidence observed
- `midi_inject_test` was stable in external mode after busy-prefix defer changes.
- Crash could still occur in `source_mode=internal` (example repro: arp running while pads held).
- Root cause: strongest shim guard checks were keyed to external mode only.

### Change implemented
- Added queue mode flag: `SHADOW_MIDI_TO_MOVE_MODE_INTERNAL` in shared-memory header.
- `midi_inject_test` now publishes both external and internal mode flags based on current `source_mode`.
- Shim guard activation now uses a combined guard predicate (external OR internal mode enabled), so:
  - non-empty-prefix busy guard applies to internal mode
  - mailbox duplicate suppression applies to internal mode
  - internal-aftertouch suppression gate is enabled for guarded internal-forwarding windows

### Verification
- `tests/shadow/test_midi_to_move_injection_stability.sh`: PASS (updated to assert internal-flag plumbing + internal guard activation)
- Full build: PASS (`./scripts/build.sh`)

## Notes format for next entries
- `Date`
- `Evidence observed`
- `Hypothesis`
- `Change implemented`
- `Verification`
- `Open questions`
