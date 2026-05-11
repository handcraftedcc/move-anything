# Schwung Native Input Override Development Guide

## Goal

Add a new Schwung-level input system that can optionally replace Ableton Move's native pad/key input handling on a per-track basis.

Today, Schwung MIDI FX generally happen **after** Move's native input processing, which means custom MIDI FX are still tied to Move's native scale modes, pad layouts, and note interpretation. The proposed feature adds an earlier stage:

```text
Raw hardware pad input
→ Schwung custom input mode
→ optional timing/modifier stage
→ inject as external MIDI on cable 2
→ Move track receives MIDI as if from USB/external MIDI
```

The initial implementation should focus only on **Step 1: custom input modes**. Timing/modifiers/arpeggiators can be added later as a second processing stage.

Schwung already runs as an `LD_PRELOAD` shim inside MoveOriginal, intercepting SPI mailbox traffic and handling MIDI routing, overlay drawing, shadow audio, and shared-memory communication with `shadow_ui`. The current architecture docs describe cable 0 as internal Move hardware controls and cable 2 as external USB MIDI, which is also used by Schwung for injecting MIDI back into Move's native engine.

---

## Design Principle

This feature should not create a special-case parallel ecosystem.

Input modes should feel like normal Schwung modules from the authoring side:

```text
module.json defines metadata and params
Shadow UI renders the params
Schwung set state saves the params
DSP/host code consumes the params
```

The difference is where the module sits in the signal path:

```text
Normal MIDI FX:
    after native Move input

Input Mode:
    before native Move input, replacing the pad/key layer
```

Pad LED ownership is also part of this API because changing the pad layout often changes what the LEDs mean.

---

## 1. User-Facing Concept

Add two new Schwung settings menus:

```text
Shift + Volume Touch + Step 9
→ Input Mode

Shift + Volume Touch + Step 10
→ Timing / Modifiers
```

For the first development phase, implement only:

```text
Shift + Volume Touch + Step 9
→ Input Mode
```

The input mode setting should be:

```text
Native
```

by default.

When set to `Native`, Schwung does nothing special. Move's built-in pad/key system receives the hardware pad events exactly as it does today.

When set to a custom input mode, Schwung should:

1. Intercept raw pad/key hardware MIDI from cable 0.
2. Prevent those pad/key events from reaching Move's native input layer.
3. Pass the raw pad/key event to the selected Schwung input module.
4. Let the input module output normal MIDI note events.
5. Inject those MIDI events back into Move as cable-2 external MIDI.
6. Route the injected MIDI to the currently selected Move track's MIDI input channel.

Other hardware controls should keep behaving normally unless explicitly claimed:

```text
Pads/keys: handled by Schwung custom input mode
Sequencer buttons: pass through to Move
Knobs: pass through to Move / Shadow UI as currently implemented
Transport/buttons: pass through unless later explicitly claimed
```

---

## 2. Why This Is Different From Current MIDI FX

Current MIDI FX generally transform MIDI after Move's native input interpretation. That means they receive already-processed notes, not the raw pad layout.

This feature creates a new earlier layer:

```text
Native path:
Hardware pads
→ Move native key/scale/layout processing
→ Move instruments

New custom path:
Hardware pads
→ Schwung input mode
→ cable-2 MIDI injection
→ Move instruments
```

This makes these possible:

```text
32 Drum Pads
Chord Pads
True Chromatic Layout
Isomorphic Layouts
Custom Scale Layouts
MPE-ish layouts later, if supported
User-defined pad maps
```

The shipped Schwung MIDI injection path already supports injecting cable-2 USB-MIDI packets into Move's MIDI input. The current `ADDRESSING_MOVE_SYNTHS.md` guide says modules should send on the channel that the receiving Move track is listening on, with tracks 1-4 defaulting to MIDI channels 1-4. It also documents cable-2 packet injection through `host->midi_inject_to_move()` and the expected USB-MIDI packet format.

---

## 3. Core Architecture

### 3.1 High-Level Flow

```text
[Hardware SPI MIDI_IN cable 0]
        |
        v
[Shim raw input filter]
        |
        +-- if track input mode == Native:
        |       pass original event to Move unchanged
        |
        +-- if custom input mode active:
                block pad/key event from Move
                send raw event to Schwung input engine
                input engine emits note/CC MIDI
                inject output as cable-2 MIDI_IN
                Move receives it as external MIDI
```

The docs describe the SPI mailbox input area as containing MIDI input events, where cable 0 represents internal Move hardware controls and cable 2 represents external USB MIDI. This feature should operate at that same shim-level MIDI filtering point.

---

## 4. Track-Aware State Model

Input mode settings must be **track-aware**.

Each of the four Move tracks should have its own input configuration:

```json
{
  "tracks": {
    "1": {
      "input_mode": "native",
      "input_module": null,
      "input_params": {},
      "led_mode": "pass_through"
    },
    "2": {
      "input_mode": "native",
      "input_module": null,
      "input_params": {},
      "led_mode": "pass_through"
    },
    "3": {
      "input_mode": "native",
      "input_module": null,
      "input_params": {},
      "led_mode": "pass_through"
    },
    "4": {
      "input_mode": "native",
      "input_module": null,
      "input_params": {},
      "led_mode": "pass_through"
    }
  }
}
```

Default behavior:

```text
If no saved setting exists for a track:
    input_mode = native
```

The selected input mode should switch automatically when the user changes the active Move track.

Important requirement:

```text
Never globally force a non-native input mode.
Every track must independently default back to native unless explicitly configured.
```

### 4.1 Set-Aware Saving / Loading

Input mode settings must be saved as part of Schwung's existing **set-specific state**, not only as a global Schwung preference.

The intended behavior is:

```text
Move Set A:
    Track 1 → Native
    Track 2 → True Chromatic
    Track 3 → Chord Pads
    Track 4 → Native

Move Set B:
    Track 1 → 32 Drum Pads
    Track 2 → Native
    Track 3 → Native
    Track 4 → Native
```

When the user switches or reloads a Move set, Schwung should restore the input mode configuration associated with that set.

If no saved Schwung input configuration exists for the current set, all tracks must default to:

```text
Native
```

This is important because custom input override changes core performance behavior. It should never accidentally carry over globally into unrelated sets.

Recommended saved data structure:

```json
{
  "input_modes": {
    "tracks": {
      "1": {
        "mode": "native",
        "module_id": null,
        "params": {},
        "led_mode": "pass_through"
      },
      "2": {
        "mode": "module",
        "module_id": "true-chromatic-input",
        "params": {
          "root": 60,
          "layout": "linear"
        },
        "led_mode": "module"
      },
      "3": {
        "mode": "native",
        "module_id": null,
        "params": {},
        "led_mode": "pass_through"
      },
      "4": {
        "mode": "native",
        "module_id": null,
        "params": {},
        "led_mode": "pass_through"
      }
    }
  }
}
```

This should be integrated into the same save/load lifecycle Schwung already uses for set-specific module state.

Implementation requirement:

```text
Input mode changes made through the UI must be persisted with the current Schwung set state.

When the set is reloaded:
    restore per-track input mode
    restore selected module
    restore module params
    restore LED behavior mode
```

Fail-safe behavior:

```text
If saved input mode state is missing, invalid, or references an unavailable module:
    fall back to Native for that track
```

---

## 5. Input Module Types

Add a new module category or capability for input modules.

Recommended new `component_type`:

```json
{
  "component_type": "input_mode"
}
```

Alternative if avoiding a new component type:

```json
{
  "component_type": "utility",
  "capabilities": {
    "input_mode": true
  }
}
```

A new `component_type: "input_mode"` is cleaner and easier for the Shadow UI to filter.

Current Schwung module docs already support module metadata through `module.json`, `component_type`, `capabilities`, optional `ui.js`, optional `dsp.so`, and module defaults. New input modules should follow the same packaging style wherever possible.

Example:

```json
{
  "id": "true-chromatic-input",
  "name": "True Chromatic",
  "version": "0.1.0",
  "api_version": 2,
  "component_type": "input_mode",
  "description": "A non-overlapping chromatic pad layout for Move.",
  "author": "Schwung",
  "ui": "ui.js",
  "dsp": "dsp.so",
  "capabilities": {
    "input_mode": true,
    "midi_out": true,
    "led_out": true
  },
  "defaults": {
    "root": 60,
    "layout": "rows_fourths",
    "velocity_mode": "native"
  }
}
```

### 5.1 Input Modes Should Use the Same Module UI System

Input mode modules should use the same UI definition model as other Schwung modules wherever possible.

That means input modules should expose their editable parameters through `module.json`, rather than requiring custom hardcoded UI for every input mode.

The Input Mode menu should be a host/container menu that lets the user:

```text
1. Pick input mode for the current track.
2. If the selected mode is a module, show that module's parameters.
3. Edit those parameters using the same base parameter UI system used by other Schwung modules.
4. Save those params into the current set-specific Schwung state.
```

Example:

```json
{
  "id": "true-chromatic-input",
  "name": "True Chromatic",
  "version": "0.1.0",
  "api_version": 2,
  "component_type": "input_mode",
  "description": "A non-overlapping chromatic pad layout.",
  "capabilities": {
    "input_mode": true,
    "midi_out": true,
    "led_out": true
  },
  "params": [
    {
      "id": "root",
      "name": "Root",
      "type": "note",
      "default": 48
    },
    {
      "id": "layout",
      "name": "Layout",
      "type": "enum",
      "default": "linear",
      "options": [
        { "value": "linear", "label": "Linear" },
        { "value": "fourths", "label": "4ths" },
        { "value": "fifths", "label": "5ths" }
      ]
    },
    {
      "id": "velocity_mode",
      "name": "Velocity",
      "type": "enum",
      "default": "native",
      "options": [
        { "value": "native", "label": "Native" },
        { "value": "fixed", "label": "Fixed" }
      ]
    },
    {
      "id": "fixed_velocity",
      "name": "Fixed Vel",
      "type": "int",
      "default": 100,
      "min": 1,
      "max": 127,
      "visible_if": {
        "velocity_mode": "fixed"
      }
    }
  ]
}
```

The goal is that input modules behave like normal Schwung modules from a UI-authoring perspective:

```text
Module author defines params in module.json.
Shadow UI renders/edits/saves those params.
Input engine consumes the current param values.
```

Avoid designing a second, separate UI parameter system just for input modules.

The only special UI element should be the host-level selector:

```text
Input Mode:
    Native
    True Chromatic
    32 Drum Pads
    Chord Pads
    ...
```

Once a module is selected, its editable settings should come from `module.json`.

### 5.2 Module JSON Example With LED Capability

```json
{
  "id": "chord-pads-input",
  "name": "Chord Pads",
  "version": "0.1.0",
  "api_version": 2,
  "component_type": "input_mode",
  "description": "Maps each Move pad to a chord.",
  "author": "Schwung",
  "dsp": "dsp.so",
  "capabilities": {
    "input_mode": true,
    "midi_out": true,
    "led_out": true
  },
  "params": [
    {
      "id": "root",
      "name": "Root",
      "type": "note",
      "default": 60
    },
    {
      "id": "scale",
      "name": "Scale",
      "type": "enum",
      "default": "minor",
      "options": [
        { "value": "major", "label": "Major" },
        { "value": "minor", "label": "Minor" },
        { "value": "dorian", "label": "Dorian" },
        { "value": "mixolydian", "label": "Mixolydian" }
      ]
    },
    {
      "id": "voicing",
      "name": "Voicing",
      "type": "enum",
      "default": "triads",
      "options": [
        { "value": "triads", "label": "Triads" },
        { "value": "sevenths", "label": "7ths" },
        { "value": "spread", "label": "Spread" }
      ]
    },
    {
      "id": "led_style",
      "name": "LED Style",
      "type": "enum",
      "default": "chord_quality",
      "options": [
        { "value": "off", "label": "Off" },
        { "value": "chord_quality", "label": "Quality" },
        { "value": "held", "label": "Held" }
      ]
    }
  ]
}
```

---

## 6. Input Module API

The first implementation can be done either in C/DSP or JS, but the real-time-sensitive pad interception should avoid blocking, allocations, or file I/O in the SPI callback. Schwung's architecture docs explicitly note that SPI callback paths should be non-allocating and non-blocking.

Recommended model:

```text
Shim receives raw pad event
→ writes compact event into SHM/ring buffer
→ input module DSP or host-side processor consumes it
→ emits MIDI output packets into existing injection ring
```

### 6.1 Minimal Input Event

Represent raw pad events as a compact struct:

```c
typedef struct {
    uint8_t type;       // note_on, note_off, pressure, unknown
    uint8_t pad_index;  // 0-31 if known
    uint8_t note;       // raw Move note number, e.g. 68-99
    uint8_t velocity;
    uint8_t track;      // 0-3 active track at event time
    uint32_t timestamp; // optional SPI/input timestamp
} schwung_input_event_t;
```

For this feature, the first pass should intercept only pad/key note messages, not step buttons.

### 6.2 Input Module Output

Input modules should output ordinary MIDI messages:

```c
typedef struct {
    uint8_t status;   // 0x90, 0x80, 0xB0, etc.
    uint8_t data1;    // note/cc
    uint8_t data2;    // velocity/value
    uint8_t channel;  // 0-15
} schwung_midi_msg_t;
```

The final injection packet should be converted to USB-MIDI cable-2 format:

```c
uint8_t pkt[4] = {
    (2 << 4) | cin,
    status | channel,
    data1,
    data2
};
```

Note-on should use cable 2 with CIN `0x09`; note-off should use cable 2 with CIN `0x08`.

### 6.3 Optional Pad LED API

Input modules may optionally provide LED output for the playable pad grid.

Minimum API:

```c
typedef struct {
    uint8_t pad_index;   // 0-31
    uint8_t r;           // 0-127 or native LED scale
    uint8_t g;
    uint8_t b;
    uint8_t brightness;  // optional
} schwung_pad_led_t;
```

Alternative if Move uses a packed native LED format internally:

```c
typedef struct {
    uint8_t pad_index;
    uint8_t color_id;
    uint8_t brightness;
    uint8_t flags;
} schwung_pad_led_t;
```

The exact representation should match Schwung's existing LED/pad-light handling if one already exists.

Input module optional callbacks:

```c
void input_on_pad_event(
    const schwung_input_event_t *event,
    schwung_midi_output_t *midi_out,
    schwung_led_output_t *led_out
);

void input_render_leds(
    const input_module_state_t *state,
    schwung_led_output_t *led_out
);
```

The first callback is event-driven and useful for immediate LED feedback:

```text
pad pressed
→ flash pad
→ mark held state
```

The second callback is state-driven and useful when params change:

```text
root changed
→ recompute scale colors
layout changed
→ redraw all pad LEDs
track changed
→ redraw selected track's pad LEDs
```

Required behavior:

```text
When switching to a custom input mode with module LEDs enabled:
    module should draw its full pad LED state.

When switching away from a custom input mode:
    Schwung should restore native LED pass-through and/or request native LED refresh.
```

If full native LED refresh is not easy, Schwung should at least clear or release ownership of the playable pad LEDs so Move can repopulate them naturally.

---

## 7. Blocking Native Pad Events

When a custom input mode is active for the current track:

```text
Cable-0 pad note-on/off events should be removed, zeroed, or otherwise hidden from MoveOriginal before Move sees them.
```

Only the pad/key layer should be blocked.

Do not block:

```text
Step buttons
Track buttons
Transport
Jog wheel
Volume knob
Device knobs
Menu/back/shift
LED feedback, unless LED override is explicitly enabled
```

This should allow the user to continue operating Move normally while only replacing the key/pad note-entry system.

Suggested shim behavior:

```c
bool should_overtake_pad_input(int active_track, const midi_event_t *ev) {
    if (!is_cable0(ev)) return false;
    if (!is_pad_note_event(ev)) return false;
    if (track_input_mode[active_track] == INPUT_MODE_NATIVE) return false;
    return true;
}
```

Then:

```c
if (should_overtake_pad_input(active_track, ev)) {
    enqueue_raw_input_event(active_track, ev);
    clear_or_drop_event_before_move_sees_it(ev);
}
```

Important:

```text
Note-off must be blocked if note-on was blocked.
```

Otherwise Move may receive orphan note-offs, or worse, it may still partially interact with native note state.

### 7.1 Pad LED Override / Pass-Through

Custom input modes may need to control the pad LEDs.

For example:

```text
True Chromatic:
    show root notes brighter
    show scale notes dim
    show non-scale notes off

32 Drum Pads:
    show occupied pads
    flash pad on trigger
    use colors for sample groups

Chord Pads:
    color pads by chord quality
    highlight currently held chords
```

Because custom input modes may completely change the meaning of each pad, the native Move pad LED feedback may no longer be correct.

Therefore, the input override system should support pad LED behavior as part of the input module API.

Each input module should be able to choose one of these LED policies:

```text
pass_through
    Do not modify Move's native pad LED output.
    Native LEDs behave as they normally would.

suppress
    Block native pad LED updates for the playable pad grid.
    Useful if the module wants the pads dark or externally controlled.

module
    Block/intercept native pad LED updates for the playable pad grid.
    Let the selected input module provide replacement LED colors/states.

hybrid
    Let the module receive native LED state and optionally modify it before display.
```

Recommended first implementation:

```text
pass_through
module
```

`pass_through` should be the safest default.

`module` should allow the input module to fully replace the pad LED state for the playable pads.

Suggested setting:

```json
{
  "id": "led_mode",
  "name": "LEDs",
  "type": "enum",
  "default": "pass_through",
  "options": [
    { "value": "pass_through", "label": "Native" },
    { "value": "module", "label": "Module" }
  ]
}
```

This setting can either be:

```text
A host-level setting stored per track
```

or:

```text
A module.json param defined by the input module
```

Recommended:

```text
Expose LED mode as a host-level setting with module-level override support later.
```

Reason:

```text
Users may want to use a custom note layout while keeping native LED behavior.
Other modules may require custom LED behavior to make sense.
```

### 7.2 LED Event Flow

The shim should distinguish between:

```text
Pad input events:
    Hardware pad press/release from Move hardware into MoveOriginal.

Pad LED output events:
    MoveOriginal/native UI LED updates going back out to the pad hardware.
```

When a track uses Native input mode:

```text
Pad input:
    pass through unchanged

Pad LEDs:
    pass through unchanged
```

When a track uses custom input mode with `led_mode = pass_through`:

```text
Pad input:
    block native pad input
    route through Schwung input module
    inject resulting MIDI through cable 2

Pad LEDs:
    pass through native LED updates unchanged
```

When a track uses custom input mode with `led_mode = module`:

```text
Pad input:
    block native pad input
    route through Schwung input module
    inject resulting MIDI through cable 2

Pad LEDs:
    block or intercept native pad LED updates for playable pads
    let input module provide replacement LED state
```

Potential function shape:

```c
bool should_overtake_pad_leds(int active_track) {
    input_config_t *cfg = &input_configs[active_track];

    if (cfg->mode == INPUT_MODE_NATIVE)
        return false;

    if (cfg->led_mode == LED_MODE_PASS_THROUGH)
        return false;

    if (cfg->led_mode == LED_MODE_MODULE)
        return true;

    return false;
}
```

For `hybrid` later:

```text
native_led_state
→ input module
→ modified_led_state
→ hardware
```

But this should not be required for the first implementation.

---

## 8. Active Track Handling

The system must know which Move track is active at the time of pad input.

Implementation requirement:

```text
Maintain shim-side active_track state by observing Move track button events or existing Schwung track focus state.
```

The track-aware input setting should be evaluated against the active track:

```c
int track = schwung_get_active_move_track(); // 0-3
input_config_t *cfg = &input_configs[track];
```

If track detection is uncertain, fail safely:

```text
If active track cannot be determined:
    use Native mode
```

This prevents accidental global pad blocking.

---

## 9. MIDI Channel Routing

For custom input modes, output MIDI must be routed to the correct Move track.

Default mapping:

```text
Track 1 → MIDI channel 1
Track 2 → MIDI channel 2
Track 3 → MIDI channel 3
Track 4 → MIDI channel 4
```

Internally, MIDI channel values in status bytes are usually zero-based:

```text
Track 1 → channel 0
Track 2 → channel 1
Track 3 → channel 2
Track 4 → channel 3
```

But this should ideally respect Move's actual MIDI input channel settings where possible.

Initial implementation:

```text
Use default channel = active_track + 1.
```

Future improvement:

```text
Read actual Move track MIDI input channel from set state or internal runtime state.
```

---

## 10. Step 1 Scope: Input Modes Only

The first implementation should **not** implement arpeggiators, timing, repeat, clock sync, or generative sequencing.

Step 1 should only prove:

```text
Raw pad press
→ Schwung input module maps it to note(s)
→ Move native instrument plays those notes
→ Move native pad/key system does not also play the original note
→ track-specific mode switching works
→ set-specific settings persist
→ optional LED pass-through/module replacement works
```

Minimum useful input modules for testing:

### 10.1 Native

```text
Built-in pseudo-mode.
No custom module loaded.
All pad/key events pass through unchanged.
```

### 10.2 True Chromatic

```text
Non-overlapping chromatic layout.
Each physical pad maps to one unique MIDI note.
No repeated notes between rows.
```

Example 32-pad map:

```text
Pad 0  → C2
Pad 1  → C#2
Pad 2  → D2
...
Pad 31 → G4
```

This is the simplest validation mode because each pad produces exactly one deterministic note.

### 10.3 32 Drum Pads

```text
Each of 32 pads maps to a drum note.
Useful for avoiding Move's native 16-pad drum limitation.
```

Example:

```text
Pad 0  → MIDI note 36
Pad 1  → MIDI note 37
...
Pad 31 → MIDI note 67
```

### 10.4 Chord Pads

```text
Each pad emits multiple note-ons and matching note-offs.
```

This mode is important because it validates multi-note output and stuck-note handling.

Example:

```text
Pad 0 → C major: 60, 64, 67
Pad 1 → D minor: 62, 65, 69
Pad 2 → E minor: 64, 67, 71
```

---

## 11. Note-On / Note-Off Ownership

Input modules that emit transformed notes must remember which notes were emitted for each held pad.

Example:

```text
Pad 0 note-on:
    emits 60, 64, 67
    stores active_notes[pad0] = [60, 64, 67]

Pad 0 note-off:
    emits note-off for 60, 64, 67
    clears active_notes[pad0]
```

Never recompute note-offs from current settings alone.

Reason:

```text
The user might change root/scale/layout while a pad is held.
The note-off must match the notes that were actually emitted on note-on.
```

Suggested structure:

```c
typedef struct {
    uint8_t count;
    uint8_t notes[8];
    uint8_t channel;
    bool active;
} held_pad_notes_t;

held_pad_notes_t held[32];
```

---

## 12. Panic / Cleanup

Add a panic routine for every input mode:

```text
When switching input mode
When switching track
When unloading module
When disabling Schwung custom input
When crashing/recovering
When transport stop is pressed, if appropriate
```

Panic should send note-offs for all active notes owned by the input module.

```c
for each pad:
    if held[pad].active:
        emit_note_off for every held note
        clear held[pad]
```

This is especially important because the feature deliberately blocks Move's native pad input. Any missed note-off could create stuck notes in the native Move instrument.

---

## 13. Shadow UI Menu Design

### 13.1 Entry Shortcut

Add:

```text
Shift + Volume Touch + Step 9
→ Input Mode menu
```

This should be parallel to existing Shadow Mode shortcuts.

### 13.2 Menu Layout

Suggested menu:

```text
Input Mode
Track: 1
Mode: Native
LEDs: Native
```

When selecting mode:

```text
Native
True Chromatic
32 Drum Pads
Chord Pads
...
```

If the selected input module exposes params:

```text
Input Mode
Track: 1
Mode: True Chromatic
LEDs: Module
Root: C2
Layout: Linear
Velocity: Native
```

### 13.3 Track Awareness in UI

The menu should clearly show the currently edited track:

```text
Input Mode — Track 2
Mode: Chord Pads
LEDs: Module
```

If the user changes Move track while the menu is open, either:

```text
Option A: UI follows active track automatically.
Option B: UI keeps editing the originally selected track until reopened.
```

Recommended for Move-style immediacy:

```text
Option A: follow active track automatically.
```

But make it visually obvious, so users do not accidentally edit the wrong track.

---

## 14. Input Module Discovery

The Input Mode menu should discover installed modules with:

```json
"component_type": "input_mode"
```

or:

```json
"capabilities": {
  "input_mode": true
}
```

Then build the mode list:

```text
Native
<installed input module 1>
<installed input module 2>
...
```

Native should not require a module folder.

---

## 15. Suggested File / Code Areas

Based on current docs, the implementation will likely touch:

```text
src/schwung_shim.c
    Raw cable-0 pad filtering
    Pad LED output filtering/interception
    Shortcut detection for Shift+Vol+Step9
    Active track tracking
    Custom input-mode dispatch
    MIDI injection drain integration

src/host/shadow_constants.h
    New SHM structs / constants if needed

src/shadow/shadow_ui.js
    New Input Mode menu
    Module discovery/filtering
    Track-aware and set-aware setting UI
    Reuse existing parameter UI base for module.json params

src/modules/input_modes/
    Optional built-in test modules:
        true-chromatic
        drum32
        chord-pads

docs/
    New INPUT_MODES.md developer docs
    Update MODULES.md
    Update MANUAL.md
```

### 15.1 New API Documentation Requirement

This feature introduces a new public module API and must be documented clearly for module authors.

Add a new developer document:

```text
docs/INPUT_MODES.md
```

This document should explain:

```text
What input modes are
How they differ from normal MIDI FX
How they differ from generator modules
How to define an input mode in module.json
How track-aware state works
How set-aware saving works
How raw pad events are represented
How MIDI output should be emitted
How note-on/note-off ownership works
How LED pass-through/override works
How to avoid stuck notes
How to avoid cable-2 echo loops
How to test an input mode
```

Suggested outline:

```markdown
# Input Mode Modules

## Overview

## Native vs Custom Input Modes

## Signal Flow

## module.json Requirements

## Parameters

## Track-Aware State

## Set-Aware Saving

## Raw Pad Event API

## MIDI Output API

## Pad LED API

## Note Ownership / Stuck Note Prevention

## Real-Time Safety Rules

## Example: True Chromatic

## Example: 32 Drum Pads

## Example: Chord Pads

## Debugging

## Common Pitfalls
```

Also update:

```text
docs/MODULES.md
```

with a short section describing the new module type:

```json
{
  "component_type": "input_mode",
  "capabilities": {
    "input_mode": true,
    "midi_out": true,
    "led_out": true
  }
}
```

And update the user manual/docs with the new shortcut:

```text
Shift + Volume Touch + Step 9
→ Input Mode
```

Later, when Step 2 is implemented, add:

```text
Shift + Volume Touch + Step 10
→ Timing / Modifiers
```

---

## 16. Modifier / Timing Stage — Step 2 Design

Do not implement this first, but design Step 1 so Step 2 can be inserted cleanly.

Future desired flow:

```text
Raw pad input
→ Input Mode
→ Timing / Modifier Chain
→ MIDI injection
→ Move native instrument
```

Examples:

```text
Arpeggiator
Note repeat
Euclidean input sequencer
Strummer
Chord rhythmizer
Humanizer
Scale quantizer
Velocity processor
```

The important design idea:

```text
Input modes create an initial MIDI stream.
Modifiers transform that MIDI stream.
```

This means existing MIDI FX modules may be reusable if they can be hosted in this pre-native-input path.

Future internal interface:

```text
input_mode.process(raw_pad_event) → midi_events[]
modifier.process(midi_events[]) → midi_events[]
inject(midi_events[])
```

For Step 2, the Timing / Modifiers menu would be:

```text
Shift + Volume Touch + Step 10
→ Timing / Modifiers
```

Default:

```text
None / Native
```

Potential first modifier:

```text
Simple Arp
```

The proposed pre-native modifier layer should avoid feeding injected cable-2 echoes back into the modifier stage.

---

## 17. Optional Future: Read Move Native Scale / Key

Optional feature:

```text
Read Ableton Move's native set scale/key settings and expose them to input modules.
```

Use case:

```text
If the Move set is already in A minor, a custom chord/scaled input mode should be able to use A minor without the user setting it again in Schwung.
```

Possible approaches:

### 17.1 Read from Ableton Set File

Parse the current Move set/project file and extract:

```text
Root note
Scale/mode
Track input channel, if stored
Maybe selected layout/mode
```

Pros:

```text
Does not require deeper runtime hooks.
Can probably be done outside the real-time path.
```

Cons:

```text
May not update instantly when the user changes scale.
Requires knowing set-file format.
Must handle unsaved changes.
```

### 17.2 Runtime Observation

Observe native UI or internal state changes if accessible.

Pros:

```text
Can reflect changes immediately.
```

Cons:

```text
Likely more fragile.
Requires reverse engineering runtime state.
```

Recommended phase:

```text
Defer this until Step 1 is stable.
For Step 1, input modules should have their own root/scale params.
```

Later, add a param option:

```text
Scale Source:
    Manual
    Move Set
```

---

## 18. Real-Time Safety Requirements

The shim-side input interception path must be real-time safe.

Do not do these inside SPI/audio callback paths:

```text
malloc/free
file reads/writes
JSON parsing
module discovery
blocking locks
network I/O
large logging bursts
```

Allowed:

```text
fixed-size ring buffer writes
simple array lookups
atomic state reads
small MIDI packet transforms
bounded loops
```

Schwung's architecture docs call out that SPI callback code should be non-allocating and non-blocking, with logging drained by a background thread.

---

## 19. MIDI Injection Limits

Respect the existing injection limits.

The current docs say the shim drains the MIDI injection shared-memory ring in bounded batches per SPI tick. This is fine for normal input modes:

```text
Single-note pad mode:
    1 note-on + 1 note-off per pad

Chord pad mode:
    maybe 3-6 note-ons + 3-6 note-offs per pad

32 drum pads:
    low packet count per hit
```

But the implementation should avoid unbounded bursts:

```text
Limit max emitted notes per pad.
Suggested first-pass max: 8 notes per pad.
```

---

## 20. Testing Plan

### 20.1 Native Safety Test

```text
Set all tracks to Native.
Verify Move behaves exactly like stock Move.
No pad blocking.
No MIDI injection.
No stuck notes.
Native LEDs pass through.
```

### 20.2 Per-Track Override Test

```text
Track 1: True Chromatic
Track 2: Native
Track 3: 32 Drum Pads
Track 4: Native
```

Expected:

```text
Track 1 pads use Schwung chromatic layout.
Track 2 pads use Move native layout.
Track 3 pads use Schwung drum mapping.
Track 4 pads use Move native layout.
Switching tracks changes behavior immediately.
```

### 20.3 Native Blocking Test

With custom input mode active:

```text
Press a pad that would normally play Move's native note.
Verify only the Schwung-mapped note plays.
Verify the original native note does not also play.
```

### 20.4 Chord Note-Off Test

```text
Enable Chord Pads.
Hold pad.
Change chord/root setting while holding.
Release pad.
```

Expected:

```text
The originally emitted chord receives correct note-offs.
No stuck notes.
```

### 20.5 Mode Switch Panic Test

```text
Hold pad in custom mode.
While holding, switch mode to Native.
```

Expected:

```text
All notes emitted by custom mode are turned off.
Native mode resumes cleanly.
```

### 20.6 Track Switch Panic Test

```text
Hold pad on Track 1 custom mode.
Switch to Track 2.
Release pad.
```

Expected behavior should be defined explicitly.

Recommended:

```text
On track switch, panic all active notes owned by the old track's input module.
```

This avoids ambiguous cross-track held notes.

### 20.7 Injection Debug Test

Use existing Schwung debug logging:

```bash
ssh ableton@move.local 'touch /data/UserData/schwung/debug_log_on'
ssh ableton@move.local 'tail -f /data/UserData/schwung/debug.log'
```

### 20.8 Set-Specific Save/Load Test

```text
Open Set A.
Set Track 2 to True Chromatic.
Save/reload set.
Verify Track 2 returns to True Chromatic.

Open Set B.
Verify Track 2 is Native unless explicitly configured.
```

### 20.9 LED Pass-Through Test

```text
Set Track 1 to custom input mode.
Set LEDs to Native/pass_through.
Verify pad LED behavior remains native.
```

### 20.10 LED Module Override Test

```text
Set Track 1 to custom input mode.
Set LEDs to Module.
Verify native pad LED updates for playable pads are blocked/intercepted.
Verify module draws replacement LED state.
Switch back to Native.
Verify native LED behavior returns.
```

---

## 21. Acceptance Criteria for Step 1

Step 1 is complete when:

```text
1. There is a new Shift+Vol+Step9 Input Mode menu.
2. Each Move track has an independently saved input mode.
3. All tracks default to Native.
4. Native mode is indistinguishable from current Move behavior.
5. At least one custom input mode can intercept pad input and inject MIDI to the active Move track.
6. Native pad notes are blocked while custom input mode is active.
7. Non-pad controls continue to work normally.
8. Custom input modes can emit multiple notes per pad.
9. Note-off ownership is correct and no stuck notes occur.
10. Settings persist across restart/reload.
11. Input mode settings are saved as part of Schwung's set-specific state.
12. Loading a different Move set restores that set's input modes.
13. Missing/invalid saved state falls back to Native per track.
14. Input module parameters are defined through module.json wherever possible.
15. The Input Mode menu reuses the existing Schwung parameter UI base.
16. Input modules can optionally claim pad LED ownership.
17. Native pad LEDs pass through unchanged by default.
18. When LED mode is set to Module, native pad LED updates for playable pads are blocked/intercepted.
19. Input modules can draw replacement pad LED states.
20. The new input mode module API is documented in docs/INPUT_MODES.md.
21. docs/MODULES.md is updated with the new input_mode component type.
```

---

## 22. Suggested Minimal First Implementation

Build this in the smallest useful slice.

### Phase A — Hardcoded Proof of Concept

```text
No module discovery yet.
No full UI params yet.
Only one hardcoded mode: True Chromatic.
But still use the final track/set-aware state shape.
```

Implement:

```text
Track input config:
    Native / True Chromatic

Shortcut:
    Shift+Vol+Step9 opens simple selector

Shim:
    If active track mode == True Chromatic:
        block cable-0 pad notes
        map pad note 68-99 to MIDI note 48-79
        inject on active_track channel
```

### Phase A.1 — Set-State and UI Base Compliance

Even the hardcoded proof-of-concept should be shaped so it does not become throwaway architecture.

The first implementation may hardcode one test mode, but it should still use the same state path that the final implementation will use:

```text
current set state
→ per-track input mode config
→ active track lookup
→ input override behavior
```

Do not implement input mode state as a temporary global flag only.

Minimum acceptable POC state:

```json
{
  "input_modes": {
    "tracks": {
      "1": {
        "mode": "native"
      },
      "2": {
        "mode": "true_chromatic_poc"
      },
      "3": {
        "mode": "native"
      },
      "4": {
        "mode": "native"
      }
    }
  }
}
```

Similarly, the first UI can be simple, but should be built on the same menu/parameter primitives used by existing Schwung module UI.

Avoid creating a separate one-off custom UI system for input modes.

### Phase B — Module Interface

Add:

```text
component_type: input_mode
module discovery
module loading
module params
held-note ownership API
optional LED output API
```

### Phase C — Built-In Test Modules

Add:

```text
true-chromatic
drum32
chord-pads
```

### Phase D — Prep for Modifiers

Refactor internal flow to:

```text
input_events
→ input_mode_output
→ modifier_stage_placeholder
→ inject
```

The placeholder should simply pass events through for now.

---

## 23. Important Design Warnings

### 23.1 Do Not Reuse Cable-2 Echoes as Input

Injected cable-2 MIDI can be echoed by Move on MIDI_OUT cable 2. Historical MIDI injection work notes describe echo-loop/cascade risks.

For this system:

```text
Only raw cable-0 pad events should feed the input mode.
Never feed injected cable-2 echoes back into the input/modifier chain.
```

### 23.2 Fail Open to Native

If anything is invalid:

```text
missing module
bad config
module crash
active track unknown
injection unavailable
LED override unavailable
```

then:

```text
fall back to Native
do not block pad input
use LED pass-through
```

### 23.3 Do Not Block Step Buttons

Step buttons are also note-like hardware events, but they are not the playable pad/key grid in this design. Restrict first-pass interception to the known pad/key note range.

### 23.4 Separate Input Ownership From LED Ownership

Custom input mode does not automatically mean custom LED mode.

These should be independently controllable:

```text
Input behavior:
    Native / Custom

LED behavior:
    Native pass-through / Module override
```

This lets a user use a custom input layout without forcing custom LED behavior.

---

## 24. Final Mental Model

This feature adds a new pre-native Schwung input layer:

```text
Native:
Move hardware → Move interpretation → Move instrument

Custom:
Move hardware → Schwung input mode → MIDI injection → Move instrument
```

The key engineering challenge is not the MIDI generation itself. Schwung already has a cable-2 injection path. The hard part is making the override feel native and safe:

```text
track-aware
set-aware
module.json/UI-base compatible
default-off/native
no stuck notes
no echo loops
no accidental blocking of transport/steps/knobs
optional pad LED ownership
real-time safe
persistent
easy for modules to extend
documented as a new public API
```

Once Step 1 is stable, Step 2 can add timing/modifier modules by inserting a MIDI FX-like processing layer between the custom input mode and the cable-2 injection output.

---

## Reference Docs To Review During Implementation

- `docs/ARCHITECTURE.md`
- `docs/ADDRESSING_MOVE_SYNTHS.md`
- `docs/MODULES.md`
- `docs/MIDI_INJECTION.md`
- `docs/SPI_PROTOCOL.md`
- `docs/REALTIME_SAFETY.md`
- `src/schwung_shim.c`
- `src/shadow/shadow_ui.js`
- `src/host/shadow_constants.h`
