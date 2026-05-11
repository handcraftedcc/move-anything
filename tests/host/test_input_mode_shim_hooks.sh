#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

require() {
    local pattern="$1"
    local file="$2"
    local label="$3"
    if ! rg -q "$pattern" "$file"; then
        echo "FAIL: missing $label in $file" >&2
        exit 1
    fi
}

require 'SHADOW_UI_FLAG2_JUMP_TO_INPUT_MODE' src/host/shadow_constants.h 'input mode UI flag'
require 'input_track_modes\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track input modes'
require 'input_led_modes\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track LED modes'
require 'schwung_input_mode_handle_midi' src/schwung_shim.c 'input mode MIDI handler'
require 'shadow_midi_force_defer' src/schwung_shim.c 'forced MIDI inject defer'
require 'schwung_input_mode_classify_led_grid' src/schwung_shim.c 'input override LED grid classifier'
require 'schwung_input_mode_detect_led_grid_mode' src/schwung_shim.c 'input override four-state LED grid detector'
require 'shadow_input_mode_led_grid_mode' src/schwung_shim.c 'input override tracks detected LED grid mode'
require 'shadow_input_mode_led_allows_override' src/schwung_shim.c 'input override LED-classifier gate'
require 'shadow_input_mode_disable_runtime_gate' src/schwung_shim.c 'input override disabled during overtake'
require 'shadow_input_mode_pad_held' src/schwung_shim.c 'held pad colors ignored for classifier'
require 'led_queue_get_note_led_color' src/schwung_shim.c 'pad LED cache used for classifier'
require 'led_queue_get_pad_led_generation' src/schwung_shim.c 'pad LED repaint generation used for classifier timing'
require 'led_queue_get_pad_led_generation' src/host/shadow_led_queue.h 'pad LED repaint generation exported'
require 'led_queue_clear_pad_led_cache' src/schwung_shim.c 'pad LED cache cleared before navigation classifier sample'
require 'led_queue_clear_pad_led_cache' src/host/shadow_led_queue.h 'pad LED cache clear exported'
require 'clear_pad_cache' src/schwung_shim.c 'track samples can preserve partial LED repaint cache'
require '/\* Track selectors \*/' src/host/shadow_led_queue.c 'track selector LEDs are note-addressed'
require '/\* Step UI under steps \*/' src/host/shadow_led_queue.c 'step UI CC LEDs are tracked'
require 'shadow_input_mode_schedule_led_gate_update' src/schwung_shim.c 'LED classifier updates scheduled by navigation'
require 'shadow_input_mode_schedule_track_led_gate_update' src/schwung_shim.c 'track navigation uses confirmed LED classifier re-entry'
require 'shadow_input_mode_maybe_update_led_gate' src/schwung_shim.c 'LED classifier is not sampled continuously'
require 'shadow_input_mode_led_confirm_play' src/schwung_shim.c 'track LED classifier waits for confirmed play grid'
require 'menu-release' src/schwung_shim.c 'Menu release also schedules LED classifier trigger'
require 'd1 == 16' src/schwung_shim.c 'Shift+Step1 LED classifier trigger'
require 'd1 == 24' src/schwung_shim.c 'Shift+Vol+Step9 shortcut'
require 'src/host/input_mode.c' scripts/build.sh 'input mode in shim build'
require 'input_modes' src/shadow/shadow_ui.js 'input_modes persistence root'
require '"input_mode"' src/modules/input_modes/test/module.json 'test input module'
require '"None/Native"' src/modules/input_modes/test/module.json 'native test layout'
require '"32 Drum Pads"' src/modules/input_modes/test/module.json 'drum pads test layout'
require '"Chromatic"' src/modules/input_modes/test/module.json 'chromatic test layout'
require '"Chords"' src/modules/input_modes/test/module.json 'chords test layout'
require 'input_modes' src/shadow/shadow_ui.c 'input_modes module scan'
require 'input_modes' src/host/module_manager.c 'input_modes module manager scan'
require 'input_mode' scripts/install.sh 'input_mode installer category'
require 'set_root_note' src/host/shadow_constants.h 'set root note shared state'
require 'set_scale' src/host/shadow_constants.h 'set scale shared state'
require 'set_melodic_layout' src/host/shadow_constants.h 'set melodic layout shared state'
require 'shadow_refresh_set_musical_context' src/host/shadow_set_pages.c 'set musical context refresh'
require 'rootNote' src/host/shadow_set_pages.c 'root note parser'
require 'melodicLayout' src/host/shadow_set_pages.c 'melodic layout parser'

led_gate_direct_calls="$(rg -n '= shadow_input_mode_update_led_gate\(' src/schwung_shim.c | wc -l | tr -d ' ')"
if [[ "$led_gate_direct_calls" != "1" ]]; then
    echo "FAIL: LED classifier should only be called through scheduled sampler" >&2
    exit 1
fi

echo "PASS: input mode shim hooks present"
