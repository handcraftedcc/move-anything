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

int main(void) {
    schwung_input_mode_state_t state;
    schwung_input_mode_result_t result;

    test_led_grid_classifier();

    schwung_input_mode_init(&state);

    memset(&result, 0, sizeof(result));
    if (schwung_input_mode_handle_midi(&state, 0, 0x09, 0x90, 68, 100, &result)) {
        fail("native mode should not block pad note-on");
    }
    if (result.count != 0) fail("native mode should not emit MIDI");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 1, SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC, &result);
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
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 0, 0x09, 0x90, 68, 96, &result)) {
        fail("drum32 should block pad note-on");
    }
    if (result.count != 1) fail("drum32 should emit one note");
    expect_packet(&result, 0, 0x29, 0x90, 36, 96, "drum32 pad 0 maps to kick note 36");

    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 0, 0x08, 0x80, 68, 0, &result)) {
        fail("drum32 should block pad note-off");
    }
    expect_packet(&result, 0, 0x28, 0x80, 36, 0, "drum32 releases note 36");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 2, SCHWUNG_INPUT_MODE_CHORD_PADS, &result);
    memset(&result, 0, sizeof(result));
    if (!schwung_input_mode_handle_midi(&state, 2, 0x09, 0x92, 68, 110, &result)) {
        fail("chord pads should block pad note-on");
    }
    if (result.count != 3) fail("chord pads should emit three note-ons");
    expect_packet(&result, 0, 0x29, 0x92, 60, 110, "chord pad 0 emits root");
    expect_packet(&result, 1, 0x29, 0x92, 64, 110, "chord pad 0 emits third");
    expect_packet(&result, 2, 0x29, 0x92, 67, 110, "chord pad 0 emits fifth");

    memset(&result, 0, sizeof(result));
    schwung_input_mode_set_track_mode(&state, 2, SCHWUNG_INPUT_MODE_NATIVE, &result);
    if (result.count != 3) fail("switching chord pads to native should panic all held notes");
    expect_packet(&result, 0, 0x28, 0x82, 60, 0, "chord panic root");
    expect_packet(&result, 1, 0x28, 0x82, 64, 0, "chord panic third");
    expect_packet(&result, 2, 0x28, 0x82, 67, 0, "chord panic fifth");

    printf("PASS: input mode core mapping and panic behavior\n");
    return 0;
}
