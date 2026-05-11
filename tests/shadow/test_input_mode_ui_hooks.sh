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

require 'INPUT_MODE: "inputmode"' src/shadow/shadow_ui.js 'input mode view'
require 'SHADOW_UI_FLAG2_JUMP_TO_INPUT_MODE' src/shadow/shadow_ui.js 'input mode UI flag'
require 'function enterInputModeMenu' src/shadow/shadow_ui.js 'input mode entry'
require 'function refreshInputModeModules' src/shadow/shadow_ui.js 'input mode module discovery'
require 'function enterInputModeSelector' src/shadow/shadow_ui.js 'input mode selector entry'
require 'function drawInputModeSelector' src/shadow/shadow_ui.js 'input mode selector drawing'
require 'function drawInputModeMenu' src/shadow/shadow_ui.js 'input mode drawing'
require 'SWAP_MODULE_ACTION' src/shadow/shadow_ui.js 'swap module action'
require 'inputModeSelectedModule' src/shadow/shadow_ui.js 'per-track selected input module'
require 'inputModeTrackParams' src/shadow/shadow_ui.js 'per-track input module params'
require 'chain_params' src/modules/input_modes/test/module.json 'test module chain params'
require '"layout"' src/modules/input_modes/test/module.json 'test module layout param'
require 'function saveInputModesToDir' src/shadow/shadow_ui.js 'input mode set persistence'
require 'input_modes.json' src/shadow/shadow_ui.js 'input mode state file'
require 'layoutToInputModeValue' src/shadow/shadow_ui.js 'layout enum to host mode mapping'
require 'function moduleToInputModeValue' src/shadow/shadow_ui.js 'module metadata to host mode mapping'
require 'true-chromatic' src/shadow/shadow_ui.js 'hyphenated chromatic module id alias'
require 'chord-pads' src/shadow/shadow_ui.js 'hyphenated chord module id alias'
require 'input_mode.mode' docs/INPUT_MODES.md 'input mode module metadata mode field'
require 'shadow_get_input_track_mode' src/shadow/shadow_ui.c 'input mode getter binding'
require 'shadow_set_input_track_mode' src/shadow/shadow_ui.c 'input mode setter binding'
require 'shadow_get_ui_flags2' src/shadow/shadow_ui.c 'second flag getter binding'
require 'shadow_get_set_musical_context' src/shadow/shadow_ui.c 'set musical context binding'
require 'shadow_get_set_musical_context' docs/API.md 'set musical context API docs'

echo "PASS: input mode UI hooks present"
