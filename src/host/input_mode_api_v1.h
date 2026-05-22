#ifndef SCHWUNG_INPUT_MODE_API_V1_H
#define SCHWUNG_INPUT_MODE_API_V1_H

#include <stdint.h>

#define SCHWUNG_INPUT_MODULE_API_VERSION 1
#define SCHWUNG_INPUT_MODULE_API_VERSION_V2 2
#define SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT 256
#define SCHWUNG_INPUT_MODULE_MAX_PARAM_UPDATES 8
#define SCHWUNG_INPUT_MODULE_PARAM_KEY_LEN 32
#define SCHWUNG_INPUT_MODULE_PARAM_VALUE_LEN 64

typedef struct {
    char key[SCHWUNG_INPUT_MODULE_PARAM_KEY_LEN];
    char value[SCHWUNG_INPUT_MODULE_PARAM_VALUE_LEN];
} schwung_input_module_param_update_t;

typedef struct {
    uint16_t count;
    uint8_t packets[SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT][4];
    uint16_t light_count;
    uint8_t light_packets[SCHWUNG_INPUT_MODULE_MAX_PACKET_OUT][4];
    uint16_t param_update_count;
    schwung_input_module_param_update_t param_updates[SCHWUNG_INPUT_MODULE_MAX_PARAM_UPDATES];
} schwung_input_module_result_t;

typedef struct {
    int active_track;
    uint8_t channel;
    uint8_t cin;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} schwung_input_module_event_t;

typedef struct {
    uint8_t valid;
    uint8_t root_note;
    const char *scale;
    const char *melodic_layout;
} schwung_input_module_musical_context_t;

typedef struct schwung_input_module_api_v1 {
    uint32_t api_version;
    void *(*create)(const char *module_dir);
    void (*destroy)(void *instance);
    void (*set_param)(void *instance, const char *key, const char *value);
    int (*process_midi)(void *instance,
                        const schwung_input_module_event_t *event,
                        const schwung_input_module_musical_context_t *musical_context,
                        schwung_input_module_result_t *result);
    int (*process_button)(void *instance,
                          const schwung_input_module_event_t *event,
                          const schwung_input_module_musical_context_t *musical_context,
                          schwung_input_module_result_t *result);
    int (*update_leds)(void *instance,
                       const schwung_input_module_musical_context_t *musical_context,
                       schwung_input_module_result_t *result);
} schwung_input_module_api_v1_t;

typedef struct schwung_input_module_api_v2 {
    uint32_t api_version;
    void *(*create)(const char *module_dir);
    void (*destroy)(void *instance);
    void (*set_param)(void *instance, const char *key, const char *value);
    int (*process_midi)(void *instance,
                        const schwung_input_module_event_t *event,
                        const schwung_input_module_musical_context_t *musical_context,
                        schwung_input_module_result_t *result);
    int (*process_button)(void *instance,
                          const schwung_input_module_event_t *event,
                          const schwung_input_module_musical_context_t *musical_context,
                          schwung_input_module_result_t *result);
    int (*update_leds)(void *instance,
                       const schwung_input_module_musical_context_t *musical_context,
                       schwung_input_module_result_t *result);
    uint32_t struct_size;
    int (*tick)(void *instance,
                int frames,
                int sample_rate,
                const schwung_input_module_musical_context_t *musical_context,
                schwung_input_module_result_t *result);
    int (*panic)(void *instance,
                 const schwung_input_module_musical_context_t *musical_context,
                 schwung_input_module_result_t *result);
} schwung_input_module_api_v2_t;

typedef schwung_input_module_api_v1_t *(*schwung_input_module_init_v1_fn)(void);
typedef schwung_input_module_api_v2_t *(*schwung_input_module_init_v2_fn)(void);

#endif
