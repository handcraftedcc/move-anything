#ifndef SCHWUNG_INPUT_MODE_H
#define SCHWUNG_INPUT_MODE_H

#include <stdint.h>

#define SCHWUNG_INPUT_MODE_TRACKS 4
#define SCHWUNG_INPUT_MODE_PADS 32
#define SCHWUNG_INPUT_MODE_MAX_PACKET_OUT 256

typedef enum {
    SCHWUNG_INPUT_MODE_NATIVE = 0,
    SCHWUNG_INPUT_MODE_TRUE_CHROMATIC = 1,
    SCHWUNG_INPUT_MODE_DRUM32 = 2,
    SCHWUNG_INPUT_MODE_CHORD_PADS = 3
} schwung_input_mode_t;

/* Alias old POC name */
#define SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC SCHWUNG_INPUT_MODE_TRUE_CHROMATIC

typedef enum {
    SCHWUNG_INPUT_LED_PASS_THROUGH = 0,
    SCHWUNG_INPUT_LED_MODULE = 1
} schwung_input_led_mode_t;

typedef struct {
    uint8_t active;
    uint8_t count;
    uint8_t notes[8];
    uint8_t channel;
} schwung_input_mode_held_pad_t;

typedef struct {
    schwung_input_mode_t mode;
    schwung_input_led_mode_t led_mode;
} schwung_input_mode_track_config_t;

typedef struct {
    schwung_input_mode_track_config_t tracks[SCHWUNG_INPUT_MODE_TRACKS];
    schwung_input_mode_held_pad_t held[SCHWUNG_INPUT_MODE_TRACKS][SCHWUNG_INPUT_MODE_PADS];
} schwung_input_mode_state_t;

/* Per-track params passed from JS via SHM.
 * Index mapping (defined by module.json params array order):
 *   0 = root (MIDI note, e.g. 60 = C3)
 *   1 = scale (enum index into scale table)
 *   2 = index_2 (chord interval index, default 2 = 3rd)
 *   3 = index_3 (chord interval index, default 4 = 5th)
 *   4-7 = reserved
 * octave is a performance shift applied on top of root (+/-). */
typedef struct {
    int16_t root;        /* 0 */
    int16_t scale;       /* 1 */
    int16_t index_2;     /* 2 */
    int16_t index_3;     /* 3 */
    int16_t _unused[4];  /* 4-7 */
    int8_t  octave;
} schwung_input_mode_params_t;

typedef struct {
    uint16_t count;
    uint8_t packets[SCHWUNG_INPUT_MODE_MAX_PACKET_OUT][4];
} schwung_input_mode_result_t;

typedef enum {
    SCHWUNG_INPUT_VIEW_UNKNOWN = 0,
    SCHWUNG_INPUT_VIEW_PLAY = 1,
    SCHWUNG_INPUT_VIEW_NON_PLAY = 2
} schwung_input_view_class_t;

typedef enum {
    SCHWUNG_INPUT_LED_GRID_UNKNOWN_NON_NOTE = 0,
    SCHWUNG_INPUT_LED_GRID_NOTE = 1,
    SCHWUNG_INPUT_LED_GRID_SESSION = 2,
    SCHWUNG_INPUT_LED_GRID_SET = 3
} schwung_input_led_grid_mode_t;

void schwung_input_mode_init(schwung_input_mode_state_t *state);
void schwung_input_mode_result_clear(schwung_input_mode_result_t *result);
schwung_input_led_grid_mode_t schwung_input_mode_detect_led_grid_mode(const int pad_colors[SCHWUNG_INPUT_MODE_PADS],
                                                                      const uint8_t held_pads[SCHWUNG_INPUT_MODE_PADS]);
schwung_input_view_class_t schwung_input_mode_classify_led_grid(const int pad_colors[SCHWUNG_INPUT_MODE_PADS],
                                                                const uint8_t held_pads[SCHWUNG_INPUT_MODE_PADS]);
int schwung_input_mode_set_track_mode(schwung_input_mode_state_t *state,
                                      int track,
                                      schwung_input_mode_t mode,
                                      schwung_input_mode_result_t *result);
int schwung_input_mode_panic_track(schwung_input_mode_state_t *state,
                                   int track,
                                   schwung_input_mode_result_t *result);
int schwung_input_mode_handle_midi(schwung_input_mode_state_t *state,
                                   int active_track,
                                   uint8_t cin,
                                   uint8_t status,
                                   uint8_t data1,
                                   uint8_t data2,
                                   const int16_t *params,
                                   int8_t octave,
                                   schwung_input_mode_result_t *result);

/* Render pad LED colors for the given input mode.
 * Fills pad_colors_out[32] with color values (0 = off).
 * native_colors is the current Move pad LED state for reference.
 * Returns number of pads lit. */
int schwung_input_mode_render_leds(schwung_input_mode_t mode,
                                   const int16_t *params,
                                   int8_t octave,
                                   const uint8_t native_colors[SCHWUNG_INPUT_MODE_PADS],
                                   uint8_t pad_colors_out[SCHWUNG_INPUT_MODE_PADS]);

/* Get the number of scale definitions available */
int schwung_input_mode_scale_count(void);

/* Get scale name by index */
const char *schwung_input_mode_scale_name(int index);

/* Check if a MIDI note (0-127) belongs to a scale with given root */
int schwung_input_mode_note_in_scale(int note, int root, int scale_index);

#endif
