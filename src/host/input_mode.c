#include "input_mode.h"

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

static int valid_mode(schwung_input_mode_t mode) {
    return mode == SCHWUNG_INPUT_MODE_NATIVE ||
           mode == SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC ||
           mode == SCHWUNG_INPUT_MODE_DRUM32 ||
           mode == SCHWUNG_INPUT_MODE_CHORD_PADS;
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

            if (color_is_grey(color)) {
                grey_count++;
                if (col < 4) left_half_grey_count++;
                continue;
            }

            int hue = color_hue(color);
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

static int build_notes_for_pad(schwung_input_mode_t mode,
                               int pidx,
                               uint8_t *notes,
                               int max_notes) {
    if (!notes || max_notes <= 0 || pidx < 0 || pidx >= SCHWUNG_INPUT_MODE_PADS) return 0;

    if (mode == SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC) {
        notes[0] = (uint8_t)(48 + pidx);
        return 1;
    }

    if (mode == SCHWUNG_INPUT_MODE_DRUM32) {
        notes[0] = (uint8_t)(36 + pidx);
        return 1;
    }

    if (mode == SCHWUNG_INPUT_MODE_CHORD_PADS && max_notes >= 3) {
        static const uint8_t roots[7] = {60, 62, 64, 65, 67, 69, 71};
        static const uint8_t thirds[7] = {4, 3, 3, 4, 4, 3, 3};
        static const uint8_t fifths[7] = {7, 7, 7, 7, 7, 7, 6};
        int degree = pidx % 7;
        int octave = pidx / 7;
        uint8_t root = (uint8_t)(roots[degree] + octave * 12);
        notes[0] = root;
        notes[1] = (uint8_t)(root + thirds[degree]);
        notes[2] = (uint8_t)(root + fifths[degree]);
        return 3;
    }

    return 0;
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

static int append_note_on(schwung_input_mode_result_t *result,
                          uint8_t channel,
                          uint8_t note,
                          uint8_t velocity) {
    return append_packet(result, (uint8_t)((2 << 4) | 0x09),
                         (uint8_t)(0x90 | (channel & 0x0F)), note, velocity);
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
    for (int i = 0; i < SCHWUNG_INPUT_MODE_TRACKS; i++) {
        state->tracks[i].mode = SCHWUNG_INPUT_MODE_NATIVE;
        state->tracks[i].led_mode = SCHWUNG_INPUT_LED_PASS_THROUGH;
    }
}

int schwung_input_mode_panic_track(schwung_input_mode_state_t *state,
                                   int track,
                                   schwung_input_mode_result_t *result) {
    if (result) schwung_input_mode_result_clear(result);
    if (!state || !valid_track(track)) return 0;

    int emitted = 0;
    for (int pad = 0; pad < SCHWUNG_INPUT_MODE_PADS; pad++) {
        schwung_input_mode_held_pad_t *held = &state->held[track][pad];
        if (!held->active) continue;
        for (int i = 0; i < held->count && i < (int)sizeof(held->notes); i++) {
            emitted += append_note_off(result, held->channel, held->notes[i]) ? 1 : 0;
        }
        memset(held, 0, sizeof(*held));
    }
    return emitted;
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
    return emitted;
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
    if (state->tracks[active_track].mode == SCHWUNG_INPUT_MODE_NATIVE) return 0;

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
            for (int i = 0; i < held->count && i < (int)sizeof(held->notes); i++) {
                append_note_off(result, held->channel, held->notes[i]);
            }
            memset(held, 0, sizeof(*held));
        }

        uint8_t mapped_notes[8] = {0};
        int note_count = build_notes_for_pad(state->tracks[active_track].mode,
                                             pidx,
                                             mapped_notes,
                                             (int)sizeof(mapped_notes));
        if (note_count <= 0) return 0;

        for (int i = 0; i < note_count; i++) {
            append_note_on(result, channel, mapped_notes[i], data2);
        }
        held->active = 1;
        held->count = (uint8_t)note_count;
        memcpy(held->notes, mapped_notes, (size_t)note_count);
        held->channel = channel;
        return 1;
    }

    if (held->active) {
        for (int i = 0; i < held->count && i < (int)sizeof(held->notes); i++) {
            append_note_off(result, held->channel, held->notes[i]);
        }
        memset(held, 0, sizeof(*held));
    }
    return 1;
}
