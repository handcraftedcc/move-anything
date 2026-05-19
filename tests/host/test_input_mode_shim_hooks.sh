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
require 'input_track_roots\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track input roots'
require 'input_track_scales\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track input scales'
require 'input_track_octaves\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track input octaves'
require 'input_track_root_octaves\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track input drum octaves'
require 'input_track_index_2\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track chord index 2'
require 'input_track_index_3\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track chord index 3'
require 'input_track_colors\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track color framework'
require 'input_track_colors\[i\]' src/schwung_shim.c 'track color cache populated from LED state'
require 'schwung_input_mode_handle_midi' src/schwung_shim.c 'input mode MIDI handler'
require 'schwung_input_mode_handle_button' src/schwung_shim.c 'input mode button handler'
require 'schwung_input_mode_set_track_config' src/schwung_shim.c 'input mode config sync'
require 'schwung_input_mode_set_track_module' src/schwung_shim.c 'input mode module sync'
require 'input_track_module_ids\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track input module ids'
require 'input_track_param_keys\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track input param keys'
require 'input_track_param_values\[SHADOW_UI_SLOTS\]' src/host/shadow_constants.h 'per-track input param values'
require 'light_packets' src/host/input_mode_api_v1.h 'input mode light result packets'
require 'update_leds' src/host/input_mode_api_v1.h 'input mode update LEDs API'
require 'param_updates' src/host/input_mode_api_v1.h 'input mode param update result packets'
require 'shadow_input_mode_apply_param_updates' src/schwung_shim.c 'input mode param updates returned to shared state'
require 'shadow_input_mode_queue_leds_if_allowed' src/schwung_shim.c 'input mode light result forwarding'
require 'shadow_midi_force_defer' src/schwung_shim.c 'forced MIDI inject defer'
require 'shadow_sentry_mode_watcher_start' src/schwung_shim.c 'Sentry MainMode watcher startup'
require 'shadow_sentry_mode_watcher_thread' src/schwung_shim.c 'Sentry MainMode watcher thread'
require 'schwung_sentry_mode_parse_buffer' src/schwung_shim.c 'Sentry breadcrumb parser wired into shim'
require 'shadow_input_mode_apply_sentry_gate' src/schwung_shim.c 'input override gate follows Sentry MainMode'
require 'shadow_input_mode_led_allows_override' src/schwung_shim.c 'input override runtime gate'
require 'shadow_input_mode_custom_pad_colors' src/schwung_shim.c 'input mode custom LED cache'
require 'shadow_input_mode_record_custom_led' src/schwung_shim.c 'input mode records custom LED writes'
require 'shadow_input_mode_should_suppress_move_led' src/schwung_shim.c 'input mode suppresses native pad LED writes while custom LEDs own pads'
require 'shadow_input_mode_clear_led_ownership' src/schwung_shim.c 'input mode clears LED ownership without always clearing hardware'
require 'shadow_input_mode_begin_native_led_handoff' src/schwung_shim.c 'input mode only clears stale LEDs when leaving custom note ownership'
require 'schwung_input_mode_update_leds' src/schwung_shim.c 'input mode update LEDs callback'
require 'suppress_move_led' src/host/shadow_led_queue.h 'LED queue exposes native LED suppression hook'
require 'host\.suppress_move_led' src/host/shadow_led_queue.c 'LED queue applies native LED suppression hook'
require 'led_queue_clear_pending_pad_leds' src/host/shadow_led_queue.h 'LED queue can clear pending custom pad LEDs'
require 'led_queue_clear_pending_pad_leds' src/schwung_shim.c 'input mode clears pending custom pad LEDs when handing pads back'
require 'led_queue_queue_native_pad_leds' src/host/shadow_led_queue.h 'LED queue can queue native pad LED handoff restore'
require 'led_queue_queue_native_pad_leds' src/schwung_shim.c 'input mode restores native pad LEDs during custom handoff'
require 'shadow_input_mode_disable_runtime_gate' src/schwung_shim.c 'input override disabled during overtake'
require 'shadow_input_mode_pad_held' src/schwung_shim.c 'held pad state tracked for input modules'
require 'led_queue_get_note_led_color' src/schwung_shim.c 'native LED cache used for track colors and pad snapshots'
require '/\* Track selectors \*/' src/host/shadow_led_queue.c 'track selector LEDs are note-addressed'
require '/\* Step UI under steps \*/' src/host/shadow_led_queue.c 'step UI CC LEDs are tracked'
require 'shadow_input_mode_note_navigation_hint' src/schwung_shim.c 'navigation events are logged while Sentry watcher owns mode detection'
require 'menu-release' src/schwung_shim.c 'Menu release navigation hint'
require 'd1 == 16' src/schwung_shim.c 'Shift+Step1 navigation hint'
require 'd1 == 24' src/schwung_shim.c 'Shift+Vol+Step9 shortcut'
require 'src/host/input_mode.c' scripts/build.sh 'input mode in shim build'
require 'src/host/sentry_mode.c' scripts/build.sh 'Sentry mode parser in shim build'
require 'input_modes' src/shadow/shadow_ui.js 'input_modes persistence root'
require '"input_mode"' src/modules/input_modes/drum32/module.json 'drum input module'
require '"dsp"' src/modules/input_modes/drum32/module.json 'drum input module dsp'
require 'schwung_input_module_init_v1' src/modules/input_modes/drum32/dsp/drum32.c 'drum input module entrypoint'
require '"root_octave"' src/modules/input_modes/drum32/module.json 'drum root octave parameter'
require '"input_mode"' src/modules/input_modes/chromatic/module.json 'chromatic input module'
require '"dsp"' src/modules/input_modes/chromatic/module.json 'chromatic input module dsp'
require 'schwung_input_module_init_v1' src/modules/input_modes/chromatic/dsp/chromatic.c 'chromatic input module entrypoint'
require '"scale"' src/modules/input_modes/chromatic/module.json 'chromatic scale parameter'
require '"root"' src/modules/input_modes/chromatic/module.json 'chromatic root parameter'
require '"input_mode"' src/modules/input_modes/chord-pads/module.json 'chord input module'
require '"dsp"' src/modules/input_modes/chord-pads/module.json 'chord input module dsp'
require 'schwung_input_module_init_v1' src/modules/input_modes/chord-pads/dsp/chord_pads.c 'chord input module entrypoint'
require '"index_2"' src/modules/input_modes/chord-pads/module.json 'chord index 2 parameter'
require '"index_3"' src/modules/input_modes/chord-pads/module.json 'chord index 3 parameter'
require 'build/modules/input_modes/drum32/dsp.so' scripts/build.sh 'drum input module build'
require 'build/modules/input_modes/chromatic/dsp.so' scripts/build.sh 'chromatic input module build'
require 'build/modules/input_modes/chord-pads/dsp.so' scripts/build.sh 'chord input module build'
require 'input_modes' src/shadow/shadow_ui.c 'input_modes module scan'
require 'input_modes' src/host/module_manager.c 'input_modes module manager scan'
require 'input_mode' scripts/install.sh 'input_mode installer category'
require 'set_root_note' src/host/shadow_constants.h 'set root note shared state'
require 'set_scale' src/host/shadow_constants.h 'set scale shared state'
require 'set_melodic_layout' src/host/shadow_constants.h 'set melodic layout shared state'
require 'shadow_refresh_set_musical_context' src/host/shadow_set_pages.c 'set musical context refresh'
require 'rootNote' src/host/shadow_set_pages.c 'root note parser'
require 'melodicLayout' src/host/shadow_set_pages.c 'melodic layout parser'
require 'shadow_set_loaded_generation' src/host/shadow_set_pages.h 'set-change generation exported for runtime reset'
require 'shadow_set_loaded_generation\+\+' src/host/shadow_set_pages.c 'set-change generation increments on loaded set'
require 'shadow_input_mode_handle_set_change' src/schwung_shim.c 'input mode runtime resets on set change'
require 'shadow_input_mode_reset_runtime_state_for_set_change' src/schwung_shim.c 'input mode DSP state resets on set change'
require 'SHADOW_UI_FLAG_SET_CHANGED' src/schwung_shim.c 'input modes are suspended while set reload flag is pending'

sentry_gate_direct_calls="$(rg -n 'shadow_input_mode_apply_sentry_gate\(' src/schwung_shim.c | wc -l | tr -d ' ')"
if [[ "$sentry_gate_direct_calls" != "2" ]]; then
    echo "FAIL: Sentry gate should have one definition and one SPI call site" >&2
    exit 1
fi

echo "PASS: input mode shim hooks present"
