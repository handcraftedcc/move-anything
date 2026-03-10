#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "host/plugin_api_v1.h"
#include "host/shadow_midi_to_move.h"
#include "host/shadow_midi_to_move_shm.h"

#define SELF_ECHO_WINDOW_MS 80u
#define SELF_ECHO_RING_SIZE 128u

typedef struct self_echo_event {
    uint64_t timestamp_ms;
    uint8_t status;
    uint8_t d1;
    uint8_t d2;
} self_echo_event_t;

typedef enum source_mode {
    SOURCE_MODE_BOTH = 0,
    SOURCE_MODE_INTERNAL = 1,
    SOURCE_MODE_EXTERNAL = 2
} source_mode_t;

typedef struct midi_inject_test_instance {
    uint32_t received_packets;
    uint32_t forwarded_packets;
    uint32_t dropped_packets;
    uint32_t dropped_source;
    uint32_t dropped_system;
    uint32_t dropped_non_channel;
    uint32_t dropped_aftertouch;
    uint32_t dropped_self_echo;
    uint32_t dropped_queue_full;

    uint8_t last_status;
    uint8_t last_source;

    int output_channel;              /* -1 = thru, 0..15 = force channel */
    source_mode_t source_mode;

    self_echo_event_t echo_ring[SELF_ECHO_RING_SIZE];
    uint32_t echo_ring_write;
} midi_inject_test_instance_t;

static const host_api_v1_t *g_host = NULL;
static int g_log_fd = -1;
static uint32_t g_instance_count = 0;
static uint32_t g_external_mode_instance_count = 0;
static uint32_t g_internal_mode_instance_count = 0;
static const char *k_log_path = "/data/UserData/move-anything/midi_inject_test.log";
static const char *k_ui_hierarchy_json =
    "{"
        "\"levels\":{"
            "\"root\":{"
                "\"label\":\"MIDI Inject Test\","
                "\"params\":["
                    "{\"key\":\"out_channel\",\"label\":\"Output Ch\"},"
                    "{\"key\":\"source_mode\",\"label\":\"Input Src\"}"
                "],"
                "\"knobs\":[\"out_channel\",\"source_mode\"]"
            "}"
        "}"
    "}";

static const char *k_chain_params_json =
    "["
        "{\"key\":\"out_channel\",\"name\":\"Output Ch\",\"type\":\"int\",\"min\":0,\"max\":16,\"default\":0,\"step\":1},"
        "{\"key\":\"source_mode\",\"name\":\"Input Src\",\"type\":\"enum\",\"options\":[\"both\",\"internal\",\"external\"],\"default\":2}"
    "]";

static void midi_inject_log_open(void)
{
    if (g_log_fd >= 0) return;
    g_log_fd = open(k_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
}

static void midi_inject_log_close(void)
{
    if (g_log_fd >= 0) {
        close(g_log_fd);
        g_log_fd = -1;
    }
}

static void midi_inject_logf(const char *fmt, ...)
{
    if (!fmt) return;
    midi_inject_log_open();
    if (g_log_fd < 0) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);

    char msg[512];
    int prefix_len = snprintf(msg, sizeof(msg), "%02d:%02d:%02d.%03ld [midi_inject_test] ",
                              tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000L);
    if (prefix_len < 0 || prefix_len >= (int)sizeof(msg)) return;

    va_list ap;
    va_start(ap, fmt);
    int body_len = vsnprintf(msg + prefix_len, sizeof(msg) - (size_t)prefix_len, fmt, ap);
    va_end(ap);
    if (body_len < 0) return;

    size_t total_len = (size_t)prefix_len + (size_t)body_len;
    if (total_len >= sizeof(msg)) total_len = sizeof(msg) - 1u;

    msg[total_len++] = '\n';
    (void)write(g_log_fd, msg, total_len);
}

static int should_log_sample(uint32_t count, uint32_t interval_mask)
{
    return (count <= 8u) || ((count & interval_mask) == 0u);
}

static uint64_t now_mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000u) + (uint64_t)(ts.tv_nsec / 1000000u);
}

static const char *source_mode_to_string(source_mode_t mode)
{
    switch (mode) {
        case SOURCE_MODE_INTERNAL: return "internal";
        case SOURCE_MODE_EXTERNAL: return "external";
        default: return "both";
    }
}

static source_mode_t parse_source_mode(const char *val)
{
    if (!val) return SOURCE_MODE_BOTH;
    if (strcmp(val, "internal") == 0) return SOURCE_MODE_INTERNAL;
    if (strcmp(val, "external") == 0) return SOURCE_MODE_EXTERNAL;
    if (strcmp(val, "both") == 0) return SOURCE_MODE_BOTH;

    /* Accept enum index fallback. */
    int idx = atoi(val);
    if (idx <= 0) return SOURCE_MODE_BOTH;
    if (idx == 1) return SOURCE_MODE_INTERNAL;
    return SOURCE_MODE_EXTERNAL;
}

static int source_mode_is_external_only(source_mode_t mode)
{
    return mode == SOURCE_MODE_EXTERNAL;
}

static int source_mode_is_internal_only(source_mode_t mode)
{
    return mode == SOURCE_MODE_INTERNAL;
}

static void refresh_inject_mode_flags(void)
{
    (void)shadow_midi_to_move_set_mode_flag(
        SHADOW_MIDI_TO_MOVE_MODE_EXTERNAL,
        g_external_mode_instance_count > 0 ? 1 : 0);
    (void)shadow_midi_to_move_set_mode_flag(
        SHADOW_MIDI_TO_MOVE_MODE_INTERNAL,
        g_internal_mode_instance_count > 0 ? 1 : 0);
}

static int source_is_allowed(const midi_inject_test_instance_t *inst, int source)
{
    if (!inst) return 0;

    switch (inst->source_mode) {
        case SOURCE_MODE_INTERNAL:
            return source == MOVE_MIDI_SOURCE_INTERNAL;
        case SOURCE_MODE_EXTERNAL:
            return source == MOVE_MIDI_SOURCE_EXTERNAL;
        case SOURCE_MODE_BOTH:
        default:
            return (source == MOVE_MIDI_SOURCE_INTERNAL ||
                    source == MOVE_MIDI_SOURCE_EXTERNAL);
    }
}

static uint8_t apply_output_channel(const midi_inject_test_instance_t *inst, uint8_t status)
{
    if (!inst) return status;
    if (inst->output_channel < 0 || status >= 0xF0) return status;
    return (uint8_t)((status & 0xF0) | (inst->output_channel & 0x0F));
}

static void remember_emitted_event(midi_inject_test_instance_t *inst,
                                   uint8_t status, uint8_t d1, uint8_t d2)
{
    if (!inst) return;

    uint8_t type = (uint8_t)(status & 0xF0);
    if (type < 0x80 || type > 0xE0) return;

    uint32_t slot = inst->echo_ring_write % SELF_ECHO_RING_SIZE;
    inst->echo_ring_write++;
    inst->echo_ring[slot].timestamp_ms = now_mono_ms();
    inst->echo_ring[slot].status = status;
    inst->echo_ring[slot].d1 = d1;
    inst->echo_ring[slot].d2 = d2;
}

static int is_recent_self_echo(const midi_inject_test_instance_t *inst,
                               uint8_t status, uint8_t d1, uint8_t d2)
{
    if (!inst) return 0;

    uint64_t now = now_mono_ms();
    for (uint32_t i = 0; i < SELF_ECHO_RING_SIZE; i++) {
        const self_echo_event_t *ev = &inst->echo_ring[i];
        if (ev->timestamp_ms == 0) continue;
        if (ev->status != status) continue;
        if (ev->d1 != d1) continue;
        if (ev->d2 != d2) continue;
        if ((now - ev->timestamp_ms) <= SELF_ECHO_WINDOW_MS) return 1;
    }
    return 0;
}

static int status_to_cin_and_len(uint8_t status, uint8_t *cin_out, int *len_out)
{
    if (!cin_out || !len_out) return 0;

    switch (status & 0xF0) {
        case 0x80: *cin_out = 0x08; *len_out = 3; return 1;
        case 0x90: *cin_out = 0x09; *len_out = 3; return 1;
        case 0xA0: *cin_out = 0x0A; *len_out = 3; return 1;
        case 0xB0: *cin_out = 0x0B; *len_out = 3; return 1;
        case 0xC0: *cin_out = 0x0C; *len_out = 2; return 1;
        case 0xD0: *cin_out = 0x0D; *len_out = 2; return 1;
        case 0xE0: *cin_out = 0x0E; *len_out = 3; return 1;
        default: return 0;
    }
}

static int send_forwarded_message(midi_inject_test_instance_t *inst,
                                  uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2)
{
    if (!inst) return 0;

    uint8_t final_status = apply_output_channel(inst, status);
    if (shadow_midi_to_move_send_usb_packet(cin, final_status, d1, d2)) {
        inst->forwarded_packets++;
        remember_emitted_event(inst, final_status, d1, d2);
        if (should_log_sample(inst->forwarded_packets, 0x3Fu)) {
            midi_inject_logf("tx usb0=0x%02X status=0x%02X d1=%u d2=%u fwd_total=%u",
                             cin, final_status, d1, d2, inst->forwarded_packets);
        }
        return 1;
    }

    inst->dropped_packets++;
    inst->dropped_queue_full++;
    if (should_log_sample(inst->dropped_queue_full, 0x1Fu)) {
        midi_inject_logf("drop reason=queue-full status=0x%02X d1=%u d2=%u drop_queue=%u",
                         final_status, d1, d2, inst->dropped_queue_full);
    }
    return 0;
}

static void* create_instance(const char *module_dir, const char *json_defaults)
{
    (void)module_dir;
    (void)json_defaults;

    midi_inject_log_open();
    if (g_instance_count == 0) {
        midi_inject_logf("---------- session start ----------");
    }
    g_instance_count++;

    int opened = shadow_midi_to_move_open();
    midi_inject_test_instance_t *inst = (midi_inject_test_instance_t *)calloc(1, sizeof(midi_inject_test_instance_t));
    if (!inst) {
        midi_inject_logf("instance create failed");
        return NULL;
    }

    inst->output_channel = -1;
    inst->source_mode = SOURCE_MODE_EXTERNAL;
    if (source_mode_is_external_only(inst->source_mode)) {
        g_external_mode_instance_count++;
    }
    if (source_mode_is_internal_only(inst->source_mode)) {
        g_internal_mode_instance_count++;
    }
    refresh_inject_mode_flags();

    midi_inject_logf("instance created (queue=%s, source=%s, out=Thru)",
                     opened ? "ok" : "fail",
                     source_mode_to_string(inst->source_mode));
    return inst;
}

static void destroy_instance(void *instance)
{
    midi_inject_test_instance_t *inst = (midi_inject_test_instance_t *)instance;
    if (inst) {
        if (source_mode_is_external_only(inst->source_mode) &&
            g_external_mode_instance_count > 0) {
            g_external_mode_instance_count--;
        }
        if (source_mode_is_internal_only(inst->source_mode) &&
            g_internal_mode_instance_count > 0) {
            g_internal_mode_instance_count--;
        }
        refresh_inject_mode_flags();
        midi_inject_logf("instance destroyed recv=%u fwd=%u drop=%u source=%u system=%u non_channel=%u aftertouch=%u self_echo=%u queue=%u",
                         inst->received_packets,
                         inst->forwarded_packets,
                         inst->dropped_packets,
                         inst->dropped_source,
                         inst->dropped_system,
                         inst->dropped_non_channel,
                         inst->dropped_aftertouch,
                         inst->dropped_self_echo,
                         inst->dropped_queue_full);
    }
    free(inst);

    if (g_instance_count > 0) g_instance_count--;
    if (g_instance_count == 0) {
        midi_inject_logf("---------- session end ----------");
        midi_inject_log_close();
    }
}

static void render_block(void *instance, int16_t *out_lr, int frames)
{
    (void)instance;
    memset(out_lr, 0, (size_t)frames * 2u * sizeof(int16_t));
}

static void set_param(void *instance, const char *key, const char *val)
{
    midi_inject_test_instance_t *inst = (midi_inject_test_instance_t *)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "out_channel") == 0 || strcmp(key, "preset") == 0) {
        int ch = atoi(val);
        if (ch <= 0) {
            inst->output_channel = -1;
        } else if (ch > 16) {
            inst->output_channel = 15;
        } else {
            inst->output_channel = ch - 1;
        }

        if (inst->output_channel < 0) {
            midi_inject_logf("output channel set to Thru");
        } else {
            midi_inject_logf("output channel set to Ch %d", inst->output_channel + 1);
        }
        return;
    }

    if (strcmp(key, "source_mode") == 0) {
        source_mode_t old_mode = inst->source_mode;
        inst->source_mode = parse_source_mode(val);
        if (source_mode_is_external_only(old_mode) != source_mode_is_external_only(inst->source_mode) ||
            source_mode_is_internal_only(old_mode) != source_mode_is_internal_only(inst->source_mode)) {
            if (source_mode_is_external_only(inst->source_mode)) g_external_mode_instance_count++;
            else if (source_mode_is_external_only(old_mode) && g_external_mode_instance_count > 0)
                g_external_mode_instance_count--;

            if (source_mode_is_internal_only(inst->source_mode)) g_internal_mode_instance_count++;
            else if (source_mode_is_internal_only(old_mode) && g_internal_mode_instance_count > 0)
                g_internal_mode_instance_count--;

            refresh_inject_mode_flags();
        }
        midi_inject_logf("source mode set to %s", source_mode_to_string(inst->source_mode));
        return;
    }
}

static int get_param(void *instance, const char *key, char *buf, int buf_len)
{
    midi_inject_test_instance_t *inst = (midi_inject_test_instance_t *)instance;
    if (!inst || !key || !buf || buf_len <= 1) return -1;

    if (strcmp(key, "ui_hierarchy") == 0) {
        return snprintf(buf, (size_t)buf_len, "%s", k_ui_hierarchy_json);
    }
    if (strcmp(key, "chain_params") == 0) {
        return snprintf(buf, (size_t)buf_len, "%s", k_chain_params_json);
    }

    if (strcmp(key, "received_packets") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->received_packets);
    if (strcmp(key, "forwarded_packets") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->forwarded_packets);
    if (strcmp(key, "dropped_packets") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->dropped_packets);
    if (strcmp(key, "dropped_source") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->dropped_source);
    if (strcmp(key, "dropped_system") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->dropped_system);
    if (strcmp(key, "dropped_non_channel") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->dropped_non_channel);
    if (strcmp(key, "dropped_aftertouch") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->dropped_aftertouch);
    if (strcmp(key, "dropped_self_echo") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->dropped_self_echo);
    if (strcmp(key, "dropped_queue_full") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->dropped_queue_full);
    if (strcmp(key, "last_status") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->last_status);
    if (strcmp(key, "last_source") == 0) return snprintf(buf, (size_t)buf_len, "%u", inst->last_source);

    if (strcmp(key, "out_channel") == 0) return snprintf(buf, (size_t)buf_len, "%d", inst->output_channel + 1);
    if (strcmp(key, "preset") == 0) return snprintf(buf, (size_t)buf_len, "%d", inst->output_channel + 1);
    if (strcmp(key, "source_mode") == 0) return snprintf(buf, (size_t)buf_len, "%s", source_mode_to_string(inst->source_mode));

    return -1;
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source)
{
    midi_inject_test_instance_t *inst = (midi_inject_test_instance_t *)instance;
    if (!inst || !msg || len <= 0) return;

    inst->received_packets++;
    inst->last_status = msg[0];
    inst->last_source = (uint8_t)source;

    if (!source_is_allowed(inst, source)) {
        inst->dropped_source++;
        if (should_log_sample(inst->dropped_source, 0x3Fu)) {
            midi_inject_logf("drop reason=source src=%d mode=%s status=0x%02X dropped_source=%u",
                             source, source_mode_to_string(inst->source_mode), msg[0], inst->dropped_source);
        }
        return;
    }

    uint8_t status = msg[0];
    if (status >= 0xF0) {
        inst->dropped_system++;
        if (should_log_sample(inst->dropped_system, 0x7Fu)) {
            midi_inject_logf("drop reason=system status=0x%02X dropped_system=%u",
                             status, inst->dropped_system);
        }
        return;
    }

    uint8_t cin = 0;
    int expected_len = 0;
    if (!status_to_cin_and_len(status, &cin, &expected_len)) {
        inst->dropped_non_channel++;
        if (should_log_sample(inst->dropped_non_channel, 0x7Fu)) {
            midi_inject_logf("drop reason=non-channel status=0x%02X dropped_non_channel=%u",
                             status, inst->dropped_non_channel);
        }
        return;
    }

    uint8_t type = (uint8_t)(status & 0xF0);
    if (type == 0xA0 || type == 0xD0) {
        inst->dropped_aftertouch++;
        if (should_log_sample(inst->dropped_aftertouch, 0x7Fu)) {
            midi_inject_logf("drop reason=aftertouch status=0x%02X d1=%u d2=%u dropped_aftertouch=%u",
                             status,
                             (len > 1) ? msg[1] : 0,
                             (len > 2) ? msg[2] : 0,
                             inst->dropped_aftertouch);
        }
        return;
    }

    uint8_t d1 = (len > 1) ? msg[1] : 0;
    uint8_t d2 = (len > 2) ? msg[2] : 0;
    if (expected_len == 2) d2 = 0;

    if (source == MOVE_MIDI_SOURCE_EXTERNAL && is_recent_self_echo(inst, status, d1, d2)) {
        inst->dropped_self_echo++;
        if (should_log_sample(inst->dropped_self_echo, 0x3Fu)) {
            midi_inject_logf("drop reason=self-echo status=0x%02X d1=%u d2=%u dropped_self_echo=%u",
                             status, d1, d2, inst->dropped_self_echo);
        }
        return;
    }

    send_forwarded_message(inst, cin, status, d1, d2);
}

static plugin_api_v2_t g_api = {
    .api_version = 2,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .on_midi = on_midi,
    .set_param = set_param,
    .get_param = get_param,
    .render_block = render_block,
};

plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    (void)g_host;
    midi_inject_log_open();
    midi_inject_logf("plugin initialized (v2, direct midi pass-through, aftertouch-filtered)");
    return &g_api;
}
