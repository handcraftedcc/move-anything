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
require 'function loadInputModeModuleMetadata' src/shadow/shadow_ui.js 'input mode category-local metadata loading'
require 'function paramsFromUiHierarchy' src/shadow/shadow_ui.js 'input mode ui_hierarchy param fallback'
require 'LEGACY_INPUT_MODE_TEST_MODULE' src/shadow/shadow_ui.js 'legacy test input module filter'
require 'normalizeInputModeModuleId' src/shadow/shadow_ui.js 'legacy input module migration'
require 'function enterInputModeSelector' src/shadow/shadow_ui.js 'input mode selector entry'
require 'function drawInputModeSelector' src/shadow/shadow_ui.js 'input mode selector drawing'
require 'function drawInputModeMenu' src/shadow/shadow_ui.js 'input mode drawing'
require 'SWAP_MODULE_ACTION' src/shadow/shadow_ui.js 'swap module action'
require 'inputModeSelectedModule' src/shadow/shadow_ui.js 'per-track selected input module'
require 'inputModeTrackParams' src/shadow/shadow_ui.js 'per-track input module params'
require 'inputModeEditMode' src/shadow/shadow_ui.js 'input mode parameter edit mode'
require 'beginInputModeParamEdit' src/shadow/shadow_ui.js 'input mode edit entry'
require 'endInputModeParamEdit' src/shadow/shadow_ui.js 'input mode edit exit'
require 'setInputTrackConfig' src/shadow/shadow_ui.js 'input mode config setter'
require 'setInputTrackModule' src/shadow/shadow_ui.js 'input mode module setter'
require 'setInputTrackParams' src/shadow/shadow_ui.js 'input mode generic param setter'
require 'shadow_set_input_track_config' src/shadow/shadow_ui.c 'input mode config binding'
require 'shadow_set_input_track_module' src/shadow/shadow_ui.c 'input mode module binding'
require 'shadow_set_input_track_param' src/shadow/shadow_ui.c 'input mode param binding'
require 'shadow_clear_input_track_params' src/shadow/shadow_ui.c 'input mode param clear binding'
require 'handleInputModeParamDelta' src/shadow/shadow_ui.js 'input mode numeric/enum parameter editing'
require 'capabilities && module.capabilities.ui_hierarchy' src/shadow/shadow_ui.js 'input mode reads midi-fx style capabilities ui_hierarchy'
require 'chain_params' src/modules/input_modes/drum32/module.json 'drum module chain params'
require '"root_octave"' src/modules/input_modes/drum32/module.json 'drum root octave param'
require 'chain_params' src/modules/input_modes/chromatic/module.json 'chromatic module chain params'
require '"root"' src/modules/input_modes/chromatic/module.json 'chromatic root param'
require '"scale"' src/modules/input_modes/chromatic/module.json 'chromatic scale param'
require 'chain_params' src/modules/input_modes/chord-pads/module.json 'chord module chain params'
require '"index_2"' src/modules/input_modes/chord-pads/module.json 'chord index 2 param'
require '"index_3"' src/modules/input_modes/chord-pads/module.json 'chord index 3 param'
require 'function saveInputModesToDir' src/shadow/shadow_ui.js 'input mode set persistence'
require 'input_modes.json' src/shadow/shadow_ui.js 'input mode state file'
require 'layoutToInputModeValue' src/shadow/shadow_ui.js 'layout enum to host mode mapping'
require 'function moduleToInputModeValue' src/shadow/shadow_ui.js 'module metadata to host mode mapping'
require 'true-chromatic' src/shadow/shadow_ui.js 'hyphenated chromatic module id alias'
require 'chord-pads' src/shadow/shadow_ui.js 'hyphenated chord module id alias'
require 'input_mode.mode' docs/INPUT_MODES.md 'input mode module metadata mode field'
require 'shadow_get_input_track_mode' src/shadow/shadow_ui.c 'input mode getter binding'
require 'shadow_set_input_track_mode' src/shadow/shadow_ui.c 'input mode setter binding'
require 'shadow_set_input_track_config' src/shadow/shadow_ui.c 'input mode config setter binding'
require 'shadow_get_ui_flags2' src/shadow/shadow_ui.c 'second flag getter binding'
require 'shadow_get_set_musical_context' src/shadow/shadow_ui.c 'set musical context binding'
require 'shadow_get_set_musical_context' docs/API.md 'set musical context API docs'

echo "PASS: input mode UI hooks present"
