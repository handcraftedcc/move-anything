#include "host/input_mode_api_v1.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int root_octave;
    uint8_t held[32];
} drum32_state_t;

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

static void *drum32_create(const char *module_dir) {
    (void)module_dir;
    return calloc(1, sizeof(drum32_state_t));
}

static void drum32_destroy(void *instance) {
    free(instance);
}

static void drum32_set_param(void *instance, const char *key, const char *value) {
    drum32_state_t *state = (drum32_state_t *)instance;
    if (!state || !key) return;
    if (strcmp(key, "root_octave") == 0) {
        state->root_octave = clamp_int(parse_int(value, state->root_octave), -5, 5);
    }
}

static int drum32_process_midi(void *instance,
                               const schwung_input_module_event_t *event,
                               const schwung_input_module_musical_context_t *musical_context,
                               schwung_input_module_result_t *result) {
    (void)musical_context;
    if (!instance || !event || !result) return 0;
    uint8_t type = event->status & 0xF0;
    int is_note_on = (type == 0x90 && event->data2 > 0);
    int is_note_off = (type == 0x80 || (type == 0x90 && event->data2 == 0));
    if (!is_note_on && !is_note_off) return 0;
    if (event->data1 < 68 || event->data1 > 99) return 0;

    drum32_state_t *state = (drum32_state_t *)instance;
    int pad = event->data1 - 68;
    state->held[pad] = is_note_on ? 1 : 0;
    if (is_note_off) return 1;

    int note = clamp_int(36 + state->root_octave * 12 + pad, 0, 127);
    result->packets[result->count][0] = 0x29;
    result->packets[result->count][1] = (uint8_t)(0x90 | (event->channel & 0x0F));
    result->packets[result->count][2] = (uint8_t)note;
    result->packets[result->count][3] = event->data2;
    result->count++;
    return 1;
}

static int drum32_update_leds(void *instance,
                              const schwung_input_module_musical_context_t *musical_context,
                              schwung_input_module_result_t *result) {
    (void)musical_context;
    drum32_state_t *state = (drum32_state_t *)instance;
    if (!result) return 0;
    for (int i = 0; i < 32 && result->light_count < SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT; i++) {
        uint8_t *pkt = result->light_packets[result->light_count++];
        pkt[0] = 0x09;
        pkt[1] = 0x90;
        pkt[2] = (uint8_t)(68 + i);
        pkt[3] = (state && state->held[i]) ? 127 : ((i < 16) ? 68 : 36);
    }
    return 1;
}

static schwung_input_module_api_v1_t api = {
    .api_version = SCHWUNG_INPUT_MODULE_API_VERSION,
    .create = drum32_create,
    .destroy = drum32_destroy,
    .set_param = drum32_set_param,
    .process_midi = drum32_process_midi,
    .process_button = NULL,
    .update_leds = drum32_update_leds
};

schwung_input_module_api_v1_t *schwung_input_module_init_v1(void) {
    return &api;
}
