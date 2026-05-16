#include "input_mode.h"

#include <string.h>

/* ========================================================================
 * Scale definitions
 * ======================================================================== */

typedef struct {
    const char *name;
    uint8_t intervals[12];
    uint8_t count;
} scale_def_t;

static const scale_def_t scales[] = {
    {"Chromatic",         {0,1,2,3,4,5,6,7,8,9,10,11}, 12},
    {"Major",             {0,2,4,5,7,9,11},             7},
    {"Minor",             {0,2,3,5,7,8,10},             7},
    {"Dorian",            {0,2,3,5,7,9,10},             7},
    {"Phrygian",          {0,1,3,5,7,8,10},             7},
    {"Lydian",            {0,2,4,6,7,9,11},             7},
    {"Mixolydian",        {0,2,4,5,7,9,10},             7},
    {"Locrian",           {0,1,3,5,6,8,10},             7},
    {"Harmonic Minor",    {0,2,3,5,7,8,11},             7},
    {"Melodic Minor",     {0,2,3,5,7,9,11},             7},
    {"Major Pentatonic",  {0,2,4,7,9},                  5},
    {"Minor Pentatonic",  {0,3,5,7,10},                 5},
    {"Blues",             {0,3,5,6,7,10},               6},
};

#define SCALE_COUNT ((int)(sizeof(scales) / sizeof(scales[0])))

int schwung_input_mode_scale_count(void) { return SCALE_COUNT; }

const char *schwung_input_mode_scale_name(int index) {
    if (index < 0 || index >= SCALE_COUNT) return "Chromatic";
    return scales[index].name;
}

static const scale_def_t *get_scale(int index) {
    if (index < 0 || index >= SCALE_COUNT) return &scales[0];
    return &scales[index];
}

int schwung_input_mode_note_in_scale(int note, int root, int scale_index) {
    const scale_def_t *scale = get_scale(scale_index);
    int semitone = ((note - root) % 12 + 12) % 12;
    for (int i = 0; i < (int)scale->count; i++) {
        if (scale->intervals[i] == semitone) return 1;
    }
    return 0;
}

/* Returns the scale degree (0-based) for a note within the scale,
 * or -1 if the note is not in the scale. */
static int note_scale_degree(int note, int root, int scale_index) {
    const scale_def_t *scale = get_scale(scale_index);
    int semitone = ((note - root) % 12 + 12) % 12;
    for (int i = 0; i < (int)scale->count; i++) {
        if (scale->intervals[i] == semitone) return i;
    }
    return -1;
}

/* Returns the MIDI note for a given scale degree at a given octave.
 * degree: 0 = tonic, 1 = supertonic, etc.
 * octave: 0 = same octave as root, 1 = one octave up */
static int scale_note_for_degree(int root, int scale_index, int degree, int octave_offset) {
    const scale_def_t *scale = get_scale(scale_index);
    int oct = degree / scale->count;
    int deg = degree % scale->count;
    /* Handle negative degrees */
    if (deg < 0) { deg += scale->count; oct--; }
    return root + scale->intervals[deg] + (oct + octave_offset) * 12;
}

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

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
    return mode >= SCHWUNG_INPUT_MODE_NATIVE && mode <= SCHWUNG_INPUT_MODE_CHORD_PADS;
}

/* Extract params from the int16_t array passed from shim */
static schwung_input_mode_params_t unpack_params(const int16_t *params, int8_t octave) {
    schwung_input_mode_params_t p;
    memset(&p, 0, sizeof(p));
    if (params) {
        p.root    = params[0];
        p.scale   = params[1];
        p.index_2 = params[2];
        p.index_3 = params[3];
    }
    p.octave = octave;
    if (p.scale < 0 || p.scale >= SCALE_COUNT) p.scale = 0;
    if (p.index_2 <= 0) p.index_2 = 2;
    if (p.index_3 <= 0) p.index_3 = 4;
    if (p.index_2 > 7) p.index_2 = 7;
    if (p.index_3 > 7) p.index_3 = 7;
    return p;
}

static int clamp_root(int root) {
    if (root < 36) return 36;
    if (root > 84) return 84;
    return root;
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

/* ========================================================================
 * LED grid classification (unchanged from original)
 * ======================================================================== */

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

/* ========================================================================
 * Note building (param-aware)
 * ======================================================================== */

static int build_notes_for_pad(schwung_input_mode_t mode,
                               int pidx,
                               uint8_t *notes,
                               int max_notes,
                               const schwung_input_mode_params_t *p) {
    if (!notes || max_notes <= 0 || pidx < 0 || pidx >= SCHWUNG_INPUT_MODE_PADS) return 0;

    if (mode == SCHWUNG_INPUT_MODE_DRUM32) {
        int drum_root_offset = (int)p->root; /* param[0] = root_octave, defaults to 0 */
        if (drum_root_offset < -2) drum_root_offset = -2;
        if (drum_root_offset > 3) drum_root_offset = 3;
        int perf_octave = (p->octave > 4) ? 4 : (p->octave < -4) ? -4 : p->octave;
        int note = 36 + pidx + (drum_root_offset + perf_octave) * 12;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        notes[0] = (uint8_t)note;
        return 1;
    }

    if (mode == SCHWUNG_INPUT_MODE_TRUE_CHROMATIC) {
        int root = clamp_root((int)p->root);
        if (root <= 0) root = 48;
        root += (int)p->octave * 12;
        if (p->scale > 0) {
            /* Scale-filtered chromatic: only emit in-scale notes.
             * Each pad maps to the next in-scale note from the root. */
            const scale_def_t *scale = get_scale((int)p->scale);
            int oct = pidx / scale->count;
            int deg = pidx % scale->count;
            notes[0] = (uint8_t)(root + scale->intervals[deg] + oct * 12);
        } else {
            /* Pure chromatic: linear from root */
            notes[0] = (uint8_t)(root + pidx);
        }
        if (notes[0] > 127) notes[0] = 127;
        return 1;
    }

    if (mode == SCHWUNG_INPUT_MODE_CHORD_PADS && max_notes >= 3) {
        int root = clamp_root((int)p->root);
        if (root <= 0) root = 60;
        root += (int)p->octave * 12;
        int degree = pidx % SCALE_COUNT;
        int oct = pidx / SCALE_COUNT;

        int note1 = scale_note_for_degree(root, (int)p->scale, degree, oct);
        int note2 = scale_note_for_degree(root, (int)p->scale, degree + (int)p->index_2 - 1, oct);
        int note3 = scale_note_for_degree(root, (int)p->scale, degree + (int)p->index_3 - 1, oct);

        /* Clamp to valid MIDI range */
        if (note1 > 127) note1 = 127;
        if (note2 > 127) note2 = 127;
        if (note3 > 127) note3 = 127;
        if (note1 < 0) note1 = 0;
        if (note2 < 0) note2 = 0;
        if (note3 < 0) note3 = 0;

        notes[0] = (uint8_t)note1;
        notes[1] = (uint8_t)note2;
        notes[2] = (uint8_t)note3;
        return 3;
    }

    return 0;
}

/* ========================================================================
 * Packet helpers
 * ======================================================================== */

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

/* ========================================================================
 * Public API
 * ======================================================================== */

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
                                   const int16_t *params,
                                   int8_t octave,
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

    schwung_input_mode_params_t p = unpack_params(params, octave);

    if (is_note_on) {
        if (held->active) {
            for (int i = 0; i < held->count && i < (int)sizeof(held->notes); i++) {
                append_note_off(result, held->channel, held->notes[i]);
            }
            memset(held, 0, sizeof(*held));
        }

        uint8_t mapped_notes[8] = {0};
        int mode = state->tracks[active_track].mode;
        int note_count = build_notes_for_pad((schwung_input_mode_t)mode,
                                              pidx,
                                              mapped_notes,
                                              (int)sizeof(mapped_notes),
                                              &p);
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

/* ========================================================================
 * Pad LED rendering
 * ======================================================================== */

/* Compute hue-shifted variant of a color for root note highlighting.
 * Uses the same hue bucket math as color_hue() for consistency. */
static uint8_t hue_shift_color(uint8_t color, int offset) {
    if (color <= 2) return 0; /* off / black */
    int bucket = (int)(((int)color + 2) / 4);
    int new_bucket = bucket + offset;
    if (new_bucket < 1) new_bucket = 1;
    if (new_bucket > 30) new_bucket = 30;
    /* Map back: bucket * 4 gives the approx center of that hue range */
    int new_color = new_bucket * 4 - 2;
    if (new_color < 3) new_color = 3;
    if (new_color > 127) new_color = 127;
    return (uint8_t)new_color;
}

/* Find the dominant (most common non-off) color in an array */
static uint8_t dominant_color(const uint8_t *colors, int count) {
    int hist[128] = {0};
    int best = 0, best_count = 0;
    for (int i = 0; i < count; i++) {
        if (colors[i] <= 2) continue;
        int c = colors[i];
        hist[c]++;
        if (hist[c] > best_count) {
            best_count = hist[c];
            best = c;
        }
    }
    return (uint8_t)(best > 0 ? best : 0);
}

int schwung_input_mode_render_leds(schwung_input_mode_t mode,
                                   const int16_t *params,
                                   int8_t octave,
                                   const uint8_t native_colors[SCHWUNG_INPUT_MODE_PADS],
                                   uint8_t pad_colors_out[SCHWUNG_INPUT_MODE_PADS]) {
    if (!pad_colors_out) return 0;
    memset(pad_colors_out, 0, SCHWUNG_INPUT_MODE_PADS);

    if (mode == SCHWUNG_INPUT_MODE_NATIVE) return 0;

    schwung_input_mode_params_t p = unpack_params(params, octave);
    uint8_t track_color = dominant_color(native_colors, SCHWUNG_INPUT_MODE_PADS);
    if (track_color == 0) track_color = 85; /* fallback: warm amber */

    int lit = 0;

    if (mode == SCHWUNG_INPUT_MODE_DRUM32) {
        /* All pads lit with track color */
        for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
            pad_colors_out[i] = track_color;
            lit++;
        }
    } else if (mode == SCHWUNG_INPUT_MODE_TRUE_CHROMATIC) {
        int root = clamp_root((int)p.root);
        if (root <= 0) root = 48;
        root += p.octave * 12;
        for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
            int note;
            if (p.scale > 0) {
                const scale_def_t *scale = get_scale((int)p.scale);
                int oct = i / scale->count;
                int deg = i % scale->count;
                note = root + scale->intervals[deg] + oct * 12;
            } else {
                note = root + i;
            }

            if (!schwung_input_mode_note_in_scale(note, root, (int)p.scale)) {
                pad_colors_out[i] = 0; /* off: non-scale */
                continue;
            }
            int degree = note_scale_degree(note, root, (int)p.scale);
            /* Root notes: hue-shifted variant; others: track color */
            if (degree == 0) {
                pad_colors_out[i] = hue_shift_color(track_color, -2);
            } else {
                pad_colors_out[i] = track_color;
            }
            lit++;
        }
    } else if (mode == SCHWUNG_INPUT_MODE_CHORD_PADS) {
        for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
            pad_colors_out[i] = track_color;
            lit++;
        }
    }

    return lit;
}
