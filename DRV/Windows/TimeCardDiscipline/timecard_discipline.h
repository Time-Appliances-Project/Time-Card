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
