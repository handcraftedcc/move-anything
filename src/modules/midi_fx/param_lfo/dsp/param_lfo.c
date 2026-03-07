/*
 * Param LFO MIDI FX
 *
 * Publishes control-rate modulation values into chain host's runtime
 * modulation bus without overwriting target base values.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include "host/midi_fx_api_v1.h"
#include "host/plugin_api_v1.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PARAM_LFO_COUNT 3

typedef enum {
    WAVE_SINE = 0,
    WAVE_TRIANGLE,
    WAVE_SQUARE,
    WAVE_SAW_UP,
    WAVE_RANDOM,
    WAVE_DRUNK
} lfo_waveform_t;

typedef enum {
    RATE_MODE_FREE = 0,
    RATE_MODE_SYNC
} lfo_rate_mode_t;

typedef struct {
    lfo_waveform_t waveform;
    lfo_rate_mode_t rate_mode;
    float phase;
    float phase_offset;
    float rate_hz;
    int sync_division;
    float depth;
    float offset;
    int bipolar;
    int enabled;
    int retrigger;
    char target_component[16];
    char target_param[32];
    char source_id[32];
    int modulation_active;
    float random_hold_value;
    float drunk_start_value;
    float drunk_target_value;
} param_lfo_lane_t;

typedef struct {
    param_lfo_lane_t lanes[PARAM_LFO_COUNT];
    uint8_t held_notes[128];
    int held_count;
    int transport_running;
} param_lfo_instance_t;

static const host_api_v1_t *g_host = NULL;

/* === PARAM_LFO_SHARED_BEGIN === */

#define SYNC_DIV_COUNT 10
static const char *k_sync_division_names[SYNC_DIV_COUNT] = {
    "8 bars", "4 bars", "2 bars", "1/1", "1/2",
    "1/4", "1/8", "1/16", "1/32", "1/64"
};
static const float k_sync_division_clocks[SYNC_DIV_COUNT] = {
    768.0f, 384.0f, 192.0f, 96.0f, 48.0f,
    24.0f, 12.0f, 6.0f, 3.0f, 1.5f
};

static float clampf(float value, float min_val, float max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

static float wrap_phase(float phase) {
    phase -= floorf(phase);
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

static float rand_bipolar(void) {
    float unit = (float)rand() / (float)RAND_MAX;
    return (unit * 2.0f) - 1.0f;
}

static int appendf(char *buf, int buf_len, int *offset, const char *fmt, ...) {
    va_list ap;
    int n;

    if (!buf || !offset || *offset < 0 || *offset >= buf_len) return -1;

    va_start(ap, fmt);
    n = vsnprintf(buf + *offset, buf_len - *offset, fmt, ap);
    va_end(ap);

    if (n < 0 || n >= (buf_len - *offset)) return -1;
    *offset += n;
    return 0;
}

static int json_get_string(const char *json, const char *key, char *out, int out_len) {
    if (!json || !key || !out || out_len < 1) return 0;

    char search[96];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return 0;

    const char *colon = strchr(pos, ':');
    if (!colon) return 0;
    while (*colon && (*colon == ':' || *colon == ' ' || *colon == '\t')) colon++;
    if (*colon != '"') return 0;
    colon++;

    const char *end = strchr(colon, '"');
    if (!end) return 0;

    int len = (int)(end - colon);
    if (len >= out_len) len = out_len - 1;
    strncpy(out, colon, len);
    out[len] = '\0';
    return 1;
}

static int json_get_float(const char *json, const char *key, float *out) {
    if (!json || !key || !out) return 0;

    char search[96];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return 0;

    const char *colon = strchr(pos, ':');
    if (!colon) return 0;
    colon++;
    while (*colon && (*colon == ' ' || *colon == '\t')) colon++;

    char *endptr = NULL;
    float parsed = strtof(colon, &endptr);
    if (endptr == colon) return 0;

    *out = parsed;
    return 1;
}

static int parse_toggle(const char *val, int fallback) {
    if (!val || !val[0]) return fallback;

    if (strcmp(val, "on") == 0 || strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
        return 1;
    }
    if (strcmp(val, "off") == 0 || strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
        return 0;
    }
    return fallback;
}

static lfo_rate_mode_t parse_rate_mode(const char *val, lfo_rate_mode_t fallback) {
    if (!val || !val[0]) return fallback;

    if (strcmp(val, "free") == 0 || strcmp(val, "hz") == 0 || strcmp(val, "0") == 0) {
        return RATE_MODE_FREE;
    }
    if (strcmp(val, "sync") == 0 || strcmp(val, "clock") == 0 || strcmp(val, "1") == 0) {
        return RATE_MODE_SYNC;
    }
    return fallback;
}

static const char *rate_mode_to_string(lfo_rate_mode_t mode) {
    return (mode == RATE_MODE_SYNC) ? "sync" : "free";
}

static int parse_sync_division(const char *val, int fallback) {
    if (!val || !val[0]) return fallback;

    if (strcmp(val, "1/64s") == 0) return 9;

    for (int i = 0; i < SYNC_DIV_COUNT; i++) {
        if (strcmp(val, k_sync_division_names[i]) == 0) return i;
    }

    char *endptr = NULL;
    long parsed = strtol(val, &endptr, 10);
    if (endptr != val && *endptr == '\0' && parsed >= 0 && parsed < SYNC_DIV_COUNT) {
        return (int)parsed;
    }

    return fallback;
}

static int clamp_sync_division(int idx) {
    if (idx < 0) return 5; /* 1/4 */
    if (idx >= SYNC_DIV_COUNT) return SYNC_DIV_COUNT - 1;
    return idx;
}

static float sync_division_clocks(int idx) {
    return k_sync_division_clocks[clamp_sync_division(idx)];
}

static const char *sync_division_to_string(int idx) {
    return k_sync_division_names[clamp_sync_division(idx)];
}

static lfo_waveform_t parse_waveform(const char *val, lfo_waveform_t fallback) {
    if (!val || !val[0]) return fallback;

    if (strcmp(val, "sine") == 0 || strcmp(val, "0") == 0) return WAVE_SINE;
    if (strcmp(val, "triangle") == 0 || strcmp(val, "1") == 0) return WAVE_TRIANGLE;
    if (strcmp(val, "square") == 0 || strcmp(val, "2") == 0) return WAVE_SQUARE;
    if (strcmp(val, "saw_up") == 0 || strcmp(val, "3") == 0 || strcmp(val, "saw") == 0) return WAVE_SAW_UP;
    if (strcmp(val, "random") == 0 || strcmp(val, "4") == 0 || strcmp(val, "rand") == 0) return WAVE_RANDOM;
    if (strcmp(val, "drunk") == 0 || strcmp(val, "5") == 0) return WAVE_DRUNK;

    return fallback;
}

static const char *waveform_to_string(lfo_waveform_t waveform) {
    switch (waveform) {
        case WAVE_TRIANGLE: return "triangle";
        case WAVE_SQUARE: return "square";
        case WAVE_SAW_UP: return "saw_up";
        case WAVE_RANDOM: return "random";
        case WAVE_DRUNK: return "drunk";
        case WAVE_SINE:
        default:
            return "sine";
    }
}

static int parse_bipolar(const char *val, int fallback) {
    if (!val || !val[0]) return fallback;

    if (strcmp(val, "bipolar") == 0 || strcmp(val, "0") == 0) return 1;
    if (strcmp(val, "unipolar") == 0 || strcmp(val, "1") == 0) return 0;

    return fallback;
}

static const char *bipolar_to_string(int bipolar) {
    return bipolar ? "bipolar" : "unipolar";
}

static void reset_phase(param_lfo_lane_t *lane) {
    if (!lane) return;
    lane->phase = 0.0f;
}

static float waveform_rate_multiplier(const param_lfo_lane_t *lane) {
    if (!lane) return 1.0f;

    if (lane->waveform == WAVE_DRUNK) {
        /* Keep low rates familiar, but make full-rate drunk movement much faster. */
        const float norm = clampf(lane->rate_hz / 20.0f, 0.0f, 1.0f);
        return 1.0f + (3.0f * norm);
    }

    return 1.0f;
}

static float compute_lfo_sample(const param_lfo_lane_t *lane) {
    if (!lane) return 0.0f;

    const float phase = wrap_phase(lane->phase + lane->phase_offset);
    switch (lane->waveform) {
        case WAVE_TRIANGLE:
            return 1.0f - 4.0f * fabsf(phase - 0.5f);
        case WAVE_SQUARE:
            return (phase < 0.5f) ? 1.0f : -1.0f;
        case WAVE_SAW_UP:
            return (2.0f * phase) - 1.0f;
        case WAVE_RANDOM:
            return lane->random_hold_value;
        case WAVE_DRUNK:
            return lane->drunk_start_value + (lane->drunk_target_value - lane->drunk_start_value) * phase;
        case WAVE_SINE:
        default:
            return sinf(2.0f * (float)M_PI * phase);
    }
}

static void advance_wave_cycle_state(param_lfo_lane_t *lane) {
    if (!lane) return;

    if (lane->waveform == WAVE_RANDOM) {
        lane->random_hold_value = rand_bipolar();
    } else if (lane->waveform == WAVE_DRUNK) {
        lane->drunk_start_value = lane->drunk_target_value;
        lane->drunk_target_value = clampf(lane->drunk_target_value + rand_bipolar() * 0.5f, -1.0f, 1.0f);
    }
}

static void clear_modulation(param_lfo_lane_t *lane) {
    if (!lane) return;

    if (g_host && g_host->mod_clear_source) {
        g_host->mod_clear_source(g_host->mod_host_ctx, lane->source_id);
    }
    lane->modulation_active = 0;
}

static void init_lane(param_lfo_lane_t *lane, void *instance_ptr, int lane_index) {
    if (!lane) return;

    memset(lane, 0, sizeof(*lane));
    lane->waveform = WAVE_SINE;
    lane->rate_mode = RATE_MODE_FREE;
    lane->phase = 0.0f;
    lane->phase_offset = 0.0f;
    lane->rate_hz = 1.0f;
    lane->sync_division = 5; /* 1/4 */
    lane->depth = 0.25f;
    lane->offset = 0.0f;
    lane->bipolar = 1;
    lane->enabled = 0;
    lane->retrigger = 0;
    lane->random_hold_value = rand_bipolar();
    lane->drunk_start_value = 0.0f;
    lane->drunk_target_value = rand_bipolar();
    lane->target_component[0] = '\0';
    lane->target_param[0] = '\0';
    snprintf(lane->source_id, sizeof(lane->source_id), "param_lfo_%p_%d", instance_ptr, lane_index + 1);
}

static void clear_held_notes(param_lfo_instance_t *inst) {
    if (!inst) return;
    memset(inst->held_notes, 0, sizeof(inst->held_notes));
    inst->held_count = 0;
}

static void reset_all_phases(param_lfo_instance_t *inst) {
    if (!inst) return;
    for (int i = 0; i < PARAM_LFO_COUNT; i++) {
        reset_phase(&inst->lanes[i]);
    }
}

static void handle_note_on(param_lfo_instance_t *inst, uint8_t note) {
    if (!inst || note >= 128) return;
    if (inst->held_notes[note]) return;

    /* Retrigger only on fresh gate: no notes were previously held. */
    if (inst->held_count == 0) {
        for (int i = 0; i < PARAM_LFO_COUNT; i++) {
            if (inst->lanes[i].retrigger) {
                reset_phase(&inst->lanes[i]);
            }
        }
    }

    inst->held_notes[note] = 1;
    inst->held_count++;
}

static void handle_note_off(param_lfo_instance_t *inst, uint8_t note) {
    if (!inst || note >= 128) return;
    if (!inst->held_notes[note]) return;

    inst->held_notes[note] = 0;
    inst->held_count--;
    if (inst->held_count < 0) inst->held_count = 0;
}

static int target_component_valid(const char *component) {
    if (!component || !component[0]) return 0;
    return strcmp(component, "synth") == 0 ||
           strcmp(component, "fx1") == 0 ||
           strcmp(component, "fx2") == 0 ||
           strcmp(component, "midi_fx1") == 0 ||
           strcmp(component, "midi_fx2") == 0;
}

static int parse_lfo_key(const char *key, int *lane_idx, const char **subkey) {
    if (!key || !lane_idx || !subkey) return 0;
    if (strncmp(key, "lfo", 3) != 0) return 0;
    if (key[3] < '1' || key[3] > ('0' + PARAM_LFO_COUNT)) return 0;
    if (key[4] != '_') return 0;

    *lane_idx = key[3] - '1';
    *subkey = key + 5;
    return 1;
}

static void advance_sync_clock(param_lfo_instance_t *inst) {
    if (!inst) return;

    for (int i = 0; i < PARAM_LFO_COUNT; i++) {
        param_lfo_lane_t *lane = &inst->lanes[i];
        if (lane->rate_mode != RATE_MODE_SYNC) continue;

        const float clocks_per_cycle = sync_division_clocks(lane->sync_division);
        if (clocks_per_cycle <= 0.0f) continue;

        const float phase_inc = 1.0f / (float)clocks_per_cycle;
        const float new_phase = lane->phase + phase_inc;
        const int wraps = (int)floorf(new_phase);
        lane->phase = wrap_phase(new_phase);
        for (int w = 0; w < wraps; w++) {
            advance_wave_cycle_state(lane);
        }
    }
}

static void set_lane_param(param_lfo_lane_t *lane, const char *subkey, const char *val) {
    if (!lane || !subkey || !val) return;

    if (strcmp(subkey, "waveform") == 0) {
        lane->waveform = parse_waveform(val, lane->waveform);
    }
    else if (strcmp(subkey, "rate_mode") == 0) {
        lane->rate_mode = parse_rate_mode(val, lane->rate_mode);
    }
    else if (strcmp(subkey, "rate_hz") == 0) {
        const int sync_div = parse_sync_division(val, -1);
        if (sync_div >= 0) {
            lane->sync_division = sync_div;
        } else {
            lane->rate_hz = clampf((float)atof(val), 0.01f, 20.0f);
        }
    }
    else if (strcmp(subkey, "rate_sync") == 0) {
        lane->sync_division = parse_sync_division(val, lane->sync_division);
    }
    else if (strcmp(subkey, "phase") == 0) {
        lane->phase_offset = clampf((float)atof(val), 0.0f, 1.0f);
    }
    else if (strcmp(subkey, "depth") == 0) {
        lane->depth = clampf((float)atof(val), 0.0f, 1.0f);
    }
    else if (strcmp(subkey, "offset") == 0) {
        lane->offset = clampf((float)atof(val), -1.0f, 1.0f);
    }
    else if (strcmp(subkey, "polarity") == 0) {
        lane->bipolar = parse_bipolar(val, lane->bipolar);
    }
    else if (strcmp(subkey, "retrigger") == 0) {
        lane->retrigger = parse_toggle(val, lane->retrigger);
    }
    else if (strcmp(subkey, "enable") == 0) {
        int new_enabled = parse_toggle(val, lane->enabled);
        if (!new_enabled && lane->modulation_active) {
            clear_modulation(lane);
        }
        lane->enabled = new_enabled;
    }
    else if (strcmp(subkey, "target_component") == 0) {
        if (lane->modulation_active) {
            clear_modulation(lane);
        }
        strncpy(lane->target_component, val, sizeof(lane->target_component) - 1);
        lane->target_component[sizeof(lane->target_component) - 1] = '\0';
    }
    else if (strcmp(subkey, "target_param") == 0) {
        if (lane->modulation_active) {
            clear_modulation(lane);
        }
        strncpy(lane->target_param, val, sizeof(lane->target_param) - 1);
        lane->target_param[sizeof(lane->target_param) - 1] = '\0';
    }
}

static int get_lane_param(const param_lfo_lane_t *lane, const char *subkey, char *buf, int buf_len) {
    if (!lane || !subkey || !buf || buf_len < 1) return -1;

    if (strcmp(subkey, "waveform") == 0) {
        return snprintf(buf, buf_len, "%s", waveform_to_string(lane->waveform));
    }
    if (strcmp(subkey, "rate_mode") == 0) {
        return snprintf(buf, buf_len, "%s", rate_mode_to_string(lane->rate_mode));
    }
    if (strcmp(subkey, "rate_hz") == 0) {
        if (lane->rate_mode == RATE_MODE_SYNC) {
            return snprintf(buf, buf_len, "%s", sync_division_to_string(lane->sync_division));
        }
        return snprintf(buf, buf_len, "%.4f", lane->rate_hz);
    }
    if (strcmp(subkey, "rate_sync") == 0) {
        return snprintf(buf, buf_len, "%s", sync_division_to_string(lane->sync_division));
    }
    if (strcmp(subkey, "phase") == 0) {
        return snprintf(buf, buf_len, "%.4f", lane->phase_offset);
    }
    if (strcmp(subkey, "depth") == 0) {
        return snprintf(buf, buf_len, "%.4f", lane->depth);
    }
    if (strcmp(subkey, "offset") == 0) {
        return snprintf(buf, buf_len, "%.4f", lane->offset);
    }
    if (strcmp(subkey, "polarity") == 0) {
        return snprintf(buf, buf_len, "%s", bipolar_to_string(lane->bipolar));
    }
    if (strcmp(subkey, "retrigger") == 0) {
        return snprintf(buf, buf_len, "%s", lane->retrigger ? "on" : "off");
    }
    if (strcmp(subkey, "enable") == 0) {
        return snprintf(buf, buf_len, "%s", lane->enabled ? "on" : "off");
    }
    if (strcmp(subkey, "target_component") == 0) {
        return snprintf(buf, buf_len, "%s", lane->target_component);
    }
    if (strcmp(subkey, "target_param") == 0) {
        return snprintf(buf, buf_len, "%s", lane->target_param);
    }

    return -1;
}

static void apply_state_json(param_lfo_instance_t *inst, const char *json) {
    if (!inst || !json) return;

    char key[64];
    char text[64];
    char val_buf[32];
    float f = 0.0f;

    for (int i = 0; i < PARAM_LFO_COUNT; i++) {
        param_lfo_lane_t *lane = &inst->lanes[i];

        snprintf(key, sizeof(key), "lfo%d_waveform", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "waveform", text);
        }

        snprintf(key, sizeof(key), "lfo%d_rate_mode", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "rate_mode", text);
        }

        snprintf(key, sizeof(key), "lfo%d_rate_sync", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "rate_sync", text);
        }

        snprintf(key, sizeof(key), "lfo%d_rate_hz", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "rate_hz", text);
        } else if (json_get_float(json, key, &f)) {
            snprintf(val_buf, sizeof(val_buf), "%.6f", f);
            set_lane_param(lane, "rate_hz", val_buf);
        }

        snprintf(key, sizeof(key), "lfo%d_phase", i + 1);
        if (json_get_float(json, key, &f)) {
            snprintf(val_buf, sizeof(val_buf), "%.6f", f);
            set_lane_param(lane, "phase", val_buf);
        }

        snprintf(key, sizeof(key), "lfo%d_depth", i + 1);
        if (json_get_float(json, key, &f)) {
            snprintf(val_buf, sizeof(val_buf), "%.6f", f);
            set_lane_param(lane, "depth", val_buf);
        }

        snprintf(key, sizeof(key), "lfo%d_offset", i + 1);
        if (json_get_float(json, key, &f)) {
            snprintf(val_buf, sizeof(val_buf), "%.6f", f);
            set_lane_param(lane, "offset", val_buf);
        }

        snprintf(key, sizeof(key), "lfo%d_polarity", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "polarity", text);
        }

        snprintf(key, sizeof(key), "lfo%d_retrigger", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "retrigger", text);
        }

        snprintf(key, sizeof(key), "lfo%d_enable", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "enable", text);
        }

        snprintf(key, sizeof(key), "lfo%d_target_component", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "target_component", text);
        }

        snprintf(key, sizeof(key), "lfo%d_target_param", i + 1);
        if (json_get_string(json, key, text, sizeof(text))) {
            set_lane_param(lane, "target_param", text);
        }
    }
}

static int build_state_json(param_lfo_instance_t *inst, char *buf, int buf_len) {
    int off = 0;

    if (!inst || !buf || buf_len < 4) return -1;
    if (appendf(buf, buf_len, &off, "{") < 0) return -1;

    for (int i = 0; i < PARAM_LFO_COUNT; i++) {
        const param_lfo_lane_t *lane = &inst->lanes[i];
        const int idx = i + 1;

        if (appendf(buf, buf_len, &off,
                    "%s\"lfo%d_waveform\":\"%s\",\"lfo%d_rate_mode\":\"%s\",\"lfo%d_rate_hz\":%.6f,\"lfo%d_rate_sync\":\"%s\",\"lfo%d_phase\":%.6f,\"lfo%d_depth\":%.6f,\"lfo%d_offset\":%.6f,\"lfo%d_polarity\":\"%s\",\"lfo%d_retrigger\":\"%s\",\"lfo%d_enable\":\"%s\",\"lfo%d_target_component\":\"%s\",\"lfo%d_target_param\":\"%s\"",
                    (i == 0) ? "" : ",",
                    idx, waveform_to_string(lane->waveform),
                    idx, rate_mode_to_string(lane->rate_mode),
                    idx, lane->rate_hz,
                    idx, sync_division_to_string(lane->sync_division),
                    idx, lane->phase_offset,
                    idx, lane->depth,
                    idx, lane->offset,
                    idx, bipolar_to_string(lane->bipolar),
                    idx, lane->retrigger ? "on" : "off",
                    idx, lane->enabled ? "on" : "off",
                    idx, lane->target_component,
                    idx, lane->target_param) < 0) {
            return -1;
        }
    }

    if (appendf(buf, buf_len, &off, "}") < 0) return -1;
    return off;
}

static int build_chain_params_json(const param_lfo_instance_t *inst, char *buf, int buf_len) {
    int off = 0;
    int first = 1;

    if (!buf || buf_len < 4) return -1;
    if (appendf(buf, buf_len, &off, "[") < 0) return -1;

    for (int i = 0; i < PARAM_LFO_COUNT; i++) {
        int idx = i + 1;

#define ADD_PARAM(fmt, ...) \
        do { \
            if (appendf(buf, buf_len, &off, "%s" fmt, first ? "" : ",", __VA_ARGS__) < 0) return -1; \
            first = 0; \
        } while (0)

        ADD_PARAM("{\"key\":\"lfo%d_waveform\",\"name\":\"Wave\",\"type\":\"enum\",\"options\":[\"sine\",\"triangle\",\"square\",\"saw_up\",\"random\",\"drunk\"],\"default\":0}", idx);
        ADD_PARAM("{\"key\":\"lfo%d_rate_mode\",\"name\":\"Rate Mode\",\"type\":\"enum\",\"options\":[\"free\",\"sync\"],\"default\":0}", idx);
        if (inst && inst->lanes[i].rate_mode == RATE_MODE_SYNC) {
            ADD_PARAM("{\"key\":\"lfo%d_rate_hz\",\"name\":\"Rate\",\"type\":\"enum\",\"options\":[\"8 bars\",\"4 bars\",\"2 bars\",\"1/1\",\"1/2\",\"1/4\",\"1/8\",\"1/16\",\"1/32\",\"1/64\"],\"default\":5}", idx);
        } else {
            ADD_PARAM("{\"key\":\"lfo%d_rate_hz\",\"name\":\"Rate\",\"type\":\"float\",\"min\":0.01,\"max\":20.0,\"default\":1.0,\"step\":0.01}", idx);
        }
        ADD_PARAM("{\"key\":\"lfo%d_phase\",\"name\":\"Phase\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"default\":0.0,\"step\":0.01}", idx);
        ADD_PARAM("{\"key\":\"lfo%d_depth\",\"name\":\"Depth\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"default\":0.25,\"step\":0.01}", idx);
        ADD_PARAM("{\"key\":\"lfo%d_offset\",\"name\":\"Offset\",\"type\":\"float\",\"min\":-1.0,\"max\":1.0,\"default\":0.0,\"step\":0.01}", idx);
        ADD_PARAM("{\"key\":\"lfo%d_polarity\",\"name\":\"Polarity\",\"type\":\"enum\",\"options\":[\"bipolar\",\"unipolar\"],\"default\":0}", idx);
        ADD_PARAM("{\"key\":\"lfo%d_retrigger\",\"name\":\"Retrigger\",\"type\":\"enum\",\"options\":[\"off\",\"on\"],\"default\":0}", idx);
        ADD_PARAM("{\"key\":\"lfo%d_enable\",\"name\":\"Enable\",\"type\":\"enum\",\"options\":[\"off\",\"on\"],\"default\":0}", idx);
        ADD_PARAM("{\"key\":\"lfo%d_target_component\",\"name\":\"Target Comp\",\"type\":\"module_picker\",\"allow_none\":true,\"allow_self\":false,\"param_key\":\"lfo%d_target_param\"}", idx, idx);
        ADD_PARAM("{\"key\":\"lfo%d_target_param\",\"name\":\"Target Param\",\"type\":\"parameter_picker\",\"target_key\":\"lfo%d_target_component\",\"numeric_only\":true,\"allow_none\":true}", idx, idx);

#undef ADD_PARAM
    }

    if (appendf(buf, buf_len, &off, "]") < 0) return -1;
    return off;
}

static void* param_lfo_create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json;

    param_lfo_instance_t *inst = calloc(1, sizeof(param_lfo_instance_t));
    if (!inst) return NULL;

    for (int i = 0; i < PARAM_LFO_COUNT; i++) {
        init_lane(&inst->lanes[i], inst, i);
    }

    return inst;
}

static void param_lfo_destroy_instance(void *instance) {
    param_lfo_instance_t *inst = (param_lfo_instance_t *)instance;
    if (!inst) return;

    for (int i = 0; i < PARAM_LFO_COUNT; i++) {
        clear_modulation(&inst->lanes[i]);
    }
    free(inst);
}

static void param_lfo_handle_midi(void *instance, const uint8_t *msg, int len) {
    param_lfo_instance_t *inst = (param_lfo_instance_t *)instance;
    if (!inst || !msg || len < 1) return;

    const uint8_t status = msg[0];
    const uint8_t type = status & 0xF0;

    /* Transport Start/Continue/Stop reset phase for deterministic sync restarts. */
    if (status == 0xFA || status == 0xFB || status == 0xFC) {
        reset_all_phases(inst);
        if (status == 0xFC) {
            inst->transport_running = 0;
            clear_held_notes(inst);
        } else {
            inst->transport_running = 1;
        }
    } else if (status == 0xF8 && inst->transport_running) {
        advance_sync_clock(inst);
    }

    /* Track held-note gate for retrigger behavior. */
    if (len >= 3) {
        if (type == 0x90 && msg[2] > 0) {
            handle_note_on(inst, msg[1]);
        } else if (type == 0x80 || (type == 0x90 && msg[2] == 0)) {
            handle_note_off(inst, msg[1]);
        }
    }
}

static void param_lfo_step(void *instance, int frames, int sample_rate) {
    param_lfo_instance_t *inst = (param_lfo_instance_t *)instance;
    if (!inst || frames <= 0 || sample_rate <= 0) return;

    if (!g_host || !g_host->mod_emit_value) return;

    for (int i = 0; i < PARAM_LFO_COUNT; i++) {
        param_lfo_lane_t *lane = &inst->lanes[i];

        if (!lane->enabled || !target_component_valid(lane->target_component) || !lane->target_param[0]) {
            if (lane->modulation_active) {
                clear_modulation(lane);
            }
            continue;
        }

        float sample = compute_lfo_sample(lane);

        if (lane->rate_mode == RATE_MODE_FREE) {
            float phase_inc = (lane->rate_hz * waveform_rate_multiplier(lane)) *
                              ((float)frames / (float)sample_rate);
            float new_phase = lane->phase + phase_inc;
            int wraps = (int)floorf(new_phase);
            lane->phase = wrap_phase(new_phase);
            for (int w = 0; w < wraps; w++) {
                advance_wave_cycle_state(lane);
            }
        }

        int rc = g_host->mod_emit_value(g_host->mod_host_ctx,
                                        lane->source_id,
                                        lane->target_component,
                                        lane->target_param,
                                        sample,
                                        lane->depth,
                                        lane->offset,
                                        lane->bipolar,
                                        1);
        if (rc == 0) {
            lane->modulation_active = 1;
        }
    }
}

static void param_lfo_set_param(void *instance, const char *key, const char *val) {
    param_lfo_instance_t *inst = (param_lfo_instance_t *)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "state") == 0) {
        apply_state_json(inst, val);
        return;
    }

    int lane_idx = -1;
    const char *subkey = NULL;
    if (!parse_lfo_key(key, &lane_idx, &subkey)) return;
    if (lane_idx < 0 || lane_idx >= PARAM_LFO_COUNT) return;

    set_lane_param(&inst->lanes[lane_idx], subkey, val);
}

static int param_lfo_get_param(void *instance, const char *key, char *buf, int buf_len) {
    param_lfo_instance_t *inst = (param_lfo_instance_t *)instance;
    if (!inst || !key || !buf || buf_len < 1) return -1;

    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "Param LFO");
    }

    if (strcmp(key, "state") == 0) {
        return build_state_json(inst, buf, buf_len);
    }

    if (strcmp(key, "chain_params") == 0) {
        return build_chain_params_json(inst, buf, buf_len);
    }

    int lane_idx = -1;
    const char *subkey = NULL;
    if (!parse_lfo_key(key, &lane_idx, &subkey)) return -1;
    if (lane_idx < 0 || lane_idx >= PARAM_LFO_COUNT) return -1;

    return get_lane_param(&inst->lanes[lane_idx], subkey, buf, buf_len);
}

/* === PARAM_LFO_SHARED_END === */

static int param_lfo_process_midi(void *instance,
                                  const uint8_t *in_msg, int in_len,
                                  uint8_t out_msgs[][3], int out_lens[],
                                  int max_out) {
    if (!in_msg || in_len < 1 || max_out < 1) return 0;
    param_lfo_handle_midi(instance, in_msg, in_len);

    out_msgs[0][0] = in_msg[0];
    out_msgs[0][1] = in_len > 1 ? in_msg[1] : 0;
    out_msgs[0][2] = in_len > 2 ? in_msg[2] : 0;
    out_lens[0] = in_len;
    return 1;
}

static int param_lfo_tick(void *instance,
                          int frames, int sample_rate,
                          uint8_t out_msgs[][3], int out_lens[],
                          int max_out) {
    (void)out_msgs;
    (void)out_lens;
    (void)max_out;
    param_lfo_step(instance, frames, sample_rate);
    return 0;
}

static midi_fx_api_v1_t g_api = {
    .api_version = MIDI_FX_API_VERSION,
    .create_instance = param_lfo_create_instance,
    .destroy_instance = param_lfo_destroy_instance,
    .process_midi = param_lfo_process_midi,
    .tick = param_lfo_tick,
    .set_param = param_lfo_set_param,
    .get_param = param_lfo_get_param
};

midi_fx_api_v1_t* move_midi_fx_init(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
