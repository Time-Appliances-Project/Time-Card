/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Stable Windows ABI around Orolia's unmodified miniCOD state machine. */

#include "timecard_discipline.h"

#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <oscillator-disciplining/oscillator-disciplining.h>
#include "log.h"

#define TCOD_VERSION 0x00010000u
#define NS_PER_SECOND 1000000000ll
#define TCOD_EEPROM_SIZE 512u
#define TCOD_TEMPERATURE_OFFSET 0x90u

struct tcod_context {
    struct od *algorithm;
    struct calibration_parameters *calibration;
};

void
tcod_default_config(tcod_config *config)
{
    if (config == NULL)
        return;
    memset(config, 0, sizeof(*config));
    config->size = sizeof(*config);
    config->ref_fluctuations_ns = 30;
    config->phase_jump_threshold_ns = 300;
    config->phase_resolution_ns = 5;
    config->reactivity_min = 10;
    config->reactivity_max = 30;
    config->reactivity_power = 2;
    config->nb_calibration = 50;
    config->fine_stop_tolerance = 100;
    config->max_allowed_coarse = 20;
    config->flags = TCOD_CONFIG_FACTORY_SETTINGS;
}

static int
tcod_validate_config(const tcod_config *config, char *error,
                     uint32_t error_length)
{
    const char *message = NULL;
    if (config == NULL || config->size != sizeof(*config))
        message = "unsupported native discipline configuration size";
    else if (config->phase_resolution_ns <= 0 ||
             config->phase_resolution_ns > 1000000)
        message = "phase_resolution_ns must be between 1 and 1000000";
    else if (config->ref_fluctuations_ns < 0 ||
             config->phase_jump_threshold_ns <= 0)
        message = "phase thresholds must be non-negative";
    else if (config->reactivity_min <= 0 ||
             config->reactivity_max < config->reactivity_min ||
             config->reactivity_power <= 0)
        message = "reactivity settings are inconsistent";
    else if (config->nb_calibration <= 0 || config->nb_calibration > 1000)
        message = "nb_calibration must be between 1 and 1000";
    else if (config->fine_stop_tolerance < 0 ||
             config->max_allowed_coarse < 0)
        message = "control tolerances must be non-negative";
    if (message == NULL)
        return 0;
    if (error != NULL && error_length != 0u) {
        snprintf(error, error_length, "%s", message);
        error[error_length - 1u] = '\0';
    }
    return -1;
}

static bool
tcod_saved_parameters_valid(const struct disciplining_parameters *parameters)
{
    const struct disciplining_config *config = &parameters->dsc_config;
    uint32_t index;

    if (config->header != HEADER_MAGIC ||
        config->version != DISCIPLINING_CONFIG_VERSION ||
        parameters->temp_table.header != HEADER_MAGIC ||
        parameters->temp_table.version != DISCIPLINING_CONFIG_VERSION ||
        config->ctrl_nodes_length > CALIBRATION_POINTS_MAX ||
        config->ctrl_nodes_length_factory > 3u ||
        config->coarse_equilibrium_factory < 0 ||
        config->coarse_equilibrium_factory > 0x003fffff ||
        (config->coarse_equilibrium < -1 ||
         config->coarse_equilibrium > 0x003fffff)) {
        return false;
    }
    for (index = 0; index < config->ctrl_nodes_length; ++index) {
        if (!isfinite(config->ctrl_load_nodes[index]) ||
            !isfinite(config->ctrl_drift_coeffs[index]) ||
            config->ctrl_load_nodes[index] < 0.0f ||
            config->ctrl_load_nodes[index] > 1.0f) {
            return false;
        }
    }
    for (index = 0; index < config->ctrl_nodes_length_factory; ++index) {
        if (!isfinite(config->ctrl_load_nodes_factory[index]) ||
            !isfinite(config->ctrl_drift_coeffs_factory[index]) ||
            config->ctrl_load_nodes_factory[index] < 0.0f ||
            config->ctrl_load_nodes_factory[index] > 1.0f) {
            return false;
        }
    }
    return true;
}

void *
tcod_create(uint32_t factory_coarse, const void *saved_parameters,
            uint32_t saved_parameters_length, char *error,
            uint32_t error_length)
{
    tcod_config config;
    tcod_default_config(&config);
    return tcod_create_configured(factory_coarse, saved_parameters,
                                  saved_parameters_length, &config,
                                  error, error_length);
}

void *
tcod_create_configured(uint32_t factory_coarse,
                       const void *saved_parameters,
                       uint32_t saved_parameters_length,
                       const tcod_config *requested_config,
                       char *error, uint32_t error_length)
{
    struct minipod_config config;
    struct disciplining_parameters parameters;
    char algorithm_error[OD_ERR_MSG_LEN];
    struct tcod_context *context;

    if (tcod_validate_config(requested_config, error, error_length) != 0)
        return NULL;
    memset(&config, 0, sizeof(config));
    memset(&parameters, 0, sizeof(parameters));
    memset(algorithm_error, 0, sizeof(algorithm_error));
    config.ref_fluctuations_ns = requested_config->ref_fluctuations_ns;
    config.phase_jump_threshold_ns = requested_config->phase_jump_threshold_ns;
    config.phase_resolution_ns = requested_config->phase_resolution_ns;
    config.debug = requested_config->debug;
    config.reactivity_min = requested_config->reactivity_min;
    config.reactivity_max = requested_config->reactivity_max;
    config.reactivity_power = requested_config->reactivity_power;
    config.nb_calibration = requested_config->nb_calibration;
    config.fine_stop_tolerance = requested_config->fine_stop_tolerance;
    config.max_allowed_coarse = requested_config->max_allowed_coarse;
    config.calibrate_first = (requested_config->flags &
        TCOD_CONFIG_CALIBRATE_FIRST) != 0u;
    config.oscillator_factory_settings = (requested_config->flags &
        TCOD_CONFIG_FACTORY_SETTINGS) != 0u;
    config.learn_temperature_table = (requested_config->flags &
        TCOD_CONFIG_LEARN_TEMPERATURE) != 0u;
    config.use_temperature_table = (requested_config->flags &
        TCOD_CONFIG_USE_TEMPERATURE) != 0u;
    config.fine_table_output_path =
        requested_config->fine_table_output_path[0] == '\0' ?
        "." : requested_config->fine_table_output_path;

    parameters.dsc_config.header = HEADER_MAGIC;
    parameters.dsc_config.version = DISCIPLINING_CONFIG_VERSION;
    parameters.dsc_config.ctrl_nodes_length = 3;
    parameters.dsc_config.ctrl_load_nodes[0] = 0.25f;
    parameters.dsc_config.ctrl_load_nodes[1] = 0.50f;
    parameters.dsc_config.ctrl_load_nodes[2] = 0.75f;
    parameters.dsc_config.coarse_equilibrium = -1;
    parameters.dsc_config.ctrl_nodes_length_factory = 3;
    parameters.dsc_config.ctrl_load_nodes_factory[0] = 0.25f;
    parameters.dsc_config.ctrl_load_nodes_factory[1] = 0.50f;
    parameters.dsc_config.ctrl_load_nodes_factory[2] = 0.75f;
    parameters.dsc_config.ctrl_drift_coeffs_factory[0] = 1.2f;
    parameters.dsc_config.ctrl_drift_coeffs_factory[1] = 0.0f;
    parameters.dsc_config.ctrl_drift_coeffs_factory[2] = -1.2f;
    parameters.dsc_config.coarse_equilibrium_factory =
        (int32_t)factory_coarse;
    parameters.dsc_config.calibration_valid = false;
    parameters.temp_table.header = HEADER_MAGIC;
    parameters.temp_table.version = DISCIPLINING_CONFIG_VERSION;
    if (saved_parameters != NULL) {
        struct disciplining_parameters saved;
        bool candidate = false;
        memset(&saved, 0, sizeof(saved));
        if (saved_parameters_length == sizeof(saved)) {
            memcpy(&saved, saved_parameters, sizeof(saved));
            candidate = true;
        } else if (saved_parameters_length == TCOD_EEPROM_SIZE) {
            const uint8_t *eeprom = (const uint8_t *)saved_parameters;
            memcpy(&saved.dsc_config, eeprom, sizeof(saved.dsc_config));
            memcpy(&saved.temp_table, eeprom + TCOD_TEMPERATURE_OFFSET,
                   sizeof(saved.temp_table));
            candidate = true;
        }
        if (candidate && tcod_saved_parameters_valid(&saved)) {
            memcpy(&parameters, &saved, sizeof(parameters));
        }
    }

    log_set_quiet(true);
    context = (struct tcod_context *)calloc(1, sizeof(*context));
    if (context != NULL) {
        context->algorithm = od_new_from_config(
            &config, &parameters, algorithm_error);
        if (context->algorithm == NULL) {
            free(context);
            context = NULL;
        }
    }
    if (context == NULL && error != NULL && error_length != 0u) {
        snprintf(error, error_length, "%s", algorithm_error);
        error[error_length - 1u] = '\0';
    }
    return context;
}

int
tcod_process(void *context, const tcod_input *input, tcod_output *output)
{
    struct od_input algorithm_input;
    struct od_output algorithm_output;
    struct od_monitoring monitoring;
    int result;

    if (context == NULL || input == NULL || output == NULL)
        return -1;
    memset(&algorithm_input, 0, sizeof(algorithm_input));
    memset(&algorithm_output, 0, sizeof(algorithm_output));
    memset(&monitoring, 0, sizeof(monitoring));
    algorithm_input.temperature = input->temperature;
    algorithm_input.phase_error.tv_sec =
        (time_t)(input->phase_error_ns / NS_PER_SECOND);
    algorithm_input.phase_error.tv_nsec =
        (long)(input->phase_error_ns % NS_PER_SECOND);
    algorithm_input.fine_setpoint = input->fine_setpoint;
    algorithm_input.coarse_setpoint = input->coarse_setpoint;
    algorithm_input.qErr = input->quantization_error_ps;
    algorithm_input.calibration_requested =
        (input->flags & TCOD_INPUT_CALIBRATE) != 0u;
    algorithm_input.lock = (input->flags & TCOD_INPUT_LOCKED) != 0u;
    algorithm_input.valid = (input->flags & TCOD_INPUT_VALID) != 0u;
    algorithm_input.survey_completed =
        (input->flags & TCOD_INPUT_SURVEY_DONE) != 0u;
    if ((input->flags & TCOD_INPUT_PHASE_ERROR) != 0u)
        algorithm_input.phasemeter_status = PHASEMETER_ERROR;
    else if ((input->flags & TCOD_INPUT_PHASE_VALID) != 0u)
        algorithm_input.phasemeter_status = PHASEMETER_BOTH_TIMESTAMPS;
    else if ((input->flags & TCOD_INPUT_REFERENCE_VALID) != 0u)
        algorithm_input.phasemeter_status =
            PHASEMETER_NO_ART_INTERNAL_TIMESTAMPS;
    else if ((input->flags & TCOD_INPUT_OSCILLATOR_VALID) != 0u)
        algorithm_input.phasemeter_status = PHASEMETER_NO_GNSS_TIMESTAMPS;
    else
        algorithm_input.phasemeter_status = PHASEMETER_INIT;

    result = od_process(((struct tcod_context *)context)->algorithm,
                        &algorithm_input,
                        &algorithm_output);
    if (result != 0)
        return result;
    result = od_get_monitoring_data(
        ((struct tcod_context *)context)->algorithm, &monitoring);
    if (result != 0)
        return result;

    memset(output, 0, sizeof(*output));
    output->action = (uint32_t)algorithm_output.action;
    output->setpoint = algorithm_output.setpoint;
    output->phase_jump_ns = algorithm_output.value_phase_ctrl;
    output->state = (uint32_t)monitoring.status;
    output->clock_class = (uint32_t)monitoring.clock_class;
    output->convergence_count = monitoring.current_phase_convergence_count;
    output->convergence_threshold =
        monitoring.valid_phase_convergence_threshold;
    output->convergence_percent = monitoring.convergence_progress;
    output->ready_for_holdover = monitoring.ready_for_holdover ? 1u : 0u;
    return 0;
}

void
tcod_destroy(void *context)
{
    struct tcod_context *wrapper = (struct tcod_context *)context;
    if (wrapper == NULL)
        return;
    if (wrapper->calibration != NULL) {
        free(wrapper->calibration->ctrl_points);
        free(wrapper->calibration);
    }
    od_destroy(&wrapper->algorithm);
    free(wrapper);
}

uint32_t
tcod_version(void)
{
    return TCOD_VERSION;
}

int
tcod_get_calibration_plan(void *context, uint16_t *points,
                          uint32_t capacity, uint32_t *point_count,
                          uint32_t *samples_per_point)
{
    struct tcod_context *wrapper = (struct tcod_context *)context;
    uint32_t index;

    if (wrapper == NULL || points == NULL || point_count == NULL ||
        samples_per_point == NULL || wrapper->calibration != NULL)
        return -1;
    wrapper->calibration = od_get_calibration_parameters(wrapper->algorithm);
    if (wrapper->calibration == NULL)
        return -2;
    if (capacity < (uint32_t)wrapper->calibration->length) {
        free(wrapper->calibration->ctrl_points);
        free(wrapper->calibration);
        wrapper->calibration = NULL;
        return -3;
    }
    for (index = 0; index < (uint32_t)wrapper->calibration->length; ++index)
        points[index] = wrapper->calibration->ctrl_points[index];
    *point_count = (uint32_t)wrapper->calibration->length;
    *samples_per_point = (uint32_t)wrapper->calibration->nb_calibration;
    return 0;
}

int
tcod_complete_calibration(void *context, const float *phase_samples,
                          uint32_t sample_count)
{
    struct tcod_context *wrapper = (struct tcod_context *)context;
    struct calibration_results *results;
    uint32_t required;

    if (wrapper == NULL || wrapper->calibration == NULL ||
        phase_samples == NULL)
        return -1;
    required = (uint32_t)wrapper->calibration->length *
               (uint32_t)wrapper->calibration->nb_calibration;
    if (sample_count != required)
        return -2;
    results = (struct calibration_results *)calloc(1, sizeof(*results));
    if (results == NULL)
        return -3;
    results->measures = (float *)malloc(required * sizeof(float));
    if (results->measures == NULL) {
        free(results);
        return -3;
    }
    memcpy(results->measures, phase_samples, required * sizeof(float));
    results->length = wrapper->calibration->length;
    results->nb_calibration = wrapper->calibration->nb_calibration;
    od_calibrate(wrapper->algorithm, wrapper->calibration, results);
    /* od_calibrate owns and releases both structures. */
    wrapper->calibration = NULL;
    return 0;
}

uint32_t
tcod_parameters_size(void)
{
    return TCOD_EEPROM_SIZE;
}

int
tcod_get_parameters(void *context, void *buffer, uint32_t buffer_length)
{
    struct tcod_context *wrapper = (struct tcod_context *)context;
    struct disciplining_parameters parameters;
    int result;

    if (wrapper == NULL || buffer == NULL ||
        buffer_length < TCOD_EEPROM_SIZE)
        return -1;
    result = od_get_disciplining_parameters(
        wrapper->algorithm, &parameters);
    if (result != 0)
        return result;
    memset(buffer, 0, TCOD_EEPROM_SIZE);
    memcpy(buffer, &parameters.dsc_config, sizeof(parameters.dsc_config));
    memcpy((uint8_t *)buffer + TCOD_TEMPERATURE_OFFSET,
           &parameters.temp_table, sizeof(parameters.temp_table));
    return 0;
}
