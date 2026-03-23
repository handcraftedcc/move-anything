/*
 * Param Lab MIDI FX
 *
 * Test/validation module for metadata-driven parameter UIs.
 * It intentionally performs minimal MIDI processing and focuses on
 * reliable set/get behavior for many parameter shapes.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "host/midi_fx_api_v1.h"
#include "host/plugin_api_v1.h"

#define PARAM_LAB_MAX_PARAMS 64
#define PARAM_LAB_KEY_LEN 64
#define PARAM_LAB_VAL_LEN 256

typedef struct {
    char key[PARAM_LAB_KEY_LEN];
    char value[PARAM_LAB_VAL_LEN];
} param_entry_t;

typedef struct {
    param_entry_t params[PARAM_LAB_MAX_PARAMS];
    int param_count;
    int button_ping_count;
    int midi_event_seq;
    int pad_flash_active;
    int pad_flash_led_on;
    uint64_t pad_flash_started_ms;
    uint64_t pad_flash_next_toggle_ms;
} param_lab_instance_t;

typedef struct {
    const char *key;
    const char *value;
} default_param_t;

static const default_param_t k_defaults[] = {
    {"float_gain", "0.500"},
    {"int_steps", "8"},
    {"percentage_mix", "35"},
    {"enum_wave", "sine"},
    {"boolean_sync", "off"},
    {"hide_actions_folder", "off"},
    {"note_root", "60"},
    {"time_div", "1/8"},
    {"bipolar_pan", "0.000"},
    {"string_name", "param-lab"},
    {"filepath_sample", ""},
    {"waveform_position", "0.250"},
    {"mod_target_1", "float_gain"},
    {"button_ping", ""},
    {"canvas_env", ""},
    {"module_target", ""},
    {"target_param", ""},
    {"sync_only_amount", "0.200"},
    {"canvas_midi", ""},
    {"last_callback", ""},
    {"cb_on_enter", ""},
    {"cb_on_modify", ""},
    {"cb_on_exit", ""},
    {"cb_on_cancel", ""},
    {"cb_button", ""},
    {"cb_canvas", ""}
};

static const host_api_v1_t *g_host_api = NULL;

#define PAD_NOTE_START 68
#define PAD_NOTE_END 99
#define PAD_FLASH_COLOR 127
#define PAD_FLASH_DURATION_MS 2000ULL
#define PAD_FLASH_TOGGLE_MS 150ULL

static uint64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static void send_all_pad_leds(uint8_t color) {
    if (!g_host_api || !g_host_api->midi_send_internal) return;
    for (int note = PAD_NOTE_START; note <= PAD_NOTE_END; note++) {
        uint8_t msg[4] = {
            0x09,   /* USB-MIDI CIN note-on, cable 0 */
            0x90,   /* note-on ch1 */
            (uint8_t)note,
            color
        };
        g_host_api->midi_send_internal(msg, 4);
    }
}

static void start_pad_flash(param_lab_instance_t *inst) {
    if (!inst) return;
    uint64_t now = now_ms();
    inst->pad_flash_active = 1;
    inst->pad_flash_led_on = 1;
    inst->pad_flash_started_ms = now;
    inst->pad_flash_next_toggle_ms = now + PAD_FLASH_TOGGLE_MS;
    send_all_pad_leds(PAD_FLASH_COLOR);
}

static param_entry_t* find_entry(param_lab_instance_t *inst, const char *key) {
    if (!inst || !key) return NULL;
    for (int i = 0; i < inst->param_count; i++) {
        if (strcmp(inst->params[i].key, key) == 0) {
            return &inst->params[i];
        }
    }
    return NULL;
}

static void set_value(param_lab_instance_t *inst, const char *key, const char *value) {
    if (!inst || !key) return;
    param_entry_t *entry = find_entry(inst, key);
    if (!entry) return;
    snprintf(entry->value, sizeof(entry->value), "%s", value ? value : "");
}

static const char* get_value(param_lab_instance_t *inst, const char *key) {
    param_entry_t *entry = find_entry(inst, key);
    if (!entry) return NULL;
    return entry->value;
}

static void* param_lab_create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json;

    param_lab_instance_t *inst = calloc(1, sizeof(param_lab_instance_t));
    if (!inst) return NULL;

    const int default_count = (int)(sizeof(k_defaults) / sizeof(k_defaults[0]));
    for (int i = 0; i < default_count && i < PARAM_LAB_MAX_PARAMS; i++) {
        snprintf(inst->params[i].key, sizeof(inst->params[i].key), "%s", k_defaults[i].key);
        snprintf(inst->params[i].value, sizeof(inst->params[i].value), "%s", k_defaults[i].value);
    }
    inst->param_count = default_count < PARAM_LAB_MAX_PARAMS ? default_count : PARAM_LAB_MAX_PARAMS;
    inst->button_ping_count = 0;
    inst->midi_event_seq = 0;
    inst->pad_flash_active = 0;
    inst->pad_flash_led_on = 0;
    inst->pad_flash_started_ms = 0;
    inst->pad_flash_next_toggle_ms = 0;

    return inst;
}

static void param_lab_destroy_instance(void *instance) {
    if (instance) free(instance);
}

static int param_lab_process_midi(void *instance,
                                  const uint8_t *in_msg, int in_len,
                                  uint8_t out_msgs[][3], int out_lens[],
                                  int max_out) {
    param_lab_instance_t *inst = (param_lab_instance_t *)instance;
    if (!in_msg || in_len < 1 || max_out < 1) return 0;

    if (inst && in_len >= 3) {
        const uint8_t status = in_msg[0] & 0xFF;
        const uint8_t status_nibble = status & 0xF0;
        const uint8_t channel = status & 0x0F;
        const uint8_t note = in_msg[1] & 0x7F;
        const uint8_t value = in_msg[2] & 0x7F;

        /* Mirror note events for channel 1 into a pollable param for canvas overlays. */
        if ((status_nibble == 0x90 || status_nibble == 0x80) && channel == 0) {
            char event[64];
            inst->midi_event_seq++;
            snprintf(event, sizeof(event), "%d,%u,%u,%u",
                     inst->midi_event_seq,
                     (unsigned int)status,
                     (unsigned int)note,
                     (unsigned int)value);
            set_value(inst, "canvas_midi", event);
        }
    }

    out_msgs[0][0] = in_msg[0];
    out_msgs[0][1] = in_len > 1 ? in_msg[1] : 0;
    out_msgs[0][2] = in_len > 2 ? in_msg[2] : 0;
    out_lens[0] = in_len;
    return 1;
}

static int param_lab_tick(void *instance,
                          int frames, int sample_rate,
                          uint8_t out_msgs[][3], int out_lens[],
                          int max_out) {
    param_lab_instance_t *inst = (param_lab_instance_t *)instance;
    (void)frames;
    (void)sample_rate;
    (void)out_msgs;
    (void)out_lens;
    (void)max_out;

    if (!inst || !inst->pad_flash_active) return 0;

    const uint64_t now = now_ms();
    if (now >= inst->pad_flash_started_ms + PAD_FLASH_DURATION_MS) {
        send_all_pad_leds(0);
        inst->pad_flash_active = 0;
        inst->pad_flash_led_on = 0;
        return 0;
    }

    if (now >= inst->pad_flash_next_toggle_ms) {
        inst->pad_flash_led_on = inst->pad_flash_led_on ? 0 : 1;
        send_all_pad_leds(inst->pad_flash_led_on ? PAD_FLASH_COLOR : 0);
        inst->pad_flash_next_toggle_ms = now + PAD_FLASH_TOGGLE_MS;
    }

    return 0;
}

static void param_lab_set_param(void *instance, const char *key, const char *val) {
    param_lab_instance_t *inst = (param_lab_instance_t *)instance;
    if (!inst || !key) return;

    set_value(inst, key, val ? val : "");

    if (strcmp(key, "button_ping") == 0 && val && strcmp(val, "trigger") == 0) {
        char msg[64];
        inst->button_ping_count++;
        snprintf(msg, sizeof(msg), "button_ping:%d", inst->button_ping_count);
        set_value(inst, "last_callback", msg);
    } else if (strncmp(key, "cb_", 3) == 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s:%s", key, val ? val : "");
        set_value(inst, "last_callback", msg);

        if (strcmp(key, "cb_button") == 0 && val &&
            strstr(val, "\"key\":\"button_ping\"") &&
            strstr(val, "\"event\":\"onModify\"") &&
            strstr(val, "\"value\":\"trigger\"")) {
            start_pad_flash(inst);
        }
    }
}

static int param_lab_get_param(void *instance, const char *key, char *buf, int buf_len) {
    param_lab_instance_t *inst = (param_lab_instance_t *)instance;
    if (!inst || !key || !buf || buf_len < 1) return -1;

    if (strcmp(key, "button_ping_count") == 0) {
        return snprintf(buf, buf_len, "%d", inst->button_ping_count);
    }

    if (strcmp(key, "state") == 0) {
        const char *name = get_value(inst, "string_name");
        const char *wave = get_value(inst, "enum_wave");
        const char *sync = get_value(inst, "boolean_sync");
        const char *cb = get_value(inst, "last_callback");
        return snprintf(buf, buf_len,
                        "{\"string_name\":\"%s\",\"enum_wave\":\"%s\",\"boolean_sync\":\"%s\",\"last_callback\":\"%s\"}",
                        name ? name : "",
                        wave ? wave : "",
                        sync ? sync : "",
                        cb ? cb : "");
    }

    {
        const char *val = get_value(inst, key);
        if (!val) return -1;
        return snprintf(buf, buf_len, "%s", val);
    }
}

static midi_fx_api_v1_t g_api = {
    .api_version = MIDI_FX_API_VERSION,
    .create_instance = param_lab_create_instance,
    .destroy_instance = param_lab_destroy_instance,
    .process_midi = param_lab_process_midi,
    .tick = param_lab_tick,
    .set_param = param_lab_set_param,
    .get_param = param_lab_get_param
};

midi_fx_api_v1_t* move_midi_fx_init(const host_api_v1_t *host) {
    g_host_api = host;
    return &g_api;
}
