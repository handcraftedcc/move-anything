# Param Lab (MIDI FX)

`param-lab` is a test fixture module for validating metadata-driven parameter UI behavior.

## Type Coverage

- `float`: `float_gain`
- `int`: `int_steps`
- `percentage`: `percentage_mix`
- `enum`: `enum_wave`
- `boolean`: `boolean_sync`
- `note`: `note_root`
- `time`: `time_div`
- `bipolar`: `bipolar_pan`
- `string`: `string_name`
- `filepath`: `filepath_sample`
- `waveform_position`: `waveform_position`
- `mod_target`: `mod_target_1`
- `button`: `button_ping`
- `canvas`: `canvas_env`

`canvas_env` uses `canvas.js#rotor` (rotating slit-disc controlled by jog wheel),
while the inactivity visualizer uses the rain overlay from
`canvas.js#createRainOverlay`.

Folder visibility is tested via `hide_actions_folder`, which conditionally hides
the `Actions + UI` level from the root.

`waveform_position` is paired with `filepath_sample` via `waveform_source_key`,
so editing Wave Pos in the hierarchy editor can render a cursor over a WAV preview
without any custom `ui.js`.

## Existing Dynamic Picker Coverage

- `module_picker`: `module_target`
- `parameter_picker`: `target_param`

## Callback Sink Params

These keys receive callback payload JSON from Shadow UI:

- `cb_on_enter`
- `cb_on_modify`
- `cb_on_exit`
- `cb_on_cancel`
- `cb_button`
- `cb_canvas`

The latest callback payload summary is mirrored to `last_callback`.
