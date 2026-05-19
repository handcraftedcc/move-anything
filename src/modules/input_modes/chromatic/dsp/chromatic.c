#include "host/input_mode_api_v1.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    int root;
    int scale;
    int octave;
    uint8_t held[32];
} chromatic_state_t;

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

static void *chromatic_create(const char *module_dir) {
    (void)module_dir;
    return calloc(1, sizeof(chromatic_state_t));
}

static void chromatic_destroy(void *instance) {
    free(instance);
}

static void chromatic_set_param(void *instance, const char *key, const char *value) {
    chromatic_state_t *state = (chromatic_state_t *)instance;
    if (!state || !key) return;
    if (strcmp(key, "root") == 0) {
        state->root = clamp_int(parse_int(value, state->root), 0, 11);
    } else if (strcmp(key, "scale") == 0) {
        state->scale = clamp_int(parse_int(value, state->scale), 0, 2);
    } else if (strcmp(key, "octave") == 0) {
        state->octave = clamp_int(parse_int(value, state->octave), -5, 5);
    }
}

static int pitch_in_scale(int pitch, int root, int scale) {
    static const uint8_t major[] = {0, 2, 4, 5, 7, 9, 11};
    static const uint8_t minor[] = {0, 2, 3, 5, 7, 8, 10};
    if (scale == 2) return 1;
    const uint8_t *steps = scale == 1 ? minor : major;
    int rel = (pitch - root + 120) % 12;
    for (int i = 0; i < 7; i++) {
        if (steps[i] == rel) return 1;
    }
    return 0;
}

static int chromatic_emit_note(chromatic_state_t *state,
                               const schwung_input_module_event_t *event,
                               schwung_input_module_result_t *result) {
    if ((event->status & 0xF0) != 0x90 || event->data2 == 0) return 0;
    if (event->data1 < 68 || event->data1 > 99) return 0;
    int pad = event->data1 - 68;
    state->held[pad] = 1;
    int note = clamp_int(48 + state->octave * 12 + state->root + pad, 0, 127);
    result->packets[result->count][0] = 0x29;
    result->packets[result->count][1] = (uint8_t)(0x90 | (event->channel & 0x0F));
    result->packets[result->count][2] = (uint8_t)note;
    result->packets[result->count][3] = event->data2;
    result->count++;
    return 1;
}

static int chromatic_process_midi(void *instance,
                                  const schwung_input_module_event_t *event,
                                  const schwung_input_module_musical_context_t *musical_context,
                                  schwung_input_module_result_t *result) {
    (void)musical_context;
    if (!instance || !event || !result) return 0;
    chromatic_state_t *state = (chromatic_state_t *)instance;
    uint8_t type = event->status & 0xF0;
    if ((type == 0x80 || (type == 0x90 && event->data2 == 0)) &&
        event->data1 >= 68 && event->data1 <= 99) {
        state->held[event->data1 - 68] = 0;
        return 1;
    }
    return chromatic_emit_note((chromatic_state_t *)instance, event, result);
}

static int chromatic_process_button(void *instance,
                                    const schwung_input_module_event_t *event,
                                    const schwung_input_module_musical_context_t *musical_context,
                                    schwung_input_module_result_t *result) {
    (void)musical_context;
    (void)result;
    chromatic_state_t *state = (chromatic_state_t *)instance;
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

static int chromatic_update_leds(void *instance,
                                 const schwung_input_module_musical_context_t *musical_context,
                                 schwung_input_module_result_t *result) {
    (void)musical_context;
    chromatic_state_t *state = (chromatic_state_t *)instance;
    if (!state || !result) return 0;
    for (int i = 0; i < 32 && result->light_count < SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT; i++) {
        int note = 48 + state->octave * 12 + state->root + i;
        int pitch = ((note % 12) + 12) % 12;
        uint8_t color = 0;
        if (state->held[i]) {
            color = 127;
        } else if (pitch == state->root) {
            color = 122;
        } else if (pitch_in_scale(pitch, state->root, state->scale)) {
            color = 36;
        }
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
    .create = chromatic_create,
    .destroy = chromatic_destroy,
    .set_param = chromatic_set_param,
    .process_midi = chromatic_process_midi,
    .process_button = chromatic_process_button,
    .update_leds = chromatic_update_leds
};

schwung_input_module_api_v1_t *schwung_input_module_init_v1(void) {
    return &api;
}
