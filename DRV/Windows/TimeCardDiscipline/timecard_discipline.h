/* SPDX-License-Identifier: LGPL-2.1-or-later */

#pragma once

#include <stdint.h>

#if defined(_WIN32)
#define TCOD_API __declspec(dllexport)
#else
#define TCOD_API
#endif

#define TCOD_INPUT_VALID       (1u << 0)
#define TCOD_INPUT_LOCKED      (1u << 1)
#define TCOD_INPUT_SURVEY_DONE (1u << 2)
#define TCOD_INPUT_CALIBRATE   (1u << 3)
#define TCOD_INPUT_PHASE_VALID (1u << 4)
#define TCOD_INPUT_REFERENCE_VALID (1u << 5)
#define TCOD_INPUT_OSCILLATOR_VALID (1u << 6)
#define TCOD_INPUT_PHASE_ERROR     (1u << 7)

#define TCOD_CONFIG_CALIBRATE_FIRST       (1u << 0)
#define TCOD_CONFIG_FACTORY_SETTINGS      (1u << 1)
#define TCOD_CONFIG_LEARN_TEMPERATURE     (1u << 2)
#define TCOD_CONFIG_USE_TEMPERATURE       (1u << 3)
#define TCOD_FINE_TABLE_PATH_LENGTH 220u

/* Versioned, fixed-size configuration for stable managed/native interop. */
typedef struct tcod_config {
    uint32_t size;
    int32_t ref_fluctuations_ns;
    int32_t phase_jump_threshold_ns;
    int32_t phase_resolution_ns;
    int32_t debug;
    int32_t reactivity_min;
    int32_t reactivity_max;
    int32_t reactivity_power;
    int32_t nb_calibration;
    int32_t fine_stop_tolerance;
    int32_t max_allowed_coarse;
    uint32_t flags;
    uint32_t reserved[4];
    char fine_table_output_path[TCOD_FINE_TABLE_PATH_LENGTH];
} tcod_config;

typedef struct tcod_input {
    double temperature;
    int64_t phase_error_ns;
    uint32_t fine_setpoint;
    int32_t coarse_setpoint;
    int32_t quantization_error_ps;
    uint32_t flags;
} tcod_input;

typedef struct tcod_output {
    uint32_t action;
    uint32_t setpoint;
    int32_t phase_jump_ns;
    uint32_t state;
    uint32_t clock_class;
    int32_t convergence_count;
    int32_t convergence_threshold;
    float convergence_percent;
    uint32_t ready_for_holdover;
} tcod_output;

TCOD_API void *tcod_create(uint32_t factory_coarse,
                           const void *saved_parameters,
                           uint32_t saved_parameters_length,
                           char *error, uint32_t error_length);
TCOD_API void tcod_default_config(tcod_config *config);
TCOD_API void *tcod_create_configured(uint32_t factory_coarse,
                                      const void *saved_parameters,
                                      uint32_t saved_parameters_length,
                                      const tcod_config *config,
                                      char *error, uint32_t error_length);
TCOD_API int tcod_process(void *context, const tcod_input *input,
                          tcod_output *output);
TCOD_API void tcod_destroy(void *context);
TCOD_API uint32_t tcod_version(void);
TCOD_API int tcod_get_calibration_plan(void *context, uint16_t *points,
                                       uint32_t capacity,
                                       uint32_t *point_count,
                                       uint32_t *samples_per_point);
TCOD_API int tcod_complete_calibration(void *context,
                                       const float *phase_samples,
                                       uint32_t sample_count);
TCOD_API uint32_t tcod_parameters_size(void);
TCOD_API int tcod_get_parameters(void *context, void *buffer,
                                 uint32_t buffer_length);
