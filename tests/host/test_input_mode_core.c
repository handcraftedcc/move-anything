#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/input_mode.h"

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_packet(const schwung_input_mode_result_t *result,
                          int index,
                          uint8_t b0,
                          uint8_t b1,
                          uint8_t b2,
                          uint8_t b3,
                          const char *label) {
    if (index >= result->count) fail(label);
    const uint8_t *pkt = result->packets[index];
    if (pkt[0] != b0 || pkt[1] != b1 || pkt[2] != b2 || pkt[3] != b3) {
        fprintf(stderr,
                "FAIL: %s: got %02x %02x %02x %02x, expected %02x %02x %02x %02x\n",
                label, pkt[0], pkt[1], pkt[2], pkt[3], b0, b1, b2, b3);
        exit(1);
    }
}

static void expect_light(const schwung_input_mode_result_t *result,
                         int index,
                         uint8_t b0,
                         uint8_t b1,
                         uint8_t b2,
                         uint8_t b3,
                         const char *label) {
    if (index >= result->light_count) fail(label);
    const uint8_t *pkt = result->light_packets[index];
    if (pkt[0] != b0 || pkt[1] != b1 || pkt[2] != b2 || pkt[3] != b3) {
        fprintf(stderr,
                "FAIL: %s: got light %02x %02x %02x %02x, expected %02x %02x %02x %02x\n",
                label, pkt[0], pkt[1], pkt[2], pkt[3], b0, b1, b2, b3);
        exit(1);
    }
}

static void expect_led_class(const int colors[SCHWUNG_INPUT_MODE_PADS],
                             const uint8_t held[SCHWUNG_INPUT_MODE_PADS],
                             schwung_input_view_class_t expected,
                             const char *label) {
    schwung_input_view_class_t actual = schwung_input_mode_classify_led_grid(colors, held);
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: got class %d expected %d\n", label, actual, expected);
        exit(1);
    }
}

static void expect_led_mode(const int colors[SCHWUNG_INPUT_MODE_PADS],
                            const uint8_t held[SCHWUNG_INPUT_MODE_PADS],
                            schwung_input_led_grid_mode_t expected,
                            const char *label) {
    schwung_input_led_grid_mode_t actual = schwung_input_mode_detect_led_grid_mode(colors, held);
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: got mode %d expected %d\n", label, actual, expected);
        exit(1);
    }
}

static void test_led_grid_classifier(void) {
    int colors[SCHWUNG_INPUT_MODE_PADS];
    uint8_t held[SCHWUNG_INPUT_MODE_PADS];

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = (i % 7 == 0) ? 17 : 123;
        held[i] = 0;
    }
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_PLAY, "two-color note grid is playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_NOTE, "two-color note grid is note mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = (i % 2 == 0) ? 2 : ((i % 7 == 0) ? 17 : 123);
        held[i] = 0;
    }
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_PLAY, "chromatic grid can include off pads");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_NOTE, "chromatic grid with off pads is note mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = (i % 7 == 0) ? 11 : 123;
        held[i] = 0;
    }
    colors[0] = 122;
    colors[10] = 122;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_PLAY, "note grid can include sparse neutral accents");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_NOTE,
                    "brightness-adjacent neutral accents stay note mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        int row = i / 8;
        int col = i % 8;
        colors[i] = col < 4 ? (row < 2 ? 10 : 20) : 0;
        held[i] = 0;
    }
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_PLAY, "drum left-half grid is playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_NOTE, "drum left-half grid is note mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        int col = i % 8;
        colors[i] = col < 4 ? 68 : 0;
        held[i] = 0;
    }
    colors[0] = 127;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_PLAY,
                     "drum left 4x4 with one pressed-color pad is playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_NOTE,
                    "drum left 4x4 with one pressed-color pad is note mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        int col = i % 8;
        colors[i] = col < 4 ? 68 : 0;
        held[i] = 0;
    }
    colors[0] = 127;
    held[0] = 1;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_PLAY,
                     "drum left 4x4 ignores held pressed pad");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_NOTE,
                    "drum left 4x4 ignores held pressed pad for note mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = i < 16 ? 68 : 0;
        held[i] = 0;
    }
    colors[0] = 127;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_PLAY,
                     "drum contiguous 16-pad bank with one pressed-color pad is playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_NOTE,
                    "drum contiguous 16-pad bank with one pressed-color pad is note mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        int row = i / 8;
        int col = i % 8;
        colors[i] = col >= 4 ? (row < 2 ? 10 : 20) : 0;
        held[i] = 0;
    }
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_NON_PLAY, "right-half-only grid is not playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SESSION, "right-half-only grid is session mode");

    int captured_drumish[] = {
        122, 2, 2, 2, 68, 123, 123, 123,
        2, 2, 2, 2, 123, 123, 123, 68,
        2, 2, 2, 2, 68, 123, 123, 123,
        2, 2, 2, 2, 123, 123, 123, 68
    };
    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = captured_drumish[i];
        held[i] = 0;
    }
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_NON_PLAY,
                     "track grid with grey slots in every row is not playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SESSION,
                    "row-uniform grid with grey slots is session mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = 0;
        held[i] = 0;
    }
    colors[0] = 68;
    colors[1] = 68;
    colors[8] = 68;
    colors[9] = 68;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_NON_PLAY,
                     "sparse track selection grid is not playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SESSION,
                    "sparse grey grid is session mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = 2;
        held[i] = 0;
    }
    colors[0] = 68;
    colors[8] = 68;
    colors[16] = 68;
    colors[24] = 68;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_NON_PLAY,
                     "dim-off sparse track selection grid is not playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SESSION,
                    "one sparse grey pad per row is session mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = 0;
        held[i] = 0;
    }
    colors[3] = 68;
    colors[13] = 69;
    colors[18] = 68;
    colors[31] = 69;
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SESSION,
                    "sparse grey session pads can be in arbitrary columns");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = 0;
        held[i] = 0;
    }
    colors[0] = 21;
    colors[1] = 22;
    colors[8] = 23;
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SET,
                    "fewer than four painted pads is set mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = 2;
        held[i] = 0;
    }
    colors[0] = 68;
    colors[1] = 68;
    colors[8] = 21;
    colors[9] = 21;
    colors[16] = 22;
    colors[24] = 23;
    colors[25] = 23;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_NON_PLAY,
                     "under-eight lit track grid is not playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SESSION,
                    "under-ten mixed sparse grid with grey pair is session mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = 2;
        held[i] = 0;
    }
    for (int col = 0; col < 5; col++) colors[col] = 31;
    for (int col = 0; col < 4; col++) colors[8 + col] = 32;
    for (int col = 0; col < 3; col++) colors[16 + col] = 33;
    colors[24] = 68;
    colors[25] = 34;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_NON_PLAY,
                     "track rows with unshared colors are not playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SESSION,
                    "row-uniform unshared colors are session mode");

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 8; col++) {
            colors[row * 8 + col] = 20 + row * 4;
            held[row * 8 + col] = 0;
        }
    }
    colors[3] = 0; /* blinking/disabled pad in an otherwise row-uniform track view */
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_NON_PLAY, "row-uniform track grid is not playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SESSION, "row-uniform track grid is session mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = (i % 5 == 0) ? 0 : (30 + i);
        held[i] = 0;
    }
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_NON_PLAY, "many-color set grid is not playable");
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_SET, "many-color grid is set mode");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = (i % 7 == 0) ? 17 : 123;
        held[i] = 0;
    }
    colors[4] = 122;
    colors[11] = 121;
    expect_led_mode(colors, held, SCHWUNG_INPUT_LED_GRID_NOTE,
                    "brightness variants normalize before note detection");

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        colors[i] = (i % 7 == 0) ? 12 : 5;
        held[i] = 0;
    }
    held[2] = 1;
    held[9] = 1;
    colors[2] = 99;
    colors[9] = 100;
    expect_led_class(colors, held, SCHWUNG_INPUT_VIEW_PLAY, "held pad colors are ignored");
}

static void test_custom_led_merge(void) {
    int raw[SCHWUNG_INPUT_MODE_PADS];
    int native[SCHWUNG_INPUT_MODE_PADS];
    int custom[SCHWUNG_INPUT_MODE_PADS];
    int out[SCHWUNG_INPUT_MODE_PADS];
    uint8_t native_valid[SCHWUNG_INPUT_MODE_PADS];
    uint8_t custom_valid[SCHWUNG_INPUT_MODE_PADS];

    for (int i = 0; i < SCHWUNG_INPUT_MODE_PADS; i++) {
        raw[i] = 20;
        native[i] = 0;
        custom[i] = 0;
        out[i] = 0;
        native_valid[i] = 0;
        custom_valid[i] = 0;
    }

    schwung_input_mode_merge_led_grid(raw, native, native_valid, custom, custom_valid, out);
    if (out[0] != 20 || native[0] != 20 || native_valid[0] != 1) {
        fail("merge should capture initial native LED state");
    }

    custom[0] = 122;
    custom_valid[0] = 1;
    raw[0] = 122;
    schwung_input_mode_merge_led_grid(raw, native, native_valid, custom, custom_valid, out);
    if (out[0] != 20 || native[0] != 20) {
        fail("merge should ignore our own custom LED echo");
    }

    raw[0] = 68;
    schwung_input_mode_merge_led_grid(raw, native, native_valid, custom, custom_valid, out);
    if (out[0] != 68 || native[0] != 68) {
        fail("merge should accept native LED changes that differ from custom writes");
    }
}

int main(int argc, char **argv) {
    schwung_input_mode_state_t state;
    schwung_input_mode_result_t result;
    schwung_input_mode_config_t config;

    test_led_grid_classifier();
    test_custom_led_merge();

    schwung_input_mode_init(&state);
    if (argc > 1) {
        schwung_input_mode_set_modules_root(&state, argv[1]);
    }

    memset(&result, 0, sizeof(result));
    if (schwung_input_mode_handle_midi(&state, 0, 0x09, 0x90, 68, 100, &result)) {
        fail("native mode should not block pad note-on");
    }
    if (result.count != 0) fail("native mode should not emit MIDI");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 1, SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC, &result);
    schwung_input_mode_set_track_module(&state, 1, "chromatic", &result);
    if (result.count != 0) fail("enabling empty track should not emit panic notes");

    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 1, 0x09, 0x91, 68, 100, &result)) {
        fail("custom mode should block pad note-on");
    }
    if (result.count != 1) fail("true chromatic note-on should emit one packet");
    expect_packet(&result, 0, 0x29, 0x91, 48, 100, "track 2 pad 68 maps to C2 on channel 2");

    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 1, 0x08, 0x81, 68, 0, &result)) {
        fail("custom mode should block pad note-off");
    }
    if (result.count != 1) fail("true chromatic note-off should emit one packet");
    expect_packet(&result, 0, 0x28, 0x81, 48, 0, "track 2 pad 68 releases held C2");

    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 1, 0x09, 0x91, 69, 70, &result)) {
        fail("custom mode should block second pad note-on");
    }
    expect_packet(&result, 0, 0x29, 0x91, 49, 70, "pad 69 maps to C#2");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 1, SCHWUNG_INPUT_MODE_NATIVE, &result);
    if (result.count != 1) fail("switching to native should panic held notes");
    expect_packet(&result, 0, 0x28, 0x81, 49, 0, "mode switch panics held C#2");

    memset(&result, 0, sizeof(result));
    if (schwung_input_mode_handle_midi(&state, 1, 0x08, 0x81, 69, 0, &result)) {
        fail("native mode after panic should pass release through");
    }
    if (result.count != 0) fail("native release after panic should emit nothing");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 3, SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC, &result);
    schwung_input_mode_set_track_module(&state, 3, "chromatic", &result);
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 3, 0x09, 0x93, 99, 127, &result)) {
        fail("custom mode should block top pad note-on");
    }
    expect_packet(&result, 0, 0x29, 0x93, 79, 127, "track 4 pad 99 maps to G4 on channel 4");

    memset(&result, 0, sizeof(result));
    if (schwung_input_mode_handle_midi(&state, 9, 0x09, 0x99, 68, 100, &result)) {
        fail("invalid track must fail open to native");
    }
    if (result.count != 0) fail("invalid track should not emit MIDI");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 0, SCHWUNG_INPUT_MODE_DRUM32, &result);
    schwung_input_mode_set_track_module(&state, 0, "drum32", &result);
    if (result.light_count != 32) fail("drum32 module should emit custom LED state on selection");
    config = schwung_input_mode_default_config();
    config.root_octave = 1;
    schwung_input_mode_set_track_config(&state, 0, &config, &result);
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 0, 0x09, 0x90, 68, 96, &result)) {
        fail("drum32 should block pad note-on");
    }
    if (result.count != 1) fail("drum32 should emit one note");
    expect_packet(&result, 0, 0x29, 0x90, 48, 96, "drum32 root octave offsets pad 0 to note 48");

    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 0, 0x08, 0x80, 68, 0, &result)) {
        fail("drum32 should block pad note-off");
    }
    expect_packet(&result, 0, 0x28, 0x80, 48, 0, "drum32 releases octave-offset note 48");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 1, SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC, &result);
    schwung_input_mode_set_track_module(&state, 1, "chromatic", &result);
    config = schwung_input_mode_default_config();
    config.root = 2;
    config.octave = 1;
    config.scale = SCHWUNG_INPUT_SCALE_MAJOR;
    schwung_input_mode_set_track_config(&state, 1, &config, &result);
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 1, 0x09, 0x91, 68, 64, &result)) {
        fail("chromatic should block root-shifted pad note-on");
    }
    expect_packet(&result, 0, 0x29, 0x91, 62, 64, "chromatic root and octave configure base note");
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_button(&state, 1, 0x0B, 0xB1, 55, 127, &result)) {
        fail("plus button should be intercepted in chromatic mode");
    }
    if (result.param_update_count != 1) fail("plus button should report one param update");
    if (strcmp(result.param_updates[0].key, "octave") != 0 || strcmp(result.param_updates[0].value, "2") != 0) {
        fail("plus button should report updated chromatic octave param");
    }
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 1, 0x09, 0x91, 69, 80, &result)) {
        fail("chromatic should still map after octave button");
    }
    expect_packet(&result, 0, 0x29, 0x91, 75, 80, "plus button increments chromatic octave");
    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 1, SCHWUNG_INPUT_MODE_NATIVE, &result);

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 2, SCHWUNG_INPUT_MODE_CHORD_PADS, &result);
    schwung_input_mode_set_track_module(&state, 2, "chord-pads", &result);
    config = schwung_input_mode_default_config();
    config.root = 0;
    config.scale = SCHWUNG_INPUT_SCALE_MAJOR;
    config.index_2 = 2;
    config.index_3 = 4;
    schwung_input_mode_set_track_config(&state, 2, &config, &result);
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 2, 0x09, 0x92, 68, 110, &result)) {
        fail("chord pads should block pad note-on");
    }
    if (result.count != 3) fail("chord pads should emit three note-ons");
    expect_packet(&result, 0, 0x29, 0x92, 60, 110, "chord pad 0 emits root");
    expect_packet(&result, 1, 0x29, 0x92, 64, 110, "chord pad 0 emits third");
    expect_packet(&result, 2, 0x29, 0x92, 67, 110, "chord pad 0 emits fifth");
    if (result.light_count != 32) fail("chord pad note-on should redraw custom LEDs");
    expect_light(&result, 0, 0x09, 0x90, 68, 127, "pressed chord root pad should be highlighted");
    expect_light(&result, 1, 0x09, 0x90, 69, 36, "non-root chord pad should use normal color");
    expect_light(&result, 7, 0x09, 0x90, 75, 122, "next root chord pad should use root color");

    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 2, 0x08, 0x82, 68, 0, &result)) {
        fail("chord pads should block pad note-off");
    }
    if (result.count != 3) fail("chord pad note-off should release three notes");
    if (result.light_count != 32) fail("chord pad note-off should redraw custom LEDs");
    expect_light(&result, 0, 0x09, 0x90, 68, 122, "released chord root pad should restore root color");

    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 2, 0x09, 0x92, 68, 110, &result)) {
        fail("chord pads should block pad note-on before octave test");
    }

    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_button(&state, 2, 0x0B, 0xB2, 54, 127, &result)) {
        fail("minus button should be intercepted in chord mode");
    }
    if (result.param_update_count != 1) fail("minus button should report one param update");
    if (strcmp(result.param_updates[0].key, "octave") != 0 || strcmp(result.param_updates[0].value, "-1") != 0) {
        fail("minus button should report updated chord octave param");
    }
    if (result.count != 3) fail("octave button should panic held chord notes before shifting");
    if (result.light_count != 32) fail("octave button should redraw custom LEDs after shifting");
    expect_light(&result, 0, 0x09, 0x90, 68, 122, "octave panic should clear pressed chord pad highlight");
    expect_packet(&result, 0, 0x28, 0x82, 60, 0, "chord octave shift panics root");
    expect_packet(&result, 1, 0x28, 0x82, 64, 0, "chord octave shift panics third");
    expect_packet(&result, 2, 0x28, 0x82, 67, 0, "chord octave shift panics fifth");
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 2, 0x09, 0x92, 68, 110, &result)) {
        fail("chord pads should block after octave decrement");
    }
    expect_packet(&result, 0, 0x29, 0x92, 48, 110, "minus button decrements chord octave");
    expect_packet(&result, 1, 0x29, 0x92, 52, 110, "minus chord third follows configured scale degree");
    expect_packet(&result, 2, 0x29, 0x92, 55, 110, "minus chord fifth follows configured scale degree");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 2, SCHWUNG_INPUT_MODE_NATIVE, &result);
    if (result.count != 3) fail("switching chord pads to native should panic all held notes");
    expect_packet(&result, 0, 0x28, 0x82, 48, 0, "chord panic root");
    expect_packet(&result, 1, 0x28, 0x82, 52, 0, "chord panic third");
    expect_packet(&result, 2, 0x28, 0x82, 55, 0, "chord panic fifth");

    printf("PASS: input mode core mapping and panic behavior\n");
    return 0;
}
