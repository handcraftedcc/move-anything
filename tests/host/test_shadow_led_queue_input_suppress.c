#include "host/shadow_led_queue.h"
#include "host/unified_log.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void unified_log_init(void) {}
void unified_log_shutdown(void) {}
int unified_log_enabled(void) { return 0; }
void unified_log(const char *source, int level, const char *fmt, ...) {
    (void)source;
    (void)level;
    (void)fmt;
}
void unified_log_v(const char *source, int level, const char *fmt, va_list args) {
    (void)source;
    (void)level;
    (void)fmt;
    (void)args;
}
void unified_log_crash(const char *msg) { (void)msg; }

static int suppress_called = 0;

static int suppress_pad_68(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2) {
    suppress_called++;
    (void)data2;
    return data1 == 68 && ((cin == 0x09 && status == 0x90) ||
                           (cin == 0x08 && status == 0x80));
}

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
}

int main(void) {
    uint8_t midi_out[HW_MIDI_OUT_SIZE];
    uint8_t ui_midi[HW_MIDI_OUT_SIZE];
    shadow_control_t control;
    shadow_control_t *control_ptr = &control;
    uint8_t *ui_ptr = ui_midi;

    memset(midi_out, 0, sizeof(midi_out));
    memset(ui_midi, 0, sizeof(ui_midi));
    memset(&control, 0, sizeof(control));

    led_queue_host_t host = {
        .midi_out_buf = midi_out,
        .shadow_control = &control_ptr,
        .shadow_ui_midi_shm = &ui_ptr,
        .passthrough_ccs = NULL,
        .suppress_move_led = suppress_pad_68,
    };
    led_queue_init(&host);

    midi_out[0] = 0x09;
    midi_out[1] = 0x90;
    midi_out[2] = 68;
    midi_out[3] = 122;

    shadow_clear_move_leds_if_overtake();

    if (suppress_called != 1) {
        fail("Move LED suppress callback should be called for pad LED packets");
        return 1;
    }
    if (midi_out[0] || midi_out[1] || midi_out[2] || midi_out[3]) {
        fail("suppressed Move pad LED packet should be removed from hardware output");
        return 1;
    }
    if (led_queue_get_note_led_color(68) != 122) {
        fail("suppressed Move pad LED packet should still update the native LED cache");
        return 1;
    }
    if (led_queue_get_pad_led_generation() != 1) {
        fail("suppressed Move pad LED packet should advance pad LED generation");
        return 1;
    }

    midi_out[0] = 0x09;
    midi_out[1] = 0x90;
    midi_out[2] = 68;
    midi_out[3] = 0;

    shadow_clear_move_leds_if_overtake();

    if (midi_out[0] || midi_out[1] || midi_out[2] || midi_out[3]) {
        fail("suppressed Move pad LED off packet should be removed from hardware output");
        return 1;
    }

    midi_out[0] = 0x08;
    midi_out[1] = 0x80;
    midi_out[2] = 68;
    midi_out[3] = 0;

    shadow_clear_move_leds_if_overtake();

    if (midi_out[0] || midi_out[1] || midi_out[2] || midi_out[3]) {
        fail("suppressed Move pad LED note-off packet should be removed from hardware output");
        return 1;
    }

    midi_out[0] = 0x09;
    midi_out[1] = 0x90;
    midi_out[2] = 68;
    midi_out[3] = 122;

    shadow_clear_move_leds_if_overtake();

    if (led_queue_get_note_led_color(68) != 122) {
        fail("latest suppressed Move pad LED should be cached for native handoff");
        return 1;
    }

    shadow_queue_led(0x09, 0x90, 68, 36);
    shadow_queue_led(0x09, 0x90, 69, 36);
    led_queue_clear_pending_pad_leds();
    shadow_flush_pending_leds();

    for (int i = 0; i < HW_MIDI_OUT_SIZE; i += 4) {
        if ((midi_out[i+1] & 0xF0) == 0x90 && midi_out[i+2] >= 68 && midi_out[i+2] <= 99) {
            fail("cleared pending pad LEDs should not flush to hardware");
            return 1;
        }
    }

    led_queue_queue_native_pad_leds();
    midi_out[0] = 0x09;
    midi_out[1] = 0x90;
    midi_out[2] = 69;
    midi_out[3] = 55;
    shadow_flush_pending_leds();

    int saw_pad_68_native = 0;
    int saw_pad_69_native = 0;
    int saw_pad_69_off = 0;
    for (int i = 0; i < HW_MIDI_OUT_SIZE; i += 4) {
        uint8_t type = midi_out[i+1] & 0xF0;
        if (type != 0x90) continue;
        if (midi_out[i+2] == 68 && midi_out[i+3] == 122) saw_pad_68_native = 1;
        if (midi_out[i+2] == 69 && midi_out[i+3] == 55) saw_pad_69_native = 1;
        if (midi_out[i+2] == 69 && midi_out[i+3] == 0) saw_pad_69_off = 1;
    }
    if (!saw_pad_68_native) {
        fail("handoff restore should replay the cached native pad LED state");
        return 1;
    }
    if (!saw_pad_69_native || saw_pad_69_off) {
        fail("handoff clear should yield to native pad LED repaint packets");
        return 1;
    }

    return 0;
}
