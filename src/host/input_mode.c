#include "input_mode.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int valid_track(int track) {
    return track >= 0 && track < SCHWUNG_INPUT_MODE_TRACKS;
}

static int pad_index(uint8_t note) {
    if (note < 68 || note > 99) return -1;
    return (int)note - 68;
}

static uint8_t track_channel(int track) {
    return (uint8_t)(track & 0x0F);
}

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static int valid_mode(schwung_input_mode_t mode) {
    return mode == SCHWUNG_INPUT_MODE_NATIVE ||
           mode == SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC ||
           mode == SCHWUNG_INPUT_MODE_DRUM32 ||
           mode == SCHWUNG_INPUT_MODE_CHORD_PADS;
}

static int module_loaded(const schwung_input_mode_track_config_t *track) {
    return track && track->module.api;
}

static int legacy_layout_module_id(const char *module_id) {
    return module_id &&
           (strcmp(module_id, "chromatic") == 0 ||
            strcmp(module_id, "drum32") == 0 ||
            strcmp(module_id, "chord-pads") == 0);
}

static int module_active(const schwung_input_mode_track_config_t *track) {
    if (!module_loaded(track)) return 0;
    if (track->mode != SCHWUNG_INPUT_MODE_NATIVE) return 1;
    return !legacy_layout_module_id(track->module.module_id);
}

static int module_has_v2_panic(const schwung_input_mode_track_config_t *track) {
    return track &&
           track->module.api_v2 &&
           track->module.api_v2->struct_size >=
               offsetof(schwung_input_module_api_v2_t, panic) + sizeof(track->module.api_v2->panic) &&
           track->module.api_v2->panic;
}

static int module_has_v2_tick(const schwung_input_mode_track_config_t *track) {
    return track &&
           track->module.api_v2 &&
           track->module.api_v2->struct_size >=
               offsetof(schwung_input_module_api_v2_t, tick) + sizeof(track->module.api_v2->tick) &&
           track->module.api_v2->tick;
}

static int result_has_output(const schwung_input_mode_result_t *result) {
    return result &&
           (result->count > 0 ||
            result->light_count > 0 ||
            result->param_update_count > 0);
}

schwung_input_mode_config_t schwung_input_mode_default_config(void) {
    schwung_input_mode_config_t config;
    memset(&config, 0, sizeof(config));
    config.scale = SCHWUNG_INPUT_SCALE_MAJOR;
    config.index_2 = 2;
    config.index_3 = 4;
    return config;
}

static void normalize_config(schwung_input_mode_config_t *config) {
    if (!config) return;
    config->root = (uint8_t)(config->root % 12);
    config->octave = (int8_t)clamp_int(config->octave, -5, 5);
    config->root_octave = (int8_t)clamp_int(config->root_octave, -5, 5);
    if (config->scale > SCHWUNG_INPUT_SCALE_CHROMATIC) {
        config->scale = SCHWUNG_INPUT_SCALE_MAJOR;
    }
    config->index_2 = (uint8_t)clamp_int(config->index_2, 0, 15);
    config->index_3 = (uint8_t)clamp_int(config->index_3, 0, 15);
}

static void unload_track_module(schwung_input_mode_track_config_t *track) {
    if (!track) return;
    if (track->module.api && track->module.api->destroy && track->module.instance) {
        track->module.api->destroy(track->module.instance);
    }
    if (track->module.handle) {
        dlclose(track->module.handle);
    }
    memset(&track->module, 0, sizeof(track->module));
}

static int module_id_safe(const char *module_id) {
    if (!module_id || !module_id[0]) return 0;
    for (const char *p = module_id; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_') {
            continue;
        }
        return 0;
    }
    return 1;
}

static void apply_config_to_module(schwung_input_mode_track_config_t *track);

static int load_track_module(schwung_input_mode_state_t *state, int track, const char *module_id) {
    if (!state || !valid_track(track) || !module_id_safe(module_id)) return 0;
    schwung_input_mode_track_config_t *track_config = &state->tracks[track];
    if (strcmp(track_config->module.module_id, module_id) == 0 && track_config->module.api) {
        return 1;
    }

    unload_track_module(track_config);

    char module_dir[SCHWUNG_INPUT_MODE_MODULE_ROOT_LEN];
    char dsp_path[SCHWUNG_INPUT_MODE_MODULE_ROOT_LEN + SCHWUNG_INPUT_MODE_MODULE_ID_LEN + 16];
    snprintf(module_dir, sizeof(module_dir), "%s/%s", state->modules_root, module_id);
    snprintf(dsp_path, sizeof(dsp_path), "%s/dsp.so", module_dir);

    void *handle = dlopen(dsp_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return 0;

    schwung_input_module_api_v1_t *api = NULL;
    schwung_input_module_api_v2_t *api_v2 = NULL;
    schwung_input_module_init_v2_fn init_v2 =
        (schwung_input_module_init_v2_fn)dlsym(handle, "schwung_input_module_init_v2");
    if (init_v2) {
        api_v2 = init_v2();
        if (!api_v2 ||
            api_v2->api_version != SCHWUNG_INPUT_MODULE_API_VERSION_V2 ||
            api_v2->struct_size < offsetof(schwung_input_module_api_v2_t, struct_size) + sizeof(api_v2->struct_size) ||
            !api_v2->process_midi) {
            dlclose(handle);
            return 0;
        }
        api = (schwung_input_module_api_v1_t *)api_v2;
    } else {
        schwung_input_module_init_v1_fn init_v1 =
            (schwung_input_module_init_v1_fn)dlsym(handle, "schwung_input_module_init_v1");
        if (!init_v1) {
            dlclose(handle);
            return 0;
        }

        api = init_v1();
        if (!api || api->api_version != SCHWUNG_INPUT_MODULE_API_VERSION || !api->process_midi) {
            dlclose(handle);
            return 0;
        }
    }

    track_config->module.handle = handle;
    track_config->module.api = api;
    track_config->module.api_v2 = api_v2;
    track_config->module.instance = api->create ? api->create(module_dir) : NULL;
    snprintf(track_config->module.module_id, sizeof(track_config->module.module_id), "%s", module_id);
    snprintf(track_config->module.module_dir, sizeof(track_config->module.module_dir), "%s", module_dir);
    apply_config_to_module(track_config);
    return 1;
}

static int result_note_count(const schwung_input_mode_result_t *result, uint8_t *notes, int max_notes) {
    if (!result || !notes || max_notes <= 0) return 0;
    int count = 0;
    for (int i = 0; i < result->count && count < max_notes; i++) {
        const uint8_t *pkt = result->packets[i];
        if ((pkt[1] & 0xF0) == 0x90 && pkt[3] > 0) {
            notes[count++] = pkt[2];
        }
    }
    return count;
}

static void module_set_int_param(schwung_input_mode_track_config_t *track,
                                 const char *key,
                                 int value) {
    if (!module_loaded(track) || !track->module.api->set_param) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", value);
    track->module.api->set_param(track->module.instance, key, buf);
}

static void apply_config_to_module(schwung_input_mode_track_config_t *track) {
    if (!module_loaded(track) || !track->module.api->set_param) return;
    module_set_int_param(track, "root", track->config.root);
    module_set_int_param(track, "scale", track->config.scale);
    module_set_int_param(track, "octave", track->config.octave);
    module_set_int_param(track, "root_octave", track->config.root_octave);
    module_set_int_param(track, "index_2", track->config.index_2);
    module_set_int_param(track, "index_3", track->config.index_3);
}

static int append_led_update(schwung_input_mode_track_config_t *track,
                             schwung_input_mode_result_t *result) {
    if (!module_loaded(track) || !track->module.api->update_leds || !result) return 0;
    schwung_input_mode_result_t led_result;
    schwung_input_mode_result_clear(&led_result);
    if (!track->module.api->update_leds(track->module.instance, NULL, &led_result)) return 0;
    for (int i = 0; i < led_result.light_count &&
                    result->light_count < SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT; i++) {
        memcpy(result->light_packets[result->light_count++], led_result.light_packets[i], 4);
    }
    return led_result.light_count > 0 ? 1 : 0;
}

static void append_module_light_packets(schwung_input_mode_result_t *result,
                                        const schwung_input_mode_result_t *module_result) {
    if (!result || !module_result) return;
    for (int i = 0; i < module_result->light_count &&
                    result->light_count < SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT; i++) {
        memcpy(result->light_packets[result->light_count++], module_result->light_packets[i], 4);
    }
}

static void append_module_packets(schwung_input_mode_result_t *result,
                                  const schwung_input_mode_result_t *module_result) {
    if (!result || !module_result) return;
    for (int i = 0; i < module_result->count &&
                    result->count < SCHWUNG_INPUT_MODE_MAX_PACKET_OUT; i++) {
        memcpy(result->packets[result->count++], module_result->packets[i], 4);
    }
    append_module_light_packets(result, module_result);
    for (int i = 0; i < module_result->param_update_count &&
                    result->param_update_count < SCHWUNG_INPUT_MODULE_MAX_PARAM_UPDATES; i++) {
        result->param_updates[result->param_update_count++] = module_result->param_updates[i];
    }
}

static int color_value_at(const int *pad_colors, int idx) {
    if (!pad_colors || idx < 0 || idx >= SCHWUNG_INPUT_MODE_PADS) return 0;
    return pad_colors[idx] > 0 ? pad_colors[idx] : 0;
}

static int color_is_off(int color) {
    return color == 0 || color == 2;
}

static int color_hue(int color) {
    if (color_is_off(color)) return 0;
    int bucket = (color + 2) / 4;
    if (bucket >= 16 && bucket <= 18) return -1;
    return bucket;
}

static int color_is_grey(int color) {
    return color_hue(color) == -1;
}

static int pad_is_held(const uint8_t *held_pads, int idx) {
    return held_pads && idx >= 0 && idx < SCHWUNG_INPUT_MODE_PADS && held_pads[idx] != 0;
}

static int add_unique_color(int *colors, int *count, int max_count, int color) {
    for (int i = 0; i < *count; i++) {
        if (colors[i] == color) return 1;
    }
    if (*count >= max_count) return 0;
    colors[(*count)++] = color;
    return 1;
}

static int drum_bank_matches(const int pad_colors[SCHWUNG_INPUT_MODE_PADS],
                             const uint8_t held_pads[SCHWUNG_INPUT_MODE_PADS],
                             const int *indices,
                             int count) {
    int considered = 0;
    int painted = 0;
    int classes[8] = {0};
    int class_counts[8] = {0};
    int class_count = 0;
    int dominant_count = 0;

    for (int i = 0; i < count; i++) {
        int idx = indices[i];
        if (idx < 0 || idx >= SCHWUNG_INPUT_MODE_PADS) continue;
        if (pad_is_held(held_pads, idx)) continue;
        considered++;
        int color = color_value_at(pad_colors, idx);
        if (color_is_off(color)) continue;
        painted++;
        int hue = color_hue(color);
        int seen = 0;
        for (int j = 0; j < class_count; j++) {
            if (classes[j] == hue) {
                class_counts[j]++;
                if (class_counts[j] > dominant_count) dominant_count = class_counts[j];
                seen = 1;
                break;
            }
        }
        if (!seen && class_count < (int)(sizeof(classes) / sizeof(classes[0]))) {
            classes[class_count] = hue;
            class_counts[class_count] = 1;
            if (dominant_count < 1) dominant_count = 1;
            class_count++;
        }
    }

    return considered >= 12 &&
           painted >= considered - 1 &&
           dominant_count >= considered - 1;
}

schwung_input_led_grid_mode_t schwung_input_mode_detect_led_grid_mode(const int pad_colors[SCHWUNG_INPUT_MODE_PADS],
                                                                      const uint8_t held_pads[SCHWUNG_INPUT_MODE_PADS]) {
    if (!pad_colors) return SCHWUNG_INPUT_LED_GRID_UNKNOWN_NON_NOTE;

    int painted_count = 0;
    int grey_count = 0;
    int row_painted_count[4] = {0};
    int non_grey_hues[16] = {0};
    int non_grey_hue_count = 0;
    int row_hues_same[4] = {1, 1, 1, 1};
    int left_half_considered = 0;
    int left_half_painted = 0;
    int left_half_grey_count = 0;
    int left_half_hues[4] = {0};
    int left_half_hue_count = 0;
    int left_half_color_classes[8] = {0};
    int left_half_color_counts[8] = {0};
    int left_half_color_class_count = 0;
    int left_half_dominant_count = 0;

    for (int row = 0; row < 4; row++) {
        int row_hue = 0;
        int row_has_hue = 0;
        for (int col = 0; col < 8; col++) {
            int idx = row * 8 + col;
            if (pad_is_held(held_pads, idx)) continue;
            if (col < 4) left_half_considered++;

            int color = color_value_at(pad_colors, idx);
            if (color_is_off(color)) continue;

            painted_count++;
            row_painted_count[row]++;
            if (col < 4) left_half_painted++;

            int hue = color_hue(color);
            if (col < 4) {
                int seen = 0;
                for (int i = 0; i < left_half_color_class_count; i++) {
                    if (left_half_color_classes[i] == hue) {
                        left_half_color_counts[i]++;
                        if (left_half_color_counts[i] > left_half_dominant_count) {
                            left_half_dominant_count = left_half_color_counts[i];
                        }
                        seen = 1;
                        break;
                    }
                }
                if (!seen && left_half_color_class_count < (int)(sizeof(left_half_color_classes) / sizeof(left_half_color_classes[0]))) {
                    left_half_color_classes[left_half_color_class_count] = hue;
                    left_half_color_counts[left_half_color_class_count] = 1;
                    if (left_half_dominant_count < 1) left_half_dominant_count = 1;
                    left_half_color_class_count++;
                }
            }

            if (color_is_grey(color)) {
                grey_count++;
                if (col < 4) left_half_grey_count++;
                continue;
            }

            add_unique_color(non_grey_hues, &non_grey_hue_count,
                             (int)(sizeof(non_grey_hues) / sizeof(non_grey_hues[0])),
                             hue);
            if (col < 4) {
                add_unique_color(left_half_hues, &left_half_hue_count,
                                 (int)(sizeof(left_half_hues) / sizeof(left_half_hues[0])),
                                 hue);
            }
            if (!row_has_hue) {
                row_hue = hue;
                row_has_hue = 1;
            } else if (row_hue != hue) {
                row_hues_same[row] = 0;
            }
        }
    }

    if (painted_count < 10) {
        if (painted_count < 4) return SCHWUNG_INPUT_LED_GRID_SET;
        if (grey_count >= 2) return SCHWUNG_INPUT_LED_GRID_SESSION;
        if (painted_count == 4) {
            int rows_with_one = 0;
            for (int row = 0; row < 4; row++) {
                if (row_painted_count[row] == 1) rows_with_one++;
            }
            if (rows_with_one != 4) return SCHWUNG_INPUT_LED_GRID_SET;
        }
        return SCHWUNG_INPUT_LED_GRID_UNKNOWN_NON_NOTE;
    }

    if (non_grey_hue_count > 4) return SCHWUNG_INPUT_LED_GRID_SET;

    static const int physical_left_4x4[] = {
        0, 1, 2, 3,
        8, 9, 10, 11,
        16, 17, 18, 19,
        24, 25, 26, 27
    };
    static const int contiguous_low_16[] = {
        0, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15
    };
    if (drum_bank_matches(pad_colors, held_pads, physical_left_4x4, 16) ||
        drum_bank_matches(pad_colors, held_pads, contiguous_low_16, 16)) {
        return SCHWUNG_INPUT_LED_GRID_NOTE;
    }

    if (left_half_considered >= 12 &&
        left_half_painted == left_half_considered &&
        left_half_grey_count == 0 &&
        left_half_hue_count >= 1 &&
        left_half_hue_count <= 2) {
        return SCHWUNG_INPUT_LED_GRID_NOTE;
    }

    for (int row = 0; row < 4; row++) {
        if (!row_hues_same[row]) return SCHWUNG_INPUT_LED_GRID_NOTE;
    }

    if (non_grey_hue_count <= 1 && grey_count == 0) {
        return SCHWUNG_INPUT_LED_GRID_NOTE;
    }

    return SCHWUNG_INPUT_LED_GRID_SESSION;
}

schwung_input_view_class_t schwung_input_mode_classify_led_grid(const int pad_colors[SCHWUNG_INPUT_MODE_PADS],
                                                                const uint8_t held_pads[SCHWUNG_INPUT_MODE_PADS]) {
    schwung_input_led_grid_mode_t mode =
        schwung_input_mode_detect_led_grid_mode(pad_colors, held_pads);
    return mode == SCHWUNG_INPUT_LED_GRID_NOTE
        ? SCHWUNG_INPUT_VIEW_PLAY
        : SCHWUNG_INPUT_VIEW_NON_PLAY;
}

static int append_packet(schwung_input_mode_result_t *result,
                         uint8_t cin,
                         uint8_t status,
                         uint8_t data1,
                         uint8_t data2) {
    if (!result) return 0;
    if (result->count >= SCHWUNG_INPUT_MODE_MAX_PACKET_OUT) return 0;
    uint8_t *pkt = result->packets[result->count++];
    pkt[0] = cin;
    pkt[1] = status;
    pkt[2] = data1;
    pkt[3] = data2;
    return 1;
}

static int append_note_off(schwung_input_mode_result_t *result,
                           uint8_t channel,
                           uint8_t note) {
    return append_packet(result, (uint8_t)((2 << 4) | 0x08),
                         (uint8_t)(0x80 | (channel & 0x0F)), note, 0);
}

void schwung_input_mode_result_clear(schwung_input_mode_result_t *result) {
    if (!result) return;
    memset(result, 0, sizeof(*result));
}

void schwung_input_mode_init(schwung_input_mode_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    snprintf(state->modules_root, sizeof(state->modules_root), "%s",
             "/data/UserData/schwung/modules/input_modes");
    for (int i = 0; i < SCHWUNG_INPUT_MODE_TRACKS; i++) {
        state->tracks[i].mode = SCHWUNG_INPUT_MODE_NATIVE;
        state->tracks[i].led_mode = SCHWUNG_INPUT_LED_PASS_THROUGH;
        state->tracks[i].config = schwung_input_mode_default_config();
    }
}

void schwung_input_mode_set_modules_root(schwung_input_mode_state_t *state,
                                         const char *modules_root) {
    if (!state || !modules_root || !modules_root[0]) return;
    snprintf(state->modules_root, sizeof(state->modules_root), "%s", modules_root);
}

void schwung_input_mode_merge_led_grid(const int raw_colors[SCHWUNG_INPUT_MODE_PADS],
                                       int native_colors[SCHWUNG_INPUT_MODE_PADS],
                                       uint8_t native_valid[SCHWUNG_INPUT_MODE_PADS],
                                       const int custom_colors[SCHWUNG_INPUT_MODE_PADS],
                                       const uint8_t custom_valid[SCHWUNG_INPUT_MODE_PADS],
                                       int out_colors[SCHWUNG_INPUT_MODE_PADS]) {
    if (!raw_colors || !native_colors || !native_valid || !custom_colors || !custom_valid || !out_colors) return;
    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        int raw = raw_colors[i] >= 0 ? raw_colors[i] : 0;
        if (custom_valid[i] && raw == custom_colors[i]) {
            out_colors[i] = native_valid[i] ? native_colors[i] : 0;
        } else {
            native_colors[i] = raw;
            native_valid[i] = 1;
            out_colors[i] = raw;
        }
    }
}

int schwung_input_mode_panic_track(schwung_input_mode_state_t *state,
                                   int track,
                                   schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(track)) return 0;

    int emitted = 0;
    schwung_input_mode_track_config_t *track_config = &state->tracks[track];
    if (module_has_v2_panic(track_config)) {
        if (track_config->module.api_v2->panic(track_config->module.instance, NULL, result)) {
            emitted += result ? result->count : 0;
        }
        for (int pad = 0; pad < SCHWUNG_INPUT_MODE_PADS; pad++) {
            memset(&state->held[track][pad], 0, sizeof(state->held[track][pad]));
        }
        return emitted || result_has_output(result);
    }

    for (int pad = 0; pad < SCHWUNG_INPUT_MODE_PADS; pad++) {
        schwung_input_mode_held_pad_t *held = &state->held[track][pad];
        if (!held->active) continue;
        if (module_loaded(track_config) && track_config->module.api->process_midi) {
            schwung_input_module_event_t event = {
                .active_track = track,
                .channel = held->channel,
                .cin = 0x08,
                .status = (uint8_t)(0x80 | (held->channel & 0x0F)),
                .data1 = (uint8_t)(68 + pad),
                .data2 = 0
            };
            schwung_input_mode_result_t ignored;
            schwung_input_mode_result_clear(&ignored);
            track_config->module.api->process_midi(
                track_config->module.instance, &event, NULL, &ignored);
            append_module_packets(result, &ignored);
            emitted += ignored.count;
        }
        for (int i = 0; i < held->count && i < (int)sizeof(held->notes); i++) {
            emitted += append_note_off(result, held->channel, held->notes[i]) ? 1 : 0;
        }
        memset(held, 0, sizeof(*held));
    }
    return emitted || result_has_output(result);
}

int schwung_input_mode_set_track_mode(schwung_input_mode_state_t *state,
                                      int track,
                                      schwung_input_mode_t mode,
                                      schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(track)) return 0;

    if (!valid_mode(mode)) {
        mode = SCHWUNG_INPUT_MODE_NATIVE;
    }

    int emitted = 0;
    if (state->tracks[track].mode != mode) {
        emitted = schwung_input_mode_panic_track(state, track, result);
    }
    state->tracks[track].mode = mode;
    return emitted || result_has_output(result);
}

int schwung_input_mode_set_track_module(schwung_input_mode_state_t *state,
                                        int track,
                                        const char *module_id,
                                        schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(track)) return 0;
    if (!module_id || strcmp(module_id, "native") == 0 || !module_id[0]) {
        int emitted = schwung_input_mode_panic_track(state, track, result);
        unload_track_module(&state->tracks[track]);
        memset(state->tracks[track].param_keys, 0, sizeof(state->tracks[track].param_keys));
        memset(state->tracks[track].param_values, 0, sizeof(state->tracks[track].param_values));
        return emitted || result_has_output(result);
    }
    int emitted = 0;
    if (strcmp(state->tracks[track].module.module_id, module_id) != 0) {
        emitted = schwung_input_mode_panic_track(state, track, result);
        memset(state->tracks[track].param_keys, 0, sizeof(state->tracks[track].param_keys));
        memset(state->tracks[track].param_values, 0, sizeof(state->tracks[track].param_values));
    }
    load_track_module(state, track, module_id);
    if (module_active(&state->tracks[track])) {
        append_led_update(&state->tracks[track], result);
    }
    return emitted || result_has_output(result);
}

int schwung_input_mode_set_track_param(schwung_input_mode_state_t *state,
                                       int track,
                                       const char *key,
                                       const char *value,
                                       schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(track) || !key || !value) return 0;
    schwung_input_mode_track_config_t *track_config = &state->tracks[track];
    if (!track_config->module.api || !track_config->module.api->set_param) return 0;

    int slot = -1;
    for (int i = 0; i < SCHWUNG_INPUT_MODE_PARAM_COUNT; i++) {
        if (track_config->param_keys[i][0] == '\0' && slot < 0) slot = i;
        if (strncmp(track_config->param_keys[i], key, SCHWUNG_INPUT_MODE_PARAM_KEY_LEN) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return 0;
    if (strncmp(track_config->param_values[slot], value, SCHWUNG_INPUT_MODE_PARAM_VALUE_LEN) == 0 &&
        strncmp(track_config->param_keys[slot], key, SCHWUNG_INPUT_MODE_PARAM_KEY_LEN) == 0) {
        return 0;
    }

    int emitted = schwung_input_mode_panic_track(state, track, result);
    snprintf(track_config->param_keys[slot], sizeof(track_config->param_keys[slot]), "%s", key);
    snprintf(track_config->param_values[slot], sizeof(track_config->param_values[slot]), "%s", value);
    track_config->module.api->set_param(track_config->module.instance, key, value);
    if (module_active(track_config)) {
        append_led_update(track_config, result);
    }
    return emitted || result_has_output(result);
}

int schwung_input_mode_update_leds(schwung_input_mode_state_t *state,
                                   int track,
                                   schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(track)) return 0;
    if (!module_active(&state->tracks[track])) return 0;
    return append_led_update(&state->tracks[track], result);
}

int schwung_input_mode_set_track_config(schwung_input_mode_state_t *state,
                                        int track,
                                        const schwung_input_mode_config_t *config,
                                        schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(track) || !config) return 0;

    schwung_input_mode_config_t next = *config;
    normalize_config(&next);
    int emitted = 0;
    if (memcmp(&state->tracks[track].config, &next, sizeof(next)) != 0) {
        emitted = schwung_input_mode_panic_track(state, track, result);
    }
    state->tracks[track].config = next;
    apply_config_to_module(&state->tracks[track]);
    if (module_active(&state->tracks[track])) {
        append_led_update(&state->tracks[track], result);
    }
    return emitted || result_has_output(result);
}

int schwung_input_mode_handle_button(schwung_input_mode_state_t *state,
                                     int active_track,
                                     uint8_t cin,
                                     uint8_t status,
                                     uint8_t data1,
                                     uint8_t data2,
                                     schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(active_track)) return 0;
    schwung_input_mode_track_config_t *track = &state->tracks[active_track];
    if (!module_active(track)) return 0;
    if (cin != 0x0B || (status & 0xF0) != 0xB0) return 0;
    if (!track->module.api->process_button) return 0;

    schwung_input_module_event_t event = {
        .active_track = active_track,
        .channel = track_channel(active_track),
        .cin = cin,
        .status = status,
        .data1 = data1,
        .data2 = data2
    };
    schwung_input_mode_result_t module_result;
    schwung_input_mode_result_clear(&module_result);
    if (!track->module.api->process_button(track->module.instance, &event, NULL, &module_result)) {
        return 0;
    }
    schwung_input_mode_panic_track(state, active_track, result);
    for (int i = 0; i < module_result.count && result && result->count < SCHWUNG_INPUT_MODE_MAX_PACKET_OUT; i++) {
        memcpy(result->packets[result->count++], module_result.packets[i], 4);
    }
    for (int i = 0; i < module_result.light_count && result && result->light_count < SCHWUNG_INPUT_MODE_MAX_PACKET_OUT; i++) {
        memcpy(result->light_packets[result->light_count++], module_result.light_packets[i], 4);
    }
    for (int i = 0; i < module_result.param_update_count &&
                    result &&
                    result->param_update_count < SCHWUNG_INPUT_MODULE_MAX_PARAM_UPDATES; i++) {
        result->param_updates[result->param_update_count++] = module_result.param_updates[i];
        schwung_input_mode_set_track_param(state,
                                           active_track,
                                           module_result.param_updates[i].key,
                                           module_result.param_updates[i].value,
                                           NULL);
    }
    append_led_update(track, result);
    return 1;
}

int schwung_input_mode_handle_midi(schwung_input_mode_state_t *state,
                                   int active_track,
                                   uint8_t cin,
                                   uint8_t status,
                                   uint8_t data1,
                                   uint8_t data2,
                                   schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(active_track)) return 0;
    schwung_input_mode_track_config_t *track = &state->tracks[active_track];
    if (!module_active(track)) return 0;

    uint8_t type = status & 0xF0;
    int pidx = pad_index(data1);
    if (pidx < 0) return 0;
    if (!((cin == 0x09 || cin == 0x08) && (type == 0x90 || type == 0x80))) {
        return 0;
    }

    uint8_t channel = track_channel(active_track);
    schwung_input_mode_held_pad_t *held = &state->held[active_track][pidx];
    int is_note_on = (type == 0x90 && data2 > 0);

    if (is_note_on) {
        if (held->active) {
            if (!track->module.api_v2) {
                for (int i = 0; i < held->count && i < (int)sizeof(held->notes); i++) {
                    append_note_off(result, held->channel, held->notes[i]);
                }
            }
            memset(held, 0, sizeof(*held));
        }

        schwung_input_module_event_t event = {
            .active_track = active_track,
            .channel = channel,
            .cin = cin,
            .status = status,
            .data1 = data1,
            .data2 = data2
        };
        int handled = track->module.api->process_midi(
            track->module.instance, &event, NULL, result);
        if (!handled) return 0;

        uint8_t mapped_notes[8] = {0};
        int note_count = result_note_count(result, mapped_notes, (int)sizeof(mapped_notes));
        if (note_count <= 0 && !track->module.api_v2) return 0;
        held->active = 1;
        held->count = (uint8_t)note_count;
        if (note_count > 0) {
            memcpy(held->notes, mapped_notes, (size_t)note_count);
        }
        held->channel = channel;
        append_led_update(track, result);
        return 1;
    }

    if (held->active && !track->module.api_v2) {
        for (int i = 0; i < held->count && i < (int)sizeof(held->notes); i++) {
            append_note_off(result, held->channel, held->notes[i]);
        }
    }
    if (held->active) memset(held, 0, sizeof(*held));
    schwung_input_module_event_t event = {
        .active_track = active_track,
        .channel = channel,
        .cin = cin,
        .status = status,
        .data1 = data1,
        .data2 = data2
    };
    schwung_input_mode_result_t module_result;
    schwung_input_mode_result_clear(&module_result);
    if (track->module.api->process_midi(
            track->module.instance, &event, NULL, &module_result)) {
        append_module_packets(result, &module_result);
    }
    append_led_update(track, result);
    return 1;
}

int schwung_input_mode_tick(schwung_input_mode_state_t *state,
                            int active_track,
                            int frames,
                            int sample_rate,
                            schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(active_track)) return 0;
    schwung_input_mode_track_config_t *track = &state->tracks[active_track];
    if (!module_active(track) || !module_has_v2_tick(track)) return 0;
    return track->module.api_v2->tick(track->module.instance,
                                      frames,
                                      sample_rate,
                                      NULL,
                                      result);
}
