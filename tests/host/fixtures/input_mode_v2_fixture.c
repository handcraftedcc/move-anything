#include "host/input_mode_api_v1.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int pending_on;
    int pending_off;
    int silent_active;
    int active;
    uint8_t channel;
} fixture_state_t;

static void append_packet(schwung_input_module_result_t *result,
                          uint8_t cin,
                          uint8_t status,
                          uint8_t data1,
                          uint8_t data2) {
    if (!result || result->count >= SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT) return;
    uint8_t *pkt = result->packets[result->count++];
    pkt[0] = cin;
    pkt[1] = status;
    pkt[2] = data1;
    pkt[3] = data2;
}

static void append_light(schwung_input_module_result_t *result,
                         uint8_t note,
                         uint8_t color) {
    if (!result || result->light_count >= SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT) return;
    uint8_t *pkt = result->light_packets[result->light_count++];
    pkt[0] = 0x09;
    pkt[1] = 0x90;
    pkt[2] = note;
    pkt[3] = color;
}

static void *fixture_create(const char *module_dir) {
    (void)module_dir;
    return calloc(1, sizeof(fixture_state_t));
}

static void fixture_destroy(void *instance) {
    free(instance);
}

static int fixture_process_midi(void *instance,
                                const schwung_input_module_event_t *event,
                                const schwung_input_module_musical_context_t *ctx,
                                schwung_input_module_result_t *result) {
    (void)ctx;
    fixture_state_t *state = (fixture_state_t *)instance;
    if (!state || !event || !result) return 0;

    uint8_t type = event->status & 0xF0;
    uint8_t channel = event->status & 0x0F;
    if (type == 0x90 && event->data2 > 0) {
        state->active = 1;
        state->channel = channel;
        if (event->data1 == 69) {
            state->silent_active = 1;
            return 1;
        }
        state->pending_on = 1;
        append_packet(result, (uint8_t)((2 << 4) | 0x09),
                      (uint8_t)(0x90 | channel), 60, event->data2);
        return 1;
    }
    if (type == 0x80 || (type == 0x90 && event->data2 == 0)) {
        if (!state->active) return 1;
        state->active = 0;
        if (state->silent_active) {
            state->silent_active = 0;
            return 1;
        }
        state->pending_off = 1;
        append_packet(result, (uint8_t)((2 << 4) | 0x08),
                      (uint8_t)(0x80 | state->channel), 60, 0);
        return 1;
    }
    return 0;
}

static int fixture_update_leds(void *instance,
                               const schwung_input_module_musical_context_t *ctx,
                               schwung_input_module_result_t *result) {
    (void)instance;
    (void)ctx;
    append_light(result, 68, 36);
    return 1;
}

static int fixture_tick(void *instance,
                        int frames,
                        int sample_rate,
                        const schwung_input_module_musical_context_t *ctx,
                        schwung_input_module_result_t *result) {
    (void)frames;
    (void)sample_rate;
    (void)ctx;
    fixture_state_t *state = (fixture_state_t *)instance;
    if (!state || !result) return 0;
    if (state->pending_on) {
        state->pending_on = 0;
        append_packet(result, (uint8_t)((2 << 4) | 0x09),
                      (uint8_t)(0x90 | state->channel), 64, 100);
        return 1;
    }
    if (state->pending_off) {
        state->pending_off = 0;
        append_packet(result, (uint8_t)((2 << 4) | 0x08),
                      (uint8_t)(0x80 | state->channel), 64, 0);
        return 1;
    }
    return 0;
}

static int fixture_panic(void *instance,
                         const schwung_input_module_musical_context_t *ctx,
                         schwung_input_module_result_t *result) {
    (void)ctx;
    fixture_state_t *state = (fixture_state_t *)instance;
    if (!state || !result) return 0;
    append_packet(result, (uint8_t)((2 << 4) | 0x08),
                  (uint8_t)(0x80 | state->channel), 60, 0);
    append_packet(result, (uint8_t)((2 << 4) | 0x08),
                  (uint8_t)(0x80 | state->channel), 64, 0);
    state->active = 0;
    state->pending_on = 0;
    state->pending_off = 0;
    state->silent_active = 0;
    return 1;
}

static schwung_input_module_api_v2_t g_api = {
    .api_version = SCHWUNG_INPUT_MODULE_API_VERSION_V2,
    .create = fixture_create,
    .destroy = fixture_destroy,
    .process_midi = fixture_process_midi,
    .update_leds = fixture_update_leds,
    .struct_size = sizeof(schwung_input_module_api_v2_t),
    .tick = fixture_tick,
    .panic = fixture_panic
};

schwung_input_module_api_v2_t *schwung_input_module_init_v2(void) {
    return &g_api;
}
