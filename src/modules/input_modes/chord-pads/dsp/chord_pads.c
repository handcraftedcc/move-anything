#include "host/input_mode_api_v1.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    int root;
    int scale;
    int octave;
    int index_2;
    int index_3;
    uint8_t held[32];
} chord_pads_state_t;

static int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static int parse_int(const char *value, int fallback) {
    if (!value) return fallback;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    return end != value ? (int)parsed : fallback;
}

static int scale_step(int scale, int index) {
    static const uint8_t major[] = {0, 2, 4, 5, 7, 9, 11};
    static const uint8_t minor[] = {0, 2, 3, 5, 7, 8, 10};
    const uint8_t *steps = major;
    int step_count = 7;
    if (scale == 1) {
        steps = minor;
    } else if (scale == 2) {
        return index;
    }
    if (index < 0) index = 0;
    return (index / step_count) * 12 + steps[index % step_count];
}

static void *chord_pads_create(const char *module_dir) {
    (void)module_dir;
    chord_pads_state_t *state = (chord_pads_state_t *)calloc(1, sizeof(chord_pads_state_t));
    if (state) {
        state->index_2 = 2;
        state->index_3 = 4;
    }
    return state;
}

static void chord_pads_destroy(void *instance) {
    free(instance);
}

static void chord_pads_set_param(void *instance, const char *key, const char *value) {
    chord_pads_state_t *state = (chord_pads_state_t *)instance;
    if (!state || !key) return;
    if (strcmp(key, "root") == 0) {
        state->root = clamp_int(parse_int(value, state->root), 0, 11);
    } else if (strcmp(key, "scale") == 0) {
        state->scale = clamp_int(parse_int(value, state->scale), 0, 2);
    } else if (strcmp(key, "octave") == 0) {
        state->octave = clamp_int(parse_int(value, state->octave), -5, 5);
    } else if (strcmp(key, "index_2") == 0) {
        state->index_2 = clamp_int(parse_int(value, state->index_2), 0, 15);
    } else if (strcmp(key, "index_3") == 0) {
        state->index_3 = clamp_int(parse_int(value, state->index_3), 0, 15);
    }
}

static void emit_note(schwung_input_module_result_t *result,
                      uint8_t channel,
                      uint8_t note,
                      uint8_t velocity) {
    result->packets[result->count][0] = 0x29;
    result->packets[result->count][1] = (uint8_t)(0x90 | (channel & 0x0F));
    result->packets[result->count][2] = note;
    result->packets[result->count][3] = velocity;
    result->count++;
}

static int chord_pads_process_midi(void *instance,
                                   const schwung_input_module_event_t *event,
                                   const schwung_input_module_musical_context_t *musical_context,
                                   schwung_input_module_result_t *result) {
    (void)musical_context;
    chord_pads_state_t *state = (chord_pads_state_t *)instance;
    if (!state || !event || !result) return 0;
    uint8_t type = event->status & 0xF0;
    int is_note_on = (type == 0x90 && event->data2 > 0);
    int is_note_off = (type == 0x80 || (type == 0x90 && event->data2 == 0));
    if (!is_note_on && !is_note_off) return 0;
    if (event->data1 < 68 || event->data1 > 99) return 0;

    int pad = event->data1 - 68;
    state->held[pad] = is_note_on ? 1 : 0;
    if (is_note_off) return 1;

    int degree = pad % 7;
    int pad_octave = pad / 7;
    int base = 60 + state->octave * 12 + state->root + pad_octave * 12;
    emit_note(result, event->channel, (uint8_t)clamp_int(base + scale_step(state->scale, degree), 0, 127), event->data2);
    emit_note(result, event->channel, (uint8_t)clamp_int(base + scale_step(state->scale, degree + state->index_2), 0, 127), event->data2);
    emit_note(result, event->channel, (uint8_t)clamp_int(base + scale_step(state->scale, degree + state->index_3), 0, 127), event->data2);
    return 1;
}

static int chord_pads_process_button(void *instance,
                                     const schwung_input_module_event_t *event,
                                     const schwung_input_module_musical_context_t *musical_context,
                                     schwung_input_module_result_t *result) {
    (void)musical_context;
    (void)result;
    chord_pads_state_t *state = (chord_pads_state_t *)instance;
    if (!state || !event) return 0;
    if ((event->status & 0xF0) != 0xB0 || event->data2 == 0) return 0;
    int handled = 0;
    if (event->data1 == 55) {
        state->octave = clamp_int(state->octave + 1, -5, 5);
        handled = 1;
    } else if (event->data1 == 54) {
        state->octave = clamp_int(state->octave - 1, -5, 5);
        handled = 1;
    }
    if (handled && result && result->param_update_count < SCHWUNG_INPUT_MODULE_MAX_PARAM_UPDATES) {
        schwung_input_module_param_update_t *update = &result->param_updates[result->param_update_count++];
        snprintf(update->key, sizeof(update->key), "%s", "octave");
        snprintf(update->value, sizeof(update->value), "%d", state->octave);
    }
    return handled;
}

static int chord_pads_update_leds(void *instance,
                                  const schwung_input_module_musical_context_t *musical_context,
                                  schwung_input_module_result_t *result) {
    (void)musical_context;
    chord_pads_state_t *state = (chord_pads_state_t *)instance;
    if (!state || !result) return 0;
    for (int i = 0; i < 32 && result->light_count < SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT; i++) {
        int degree = i % 7;
        uint8_t color = state->held[i] ? 127 : (degree == 0 ? 122 : 36);
        uint8_t *pkt = result->light_packets[result->light_count++];
        pkt[0] = 0x09;
        pkt[1] = 0x90;
        pkt[2] = (uint8_t)(68 + i);
        pkt[3] = color;
    }
    return 1;
}

static schwung_input_module_api_v1_t api = {
    .api_version = SCHWUNG_INPUT_MODULE_API_VERSION,
    .create = chord_pads_create,
    .destroy = chord_pads_destroy,
    .set_param = chord_pads_set_param,
    .process_midi = chord_pads_process_midi,
    .process_button = chord_pads_process_button,
    .update_leds = chord_pads_update_leds
};

schwung_input_module_api_v1_t *schwung_input_module_init_v1(void) {
    return &api;
}
