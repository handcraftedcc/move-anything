# Input Modes

Input Modes let Schwung change how Move's physical pads feed the selected track before Move's firmware receives the event.

The first implementation is intentionally small and safe. It ships one core input module named **Test** with a `Layout` enum:

- **None/Native**: Move receives pad notes normally.
- **32 Drum Pads**: the 32 pads are remapped to consecutive drum notes from MIDI note 36 to 67.
- **Chromatic**: the 32 pads are remapped to a chromatic range from MIDI note 48 to 79, then injected back into Move as external cable-2 MIDI on the selected track's channel. The original pad note is blocked so Move only hears the remapped note.
- **Chords**: each pad emits a three-note diatonic triad. This layout exists to validate multi-note output and stuck-note prevention.

LED behavior is still native in this first pass. The shared state includes a per-track LED mode field so LED-driving modules can be added later without changing the persistence format.

The shim only applies input overrides when Move's pad LED grid looks like a playable Note layout. It watches Move's own pad LED MIDI, ignores pads that are currently held, and classifies the grid only after native navigation gestures that repaint the pads: track buttons, Menu, and Shift + Step 1. Normal pad playing does not continuously reclassify the grid.

- Note layouts pass when the grid is mostly two colors, with optional off pads for chromatic layouts.
- Drum layouts pass when the left half is a consistent drum-pad color and the right half is off or note-like.
- Track and Set layouts fail because they are row-uniform track colors or many independent set colors.

Unknown layouts fail closed, so Session view, Set Overview, and set selection pass pad events through unchanged.

## On-Device Shortcut

Open the Input Mode menu with:

```text
Shift + Volume Touch + Step 9
```

In the menu:

- Press a track button to choose the track you are editing.
- If the track is Native, the menu opens to the input module selector.
- Selecting a module opens that module's detail page.
- Every input module detail page includes a **Swap Module** row that returns to the selector.
- The Test module exposes its `Layout` enum from `module.json`.
- Press Back to return to the Slots menu.

Step 9 lights while Shift and Volume Touch are held, matching the existing Step 2 and Step 13 shortcut hints.

## Persistence

Input mode settings are saved per set in:

```text
/data/UserData/schwung/set_state/<set-uuid>/input_modes.json
```

The default fallback set uses:

```text
/data/UserData/schwung/slot_state/input_modes.json
```

The file is human-readable JSON:

```json
{
  "input_modes": {
    "tracks": {
      "1": {
        "module": "native",
        "params": {
          "layout": "native"
        },
        "mode": "native",
        "led_mode": "pass_through"
      },
      "2": {
        "module": "test",
        "params": {
          "layout": "true_chromatic_poc"
        },
        "mode": "true_chromatic_poc",
        "led_mode": "pass_through"
      }
    }
  }
}
```

Missing tracks default to Native. Invalid mode names also fall back to Native.

## Module Metadata

Input modes are discovered from installed modules with:

```json
{
  "component_type": "input_mode",
  "input_mode": {
    "engine": "host_static",
    "mode": "true_chromatic_poc"
  }
}
```

The current built-in test module is host-backed and lives at:

```text
src/modules/input_modes/test/module.json
```

Its `chain_params` entry defines the `Layout` enum used by the Shadow UI. The real-time mapping currently lives in `src/host/input_mode.c`. A later API can replace the host-backed static mapping with loadable input-mode processors while keeping the same menu and persistence shape.

For simple host-backed modules that do not expose a `Layout` enum, set `input_mode.mode` to one of the supported host layout values:

- `native`
- `drum32`
- `true_chromatic_poc`
- `chord_pads`

If a module has a `layout`, `mode`, `host_mode`, or `input_mode` parameter, that saved parameter value takes priority over `input_mode.mode`. This lets the Test module switch layouts from one detail page while letting one-layout modules activate their MIDI transform as soon as they are selected.

For compatibility with simple module ids, `true-chromatic`, `32-drum-pads`, `drum-pads`, and `chord-pads` are accepted aliases for the same host-backed layouts.

The current set's musical context is also mirrored into shared memory for future input modules:

- `rootNote`
- `scale`
- `melodicLayout`

Shadow UI modules can read it with `shadow_get_set_musical_context()`. The values are refreshed from the active set's `Song.abl` file when set tracking detects a loaded set.

## Developer Notes

The host-side core lives in `src/host/input_mode.c` and is deliberately independent of the shim. Unit tests cover the pad mapping, pass-through behavior, and panic notes emitted when switching a track back to Native while notes are held.

The shim owns the real-time wiring:

- `src/schwung_shim.c` watches the selected track and intercepts cable-0 pad notes.
- Remapped notes are queued through `shadow_chain_midi_inject()` as cable-2 packets.
- `shadow_midi_force_defer(2)` prevents same-frame cable-0/cable-2 injection races.
- `src/shadow/shadow_ui.js` loads and saves per-set input mode state.

Any future mode should follow the same rule as the current Chromatic mode: if Schwung transforms a physical pad event, block the original pad event and emit all required note-offs when the mode changes.
