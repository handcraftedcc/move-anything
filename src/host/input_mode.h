#ifndef SCHWUNG_INPUT_MODE_H
#define SCHWUNG_INPUT_MODE_H

#include <stdint.h>

#include "input_mode_api_v1.h"

#define SCHWUNG_INPUT_MODE_TRACKS 4
#define SCHWUNG_INPUT_MODE_PADS 32
#define SCHWUNG_INPUT_MODE_MAX_PACKET_OUT 256
#define SCHWUNG_INPUT_MODE_MODULE_ID_LEN 64
#define SCHWUNG_INPUT_MODE_MODULE_ROOT_LEN 256
#define SCHWUNG_INPUT_MODE_PARAM_COUNT 16
#define SCHWUNG_INPUT_MODE_PARAM_KEY_LEN 32
#define SCHWUNG_INPUT_MODE_PARAM_VALUE_LEN 64

typedef enum {
    SCHWUNG_INPUT_MODE_NATIVE = 0,
    SCHWUNG_INPUT_MODE_TRUE_CHROMATIC_POC = 1,
    SCHWUNG_INPUT_MODE_DRUM32 = 2,
    SCHWUNG_INPUT_MODE_CHORD_PADS = 3
} schwung_input_mode_t;

typedef enum {
    SCHWUNG_INPUT_LED_PASS_THROUGH = 0,
    SCHWUNG_INPUT_LED_MODULE = 1
} schwung_input_led_mode_t;

typedef enum {
    SCHWUNG_INPUT_SCALE_MAJOR = 0,
    SCHWUNG_INPUT_SCALE_MINOR = 1,
    SCHWUNG_INPUT_SCALE_CHROMATIC = 2
} schwung_input_scale_t;

typedef struct {
    uint8_t root;        /* 0-11 */
    int8_t octave;       /* melodic layout octave offset */
    int8_t root_octave;  /* drum layout base octave offset */
    uint8_t scale;       /* schwung_input_scale_t */
    uint8_t index_2;     /* zero-based scale degree for second chord note */
    uint8_t index_3;     /* zero-based scale degree for third chord note */
} schwung_input_mode_config_t;

typedef struct {
    uint8_t active;
    uint8_t count;
    uint8_t notes[8];
    uint8_t channel;
} schwung_input_mode_held_pad_t;

typedef struct {
    void *handle;
    const schwung_input_module_api_v1_t *api;
    const schwung_input_module_api_v2_t *api_v2;
    void *instance;
    char module_id[SCHWUNG_INPUT_MODE_MODULE_ID_LEN];
    char module_dir[SCHWUNG_INPUT_MODE_MODULE_ROOT_LEN];
} schwung_input_mode_loaded_module_t;

typedef struct {
    schwung_input_mode_t mode;
    schwung_input_led_mode_t led_mode;
    schwung_input_mode_config_t config;
    schwung_input_mode_loaded_module_t module;
    char param_keys[SCHWUNG_INPUT_MODE_PARAM_COUNT][SCHWUNG_INPUT_MODE_PARAM_KEY_LEN];
    char param_values[SCHWUNG_INPUT_MODE_PARAM_COUNT][SCHWUNG_INPUT_MODE_PARAM_VALUE_LEN];
} schwung_input_mode_track_config_t;

typedef struct {
    char modules_root[SCHWUNG_INPUT_MODE_MODULE_ROOT_LEN];
    schwung_input_mode_track_config_t tracks[SCHWUNG_INPUT_MODE_TRACKS];
    schwung_input_mode_held_pad_t held[SCHWUNG_INPUT_MODE_TRACKS][SCHWUNG_INPUT_MODE_PADS];
} schwung_input_mode_state_t;

typedef schwung_input_module_result_t schwung_input_mode_result_t;

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
schwung_input_mode_config_t schwung_input_mode_default_config(void);
void schwung_input_mode_set_modules_root(schwung_input_mode_state_t *state,
                                         const char *modules_root);
schwung_input_led_grid_mode_t schwung_input_mode_detect_led_grid_mode(const int pad_colors[SCHWUNG_INPUT_MODE_PADS],
                                                                      const uint8_t held_pads[SCHWUNG_INPUT_MODE_PADS]);
schwung_input_view_class_t schwung_input_mode_classify_led_grid(const int pad_colors[SCHWUNG_INPUT_MODE_PADS],
                                                                const uint8_t held_pads[SCHWUNG_INPUT_MODE_PADS]);
int schwung_input_mode_set_track_mode(schwung_input_mode_state_t *state,
                                      int track,
                                      schwung_input_mode_t mode,
                                      schwung_input_mode_result_t *result);
int schwung_input_mode_set_track_module(schwung_input_mode_state_t *state,
                                        int track,
                                        const char *module_id,
                                        schwung_input_mode_result_t *result);
int schwung_input_mode_set_track_param(schwung_input_mode_state_t *state,
                                       int track,
                                       const char *key,
                                       const char *value,
                                       schwung_input_mode_result_t *result);
int schwung_input_mode_update_leds(schwung_input_mode_state_t *state,
                                   int track,
                                   schwung_input_mode_result_t *result);
void schwung_input_mode_merge_led_grid(const int raw_colors[SCHWUNG_INPUT_MODE_PADS],
                                       int native_colors[SCHWUNG_INPUT_MODE_PADS],
                                       uint8_t native_valid[SCHWUNG_INPUT_MODE_PADS],
                                       const int custom_colors[SCHWUNG_INPUT_MODE_PADS],
                                       const uint8_t custom_valid[SCHWUNG_INPUT_MODE_PADS],
                                       int out_colors[SCHWUNG_INPUT_MODE_PADS]);
int schwung_input_mode_set_track_config(schwung_input_mode_state_t *state,
                                        int track,
                                        const schwung_input_mode_config_t *config,
                                        schwung_input_mode_result_t *result);
int schwung_input_mode_panic_track(schwung_input_mode_state_t *state,
                                   int track,
                                   schwung_input_mode_result_t *result);
int schwung_input_mode_handle_button(schwung_input_mode_state_t *state,
                                     int active_track,
                                     uint8_t cin,
                                     uint8_t status,
                                     uint8_t data1,
                                     uint8_t data2,
                                     schwung_input_mode_result_t *result);
int schwung_input_mode_handle_midi(schwung_input_mode_state_t *state,
                                   int active_track,
                                   uint8_t cin,
                                   uint8_t status,
                                   uint8_t data1,
                                   uint8_t data2,
                                   schwung_input_mode_result_t *result);
int schwung_input_mode_tick(schwung_input_mode_state_t *state,
                            int active_track,
                            int frames,
                            int sample_rate,
                            schwung_input_mode_result_t *result);

#endif
