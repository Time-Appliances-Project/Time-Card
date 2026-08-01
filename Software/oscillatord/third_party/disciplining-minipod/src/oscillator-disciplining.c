/*
 * liboscillator-disciplining: Disciplining Algorithm for Orolia's mRO50.
 * Copyright (C) 2021  Spectracom SAS

 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file oscillator-disciplining.c
 * @brief liboscillator-disciplining's main file.
 *
 * liboscillator-disciplining is a small library responsible of abstracting
 * disciplining algorithms used for an oscillator for which we want to control
 * the frequency compared to a PPS reference.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "oscillator-disciplining/oscillator-disciplining.h"

#include "algorithm_structs.h"
#include "checks.h"
#include "fine_circular_buffer.h"
#include "log.h"
#include "parameters.h"
#include "phase.h"
#include "utils.h"

/**
 * @def SETTLING_TIME_MRO50
 * @brief Time oscillator needs to apply new control values
 */
#define SETTLING_TIME_MRO50 6
/**
 * @def WINDOW_TRACKING
 * @brief Number of inputs for tracking state
 */
#define WINDOW_TRACKING 6
#define TRACKING_PHASE_CONVERGENCE_REACTIVITY_MIN 180
#define TRACKING_PHASE_CONVERGENCE_REACTIVITY_MAX 360

#define DAY_IN_SECONDS 86400

/**
 * @def ALPHA_ES_TRACKING
 * @brief  smoothing constant for tracking state
 */
#define ALPHA_ES_TRACKING 0.01

/**
 * @brief Window size used for each step
 */
uint16_t state_windows[disciplining_state_count] = {
	WINDOW_TRACKING, /* WARMUP */
	WINDOW_TRACKING, /* INIT */
	WINDOW_TRACKING, /* TRACKING */
	WINDOW_TRACKING, /* HOLDOVER */
	WINDOW_TRACKING, /* CALIBRATION */
};

/** Strings displayed for each status */
const char *disciplining_state_strings[disciplining_state_count] = {
	"WARMUP",
	"INIT",
	"TRACKING",
	"HOLDOVER",
	"CALIBRATION"
};

const char *cstring_from_disciplining_state(enum disciplining_state state)
{
	if (state < 0 || state >= disciplining_state_count)
	{
		log_error("Trying to convert to string an invalid disciplining state : %" PRIi64, (long)state);
		return "INVALID";
	}
	return disciplining_state_strings[state];
}

enum clock_class clock_class_from_disciplining_state(enum disciplining_state state)
{
	switch (state)
	{
	case WARMUP:
	case INIT:
		return CLOCK_CLASS_UNCALIBRATED;
	case TRACKING:
	case CALIBRATION:
		return CLOCK_CLASS_CALIBRATING;
	case HOLDOVER:
		return CLOCK_CLASS_HOLDOVER;
	default:
		log_error("Trying to convert an invalid disciplining state to a clock class: %" PRIi64, (long)state);
		return CLOCK_CLASS_UNCALIBRATED;
	}
}

const char *clock_class_strings[clock_class_count] = {
	"Uncalibrated",
	"Calibrating",
	"Holdover",
	"Lock"
};

const char *cstring_from_clock_class(enum clock_class class)
{
	if (class < 0 || class >= clock_class_count)
	{
		log_error("Trying to convert an invlaid clock class to string: %" PRIi64, (long)class);
		return "INVALID";
	}
	return clock_class_strings[class];
}

/**
 * @struct od
 * @brief Library context.
 */
struct od {
	/** Algorith state */
	struct algorithm_state state;
	/** Algorithm parameters */
	struct minipod_config minipod_config;
	struct disciplining_parameters dsc_parameters;
};

/**
 * @brief Set new state and reset counter related to state
 *
 * @param state pointer to algorithm_state structure that will be updated
 * @param new_state New state value
 */
static void set_state(struct algorithm_state *state, enum disciplining_state new_state)
{
	state->status = new_state;
	state->od_inputs_count = 0;
	state->current_phase_convergence_count = 0;

	if (new_state == HOLDOVER) {
		state->timestamp_entering_holdover = time(NULL);
		/* Save smoothed temperature if entering holdover */
		state->holdover_mRO_EP_temperature = state->mRO_EP_temperature;

		log_debug("Smoothed temperature when entering holdover: %.2f", state->holdover_mRO_EP_temperature);
	} else if (new_state == TRACKING) {
		/* Reset ready to go in holdover */
		state->ready_to_go_in_holdover_class = false;
	}
}

/**
 * @brief Set the output action and value defined by algorithm
 *
 * @param output pointer to structure to be update
 * @param action
 * @param setpoint
 * @param value_phase_ctrl
 */
static void set_output(struct od_output *output, enum output_action action, uint32_t setpoint, int32_t value_phase_ctrl)
{
	output->action = action;
	output->setpoint = setpoint;
	output->value_phase_ctrl = value_phase_ctrl;
	return;
}

/**
 * @brief Update smoothed temperature with new input value
 *
 * @param state
 * @param temperature
 */
static void update_temperature(struct algorithm_state *state, float temperature, float smoothing_coefficient)
{
	/* Update smoothed temperature */
	if (state->mRO_EP_temperature < -10.0) {
		/* Init temperature to first value */
		state->mRO_EP_temperature = temperature;
	} else {
		/* Smoothly update temprature with new value */
		state->mRO_EP_temperature = smoothing_coefficient * temperature
			+ (1 - smoothing_coefficient) * state->mRO_EP_temperature;
	}
}

/**
 * @brief Init ctrl points used by algorithm to compute relation between fine value applied and phase error expected
 *
 * @param state pointer to algorithm_state which points will be initialized
 * @param load_nodes fine points in percentage of the range
 * @param drift_coeffs drift coefficients for each fine points
 * @param length size of the two arrays
 * @return int 0 on success else error
 */
static int init_ctrl_points(struct algorithm_state *state, float *load_nodes, float *drift_coeffs, uint8_t length) {
	uint16_t diff_fine;
	int i;

	/*
	* Init ctrl_points, which have the same size as
	* ctrl_load_nodes and ctrl_drift_coeffs, which
	* is ctrl_nodes_length
	*/
	if (length <=0)
		return -EINVAL;

	state->ctrl_points = malloc(length * sizeof(float));
	if (state->ctrl_points == NULL)
		return -ENOMEM;

	state->ctrl_drift_coeffs = malloc(length * sizeof(float));
	if (state->ctrl_drift_coeffs == NULL)
		return -ENOMEM;

	diff_fine = state->ctrl_range_fine[1] - state->ctrl_range_fine[0];
	log_debug("Control points used:");
	for (i = 0; i < length; i++) {
		/* Control points used over the fine range */
		state->ctrl_points[i] = (float) state->ctrl_range_fine[0] + load_nodes[i] * diff_fine;
		/* drift coefficients for each control point */
		state->ctrl_drift_coeffs[i] = drift_coeffs[i];
		log_debug("\t%d: %f -> %f", i, state->ctrl_points[i], state->ctrl_drift_coeffs[i]);
	}
	state->ctrl_points_length = length;
	return 0;
}

/**
 * @brief Compute fine value to apply to reach 0 phase error
 *
 * @param state pointer to algorithm_state
 * @param react_coeff reactivity coefficient
 * @param fine_ctrl_value pointer where fine value will be stored
 * @return int 0 on success else -1
 */
static int compute_fine_value(struct algorithm_state *state, float react_coeff, uint16_t *fine_ctrl_value)
{
	float interpolation_value;
	int ret;
	ret = lin_interp(
		state->ctrl_points,
		state->ctrl_drift_coeffs,
		state->ctrl_points_length,
		Y_INTERPOLATION,
		react_coeff,
		&interpolation_value
	);
	if (ret < 0)
	{
		log_error("Error occured in lin_interp: %d", ret);
		return -1;
	}
	/* If linear interpretation returns a negative value, consider value found is 0 before conversion to integers */
	if (interpolation_value <= 0.0) {
		log_warn("fine control value found is negative (%f), setting to 0.0", interpolation_value);
		interpolation_value = 0.0;
	}

	*fine_ctrl_value = (uint16_t) round(interpolation_value);
	return 0;
}

static float get_smooth_coefficient(struct algorithm_state *state)
{
	float coefficient = 1.0;

	switch(state->status) {
	case TRACKING:
		if (state->current_phase_convergence_count <= round(1.0 / ALPHA_ES_TRACKING)) {
			log_debug("fast smoothing convergence : 2.0 * %f applied", ALPHA_ES_TRACKING);
			coefficient = 2.0;
		} else if ((state->current_phase_convergence_count > round(6 / ALPHA_ES_TRACKING))
			&& (state->current_phase_convergence_count <= round(24 / ALPHA_ES_TRACKING))) {
			log_debug("slow smoothing convergence : 0.5* %f applied", ALPHA_ES_TRACKING);
			coefficient = 0.5;
		} else if (state->current_phase_convergence_count > round(24 / ALPHA_ES_TRACKING)){
			log_debug("final slow smoothing convergence : 0.25* %f applied", ALPHA_ES_TRACKING);
			coefficient = 0.25;
		}

		return coefficient * ALPHA_ES_TRACKING;
	default:
		return ALPHA_ES_TRACKING;
	}

}
/**
 * @brief Print inputs' phase error
 *
 * @param inputs array of input data containing gnss valid flag
 * @param length array length
 */
static void print_inputs(struct algorithm_input *inputs, int length)
{
	int i;
	if (!inputs || length <= 0)
		return;
	char * str = calloc((length + 2) * 22, sizeof(char));
	strcat(str, "Inputs: [");
	for (i = 0; i < length; i++) {
		char float_num[20];
		sprintf(float_num, "%.3f", inputs[i].phase_error);
		strcat(str, float_num);
		if (i < length -1)
		strcat(str, ", ");
	}
	strcat(str, "]");
	log_info(str);
	free(str);
	str = NULL;
}

/**
 * @brief Initialize temperature table with value from disciplining_parameters
 *
 * @param state
 * @param config
 */
static void init_temperature_table(struct algorithm_state *state, struct minipod_config *config, struct temperature_table *temp_table)
{
	int i;
	int j;
	/* Init fine_circular_buffers */
	log_info("Initializing temperature table");
	for (i = 0; i < TEMPERATURE_STEPS; i ++) {
		state->fine_estimated_es_buffer[i].buffer_length = 0;
		state->fine_estimated_es_buffer[i].read_index = 0;
		state->fine_estimated_es_buffer[i].write_index = 0;
		state->fine_estimated_es_buffer[i].mean_fine = 0.0;
		for (j = 0; j < CIRCULAR_BUFFER_SIZE; j++) {
			state->fine_estimated_es_buffer[i].buffer[j] = 0.0;
		}
	}

	for (i = 0; i < MEAN_TEMPERATURE_ARRAY_MAX; i++) {
		float fine = (float) temp_table->mean_fine_over_temperature[i] / 10.0;
		if (fine > FINE_RANGE_MIN && fine <= FINE_RANGE_MAX) {
			log_debug("Temperature table: Adding mean value of %.2f in temperature range [%.2f, %.2f[",
				fine,
				(i + STEPS_BY_DEGREE * MIN_TEMPERATURE) / STEPS_BY_DEGREE,
				(i + 1 + STEPS_BY_DEGREE * MIN_TEMPERATURE) / STEPS_BY_DEGREE);
			for (j = 0; j < MIN_VALUES_FOR_MEAN; j++) {
				if (write_fine(&state->fine_estimated_es_buffer[i], fine) != 0) {
					log_error("Could not write value in buffer for temperature [%.2f, %.2f[",
						(i + STEPS_BY_DEGREE * MIN_TEMPERATURE) / STEPS_BY_DEGREE,
						(i + 1 + STEPS_BY_DEGREE * MIN_TEMPERATURE) / STEPS_BY_DEGREE);
				}
			}
		}
	}
	sprintf(state->fine_estimated_buffer_buffer_output_path, "%s/fine_estimated_es_table.txt", config->fine_table_output_path);

	if (config->learn_temperature_table)
		log_warn("Temperature table will be updated during disciplining");
	else
		log_info("Temperature table will not be updated during disciplining");

	if (config->use_temperature_table)
		log_info("Temperature table will be used for enhanced temperature compensation");
	else
		log_warn("Temperature table will not be used for enhanced temperature compensation");
}

/**
 * @brief Initialize algorithm state with disciplining_parameters and minipod config
 *
 * @param od library context
 * @return int 0 on sucess else error
 */
static int init_algorithm_state(struct od * od) {
	int i;
	int ret;
	struct algorithm_state *state = &od->state;
	struct disciplining_config *dsc_config = &od->dsc_parameters.dsc_config;
	struct temperature_table *temp_table = &od->dsc_parameters.temp_table;
	struct minipod_config *config = &od->minipod_config;

	log_info("Init algorithm state with Disciplining-Minipod v%s", PACKAGE_VERSION);
	/* Constant state values */
	state->mRO_fine_step_sensitivity = MRO_FINE_STEP_SENSITIVITY;
	state->mRO_coarse_step_sensitivity = MRO_COARSE_STEP_SENSITIVITY;

	state->ctrl_range_coarse[0] = COARSE_RANGE_MIN;
	state->ctrl_range_coarse[1] = COARSE_RANGE_MAX;
	state->ctrl_range_fine[0] = FINE_MID_RANGE_MIN;
	state->ctrl_range_fine[1] = FINE_MID_RANGE_MAX;

	state->fine_mid = (uint16_t) (0.5 * (FINE_MID_RANGE_MIN + FINE_MID_RANGE_MAX));

	/* Init state variables to default values */
	set_state(state, INIT);
	state->calib = false;
	state->fine_ctrl_value = 0;
	state->disciplining_ko_count = 0;

	state->estimated_drift = 0;
	state->current_phase_convergence_count = 0;
	state->previous_freq_error = 0.0;

	state->mRO_EP_temperature = -99.99;
	state->holdover_mRO_EP_temperature = -99.99;

	state->ready_to_go_in_holdover_class = false;

	state-> tracking_count_test = 0;

	/* Allocate memory for algorithm inputs */
	state->inputs = (struct algorithm_input*) malloc(WINDOW_TRACKING * sizeof(struct algorithm_input));
	if (!state->inputs) {
		log_error("Could not allocate memory for algorithm inputs !");
		return -1;
	}

	/* Check wether user parameters or factory ones should be used */
	if (config->oscillator_factory_settings) {
		/*
		 * Factory settings should be used
		 * Program will use factory settings and will not update user parameters
		 */
		log_debug("Using factory settings");
		ret = init_ctrl_points(
			state,
			dsc_config->ctrl_load_nodes_factory,
			dsc_config->ctrl_drift_coeffs_factory,
			dsc_config->ctrl_nodes_length_factory
		);
	} else {
		/*
		 * User parameters should be used
		 * First we check calibration valid flag to check if user parameters are valid
		 * Then we try to initialize control points using these values
		 * If this fail factory parameters will override user parameters
		 * and we will initialize using factory parameters
		 */
		if (!dsc_config->calibration_valid) {
			log_warn("Calibration parameters are not valid for this card. Factory settings will be used.");
			ret = -1;
		} else {
			/* Init control points with user parameters */
			ret = init_ctrl_points(
				state,
				dsc_config->ctrl_load_nodes,
				dsc_config->ctrl_drift_coeffs,
				dsc_config->ctrl_nodes_length
			);
		}
		if (ret != 0) {
			/* User parameters are not correct, trying to use factory parameters */
			log_warn("User parameters are corrupted, trying to use factory parameters");
			ret = init_ctrl_points(
				state,
				dsc_config->ctrl_load_nodes_factory,
				dsc_config->ctrl_drift_coeffs_factory,
				dsc_config->ctrl_nodes_length_factory
			);
			if (ret == 0) {
				log_info("Factory parameters can be used, resetting user parameters to factory ones");
				dsc_config->ctrl_nodes_length = dsc_config->ctrl_nodes_length_factory;
				dsc_config->coarse_equilibrium = dsc_config->coarse_equilibrium_factory;
				for (i = 0; i < dsc_config->ctrl_nodes_length; i++) {
					dsc_config->ctrl_drift_coeffs[i] = dsc_config->ctrl_drift_coeffs_factory[i];
					dsc_config->ctrl_load_nodes[i] = dsc_config->ctrl_load_nodes_factory[i];
				}
				dsc_config->estimated_equilibrium_ES = 0;
			}
		}

	}
	if (ret != 0) {
		/* If factory parameters cannot be used then we reset both factory and user parameters to default values */
		log_warn("Could not initialize algorithm with factory data from config, resetting default parameters");
		dsc_config->ctrl_nodes_length = 3;
		dsc_config->ctrl_load_nodes[0] = 0.25;
		dsc_config->ctrl_load_nodes[1] = 0.5;
		dsc_config->ctrl_load_nodes[2] = 0.75;
		dsc_config->ctrl_drift_coeffs[0] = 1.2;
		dsc_config->ctrl_drift_coeffs[1] = 0.0;
		dsc_config->ctrl_drift_coeffs[2] = -1.2;
		dsc_config->coarse_equilibrium = -1;
		dsc_config->ctrl_load_nodes_factory[0] = 0.25;
		dsc_config->ctrl_load_nodes_factory[1] = 0.5;
		dsc_config->ctrl_load_nodes_factory[2] = 0.75;
		dsc_config->ctrl_drift_coeffs_factory[0] = 1.2;
		dsc_config->ctrl_drift_coeffs_factory[1] = 0.0;
		dsc_config->ctrl_drift_coeffs_factory[2] = -1.2;
		dsc_config->coarse_equilibrium_factory = -1;
		dsc_config->calibration_valid = false;
		config->calibrate_first = false;
		ret = init_ctrl_points(
			state,
			dsc_config->ctrl_load_nodes,
			dsc_config->ctrl_drift_coeffs,
			dsc_config->ctrl_nodes_length
		);
		if (ret != 0)
			return ret;
	}

	ret = compute_fine_value(state, 0, &state->estimated_equilibrium);
	if (ret != 0) {
		log_error("Error computing fine value from control drift coefficients");
		return ret;
	}
	if (dsc_config->estimated_equilibrium_ES != 0)
		state->estimated_equilibrium_ES = (float) dsc_config->estimated_equilibrium_ES;
	else
		state->estimated_equilibrium_ES = (float) state->estimated_equilibrium;
	log_info("Initialization: Estimated equilibrium is %d and estimated equilibrium ES is %f", state->estimated_equilibrium, state->estimated_equilibrium_ES);

	init_temperature_table(state, config, temp_table);

	return 0;
}

/**
 * @brief Check if fine control setpoint is within available range +/- a tolerance.
 * If not the coarse control value is changed in order to re-center the fine control.
 * init_control_mRO must be called after a coarse change.
 * This is used to check if calibration went well or not
 *
 * @param od library context
 * @param input Pointer to input structure
 * @param output Pointer to output structure
 * @return wether calibration is good or not
 */
static bool control_check_mRO(struct od *od, const struct od_input *input, struct od_output *output) {
	struct algorithm_state *state = &(od->state);
	struct minipod_config *config = &(od->minipod_config);
	struct disciplining_config *dsc_config = &(od->dsc_parameters.dsc_config);
	int i;

	log_debug("Control Check mRO:");
	/* Check estimated equilibrium is in tolerance range */
	if (state->calib
		&& state->estimated_equilibrium >= (uint32_t) state->ctrl_range_fine[0] + config->fine_stop_tolerance
		&& state->estimated_equilibrium <= (uint32_t) state->ctrl_range_fine[1] - config->fine_stop_tolerance
		&& state->ctrl_drift_coeffs[0] >= 0.0 && state->ctrl_drift_coeffs[state->ctrl_points_length - 1] <= 0.0
	) {
		log_info("Estimated equilibrium is in tolerance range, saving calibration in config file");
		for(i = 0; i < state->ctrl_points_length - 2; i++) {
			if (state->ctrl_drift_coeffs[i] < state->ctrl_drift_coeffs[i+1]) {
				log_debug("ctrl_drift_coeffs[%d] = %f is greater than ctrl_drift_coeffs[%d] = %f",
					i, state->ctrl_drift_coeffs[i], i + 1, state->ctrl_drift_coeffs[i+1]);
				log_warn("Calibration coefficients are not descending with fine values");
				set_output(output, CALIBRATE, 0, 0);
				return false;
			}
		}
		log_debug("Calibration coefficients are descending with fine values");
		for(i = 0; i < state->ctrl_points_length; i++) {
			if (fabs(state->ctrl_drift_coeffs[i]) > DRIFT_COEFFICIENT_ABSOLUTE_MAX) {
				log_warn("ctrl_drift_coeffs[%d] coefficient is greater than %f in absolute: %f",
					i, DRIFT_COEFFICIENT_ABSOLUTE_MAX, state->ctrl_drift_coeffs[i]);
				set_output(output, CALIBRATE, 0, 0);
				return false;
			}
		}
		log_debug("All coefficients are inferior to %f in absolute value", DRIFT_COEFFICIENT_ABSOLUTE_MAX);

		dsc_config->coarse_equilibrium = input->coarse_setpoint;
		dsc_config->calibration_valid = true;
		return true;
	} else if (state->calib) {
		log_info("Coarse alignment must be adjusted based on calibration");
		int32_t delta_mid_fine = (int32_t) state->estimated_equilibrium - state->fine_mid;
		int32_t delta_coarse = (int32_t) round(
			delta_mid_fine
			* state->mRO_fine_step_sensitivity
			/ state->mRO_coarse_step_sensitivity
		);
		log_debug("Deltas: mid_fine is %d, coarse is %d", delta_mid_fine, delta_coarse);

		if (abs(delta_coarse) > config->max_allowed_coarse) {
			log_info("Large coarse change %u can lead to the loss of LOCK!", delta_coarse);
			delta_coarse = delta_coarse > 0 ?
				config->max_allowed_coarse :
				-config->max_allowed_coarse;
		}

		set_output(output, ADJUST_FINE, input->coarse_setpoint + delta_coarse, 0);
		log_info("Requesting a coarse alignement to value %d", output->setpoint);
		return false;
	} else {
		/* mRO needs to be calibrated */
		set_output(output, CALIBRATE, 0, 0);
		return false;
	}
}

static float get_reactivity(float phase_ns, int sigma, int min, int max, int power) {
	float r = max * exp(-pow(phase_ns/sigma, power));
	return r > min ? r : min;
}

/**
 * @brief Add a new input to algorithm_input array
 * This is done each second to fill a state's window
 *
 * @param algorithm_input
 * @param input
 */
static void add_input_to_algorithm(struct algorithm_input *algorithm_input, const struct od_input *input)
{
	algorithm_input->phase_error = input->phase_error.tv_nsec + (float) input->qErr / PS_IN_NS;
	algorithm_input->valid = input->valid;
	algorithm_input->lock = input->lock;
	algorithm_input->phasemeter_status = input->phasemeter_status;
}

struct od *od_new_from_config(struct minipod_config *minipod_config, struct disciplining_parameters *dsc_params, char err_msg[OD_ERR_MSG_LEN])
{
	(void)err_msg;
	struct od *od;
	int ret;

	od = calloc(1, sizeof(struct od));
	if (od == NULL)
		return NULL;

	memcpy(&od->minipod_config, minipod_config, sizeof(struct minipod_config));

	log_set_level(
		od->minipod_config.debug >= 0 ?
		od->minipod_config.debug :
		LOG_INFO
	);
	print_minipod_config(&od->minipod_config);

	memcpy(&od->dsc_parameters, dsc_params, sizeof(struct disciplining_parameters));

	print_disciplining_config(&od->dsc_parameters.dsc_config);

	ret = init_algorithm_state(od);
	if (ret < 0) {
		log_error("Error occured during init_algorithm_state, err %d", ret);
		free(od);
		od = NULL;
	}
	return od;
}

int od_process(struct od *od, const struct od_input *input,
		struct od_output *output)
{
	int ret;
	if (od == NULL || input == NULL || output == NULL)
	{
		log_error("At least one input variable is NULL");
		return -EINVAL;
	}

	log_set_level(
		od->minipod_config.debug >= 0 ?
		od->minipod_config.debug :
		LOG_INFO
	);

	struct algorithm_state *state = &(od->state);
	struct disciplining_config *dsc_config = &(od->dsc_parameters.dsc_config);
	struct minipod_config *config = &(od->minipod_config);

	/* Reset output structure */
	set_output(output, NO_OP, 0, 0);

	/* Add new algorithm input */
	add_input_to_algorithm(&state->inputs[state->od_inputs_count], input);
	log_debug("input: phase_error: %d, qErr: %d", input->phase_error.tv_nsec,input->qErr);
	log_debug("INPUT[%d] = %f",
		state->od_inputs_count,
		state->inputs[state->od_inputs_count].phase_error
	);
	state->od_inputs_count++;

	log_debug("Smoothed temperature is now %.2f", state->mRO_EP_temperature);

	log_debug("OD_PROCESS: State is %s, Conv. Step %u, (%u/%u), GNSS valid: %s, mRO lock: %s and phasemeter status: %d",
		cstring_from_disciplining_state(od->state.status),
		state->current_phase_convergence_count,
		state->od_inputs_count,
		WINDOW_TRACKING,
		input->valid ? "True" : "False", input->lock ? "True" : "False", input->phasemeter_status
	);

	/* Check if we reached the number of inputs required to process state's step */
	if (state->od_inputs_count == WINDOW_TRACKING) {
		state->od_inputs_count = 0;

		enum gnss_state gnss_state = check_gnss_valid_over_cycle(state->inputs, WINDOW_TRACKING);
		bool mro50_lock_state = check_lock_over_cycle(state->inputs, WINDOW_TRACKING);
		enum PHASEMETER_STATUS phasemeter_status = check_phasemeter_status_over_cycle(state->inputs, WINDOW_TRACKING);

		float smoothing_coefficient = get_smooth_coefficient(state);
		update_temperature(state, input->temperature, smoothing_coefficient);

		/* Check if GNSS state is valid and mro50 is locked and phasemeter is valid*/
		if (gnss_state == GNSS_OK && (mro50_lock_state || od->state.status  == INIT) && (phasemeter_status == PHASEMETER_BOTH_TIMESTAMPS || phasemeter_status == PHASEMETER_INIT))
		{
			state->disciplining_ko_count = 0;
			if (od->state.status != CALIBRATION
				&& (
					config->calibrate_first
					|| input->calibration_requested
				)
			)
			{
				config->calibrate_first = false;
				set_output(output, CALIBRATE, 0, 0);
				/* FIXME !*/
				od->state.status = CALIBRATION;
				return 0;

			}

			float mean_phase_error;
			/* State machine defining what to do, depending on current state and inputs for the state */
			switch(state->status) {
			/* Calibration: calibration data is available and control check must be done */
			case CALIBRATION:
				/** Need to check mRO control values
				 * If calibration has been done, we need to make a control check of the mRO
				 */
				if(!control_check_mRO(od, input, output))
				{
					/* Control check has not been passed.
					* Either a Coarse alignement or a calibration process
					* has been decided and prepared in output
					*/
					state->calib = false;
					log_debug("Control_check_mro has not been passed !");
					return 0;
				}
				log_debug("Control check mRO has been passed !");
				state->calib = false;
				set_state(state, INIT);

				/* Request coarse value to be saved in mRO50 memory */
				set_output(output, SAVE_DISCIPLINING_PARAMETERS, 0, 0);
				return 0;
				break;

			/* Initialization */
			case INIT:
				/*
				 * Check if algorithm is using factory settings
				 * if so check if coarse_equilibrium_factory is superior to 0 (known coarse_equilibrium_factory)
				 * and that coarse set on mRO50 is equal to coarse_equilibrium_factory
				 */
				if (config->oscillator_factory_settings
					&& dsc_config->coarse_equilibrium_factory > 0
					&& input->coarse_setpoint != dsc_config->coarse_equilibrium_factory) {
					set_output(output, ADJUST_COARSE, dsc_config->coarse_equilibrium_factory, 0);
					log_info("INITIALIZATION: Applying factory coarse equilibrium setpoint %d", dsc_config->coarse_equilibrium);
				/*
				 * Check if algorithm is using user settings
				 * if so check if coarse_equilibrium is superior to 0 (known coarse_equilibrium)
				 * and that coarse set on mRO50 is equal to coarse_equilibrium
				 */
				} else if (!config->oscillator_factory_settings
					&& dsc_config->coarse_equilibrium > 0
					&& input->coarse_setpoint != dsc_config->coarse_equilibrium) {
					set_output(output, ADJUST_COARSE, dsc_config->coarse_equilibrium, 0);
					log_info("INITIALIZATION: Applying coarse equilibrium setpoint %d", dsc_config->coarse_equilibrium);
				} else {
					/*
					 * Check if coarse is known, wether we are using factory settings or not
					 */
					if ((!config->oscillator_factory_settings && dsc_config->coarse_equilibrium < 0) ||
						(config->oscillator_factory_settings && dsc_config->coarse_equilibrium_factory < 0)) {
						log_warn("Unknown coarse_equilibrium in current config, using value saved in oscillator,"
							"consider calibration if disciplining is not efficient");
					}
					/* Set fine value to estimated equilibrium */
					set_output(output, ADJUST_FINE, (uint32_t) round(state->estimated_equilibrium_ES), 0);
					set_state(state, TRACKING);
					state->fine_ctrl_value = state->estimated_equilibrium_ES;
					log_info("INITIALIZATION: Applying estimated fine equilibrium setpoint %.2f", state->estimated_equilibrium_ES);
				}
				return 0;
				break;

			/*
			 * Holdover and valid flag switched to valid,
			 * We wait for one cycle to start disciplining again
			 */
			case HOLDOVER:
				set_state(state, TRACKING);
				set_output(output, ADJUST_FINE, (uint32_t) round(state->estimated_equilibrium_ES), 0);
				log_info("HOLDOVER: Gnss flag valid again, waiting one cycle before restarting disciplining");
				break;

			/*
			 * Tracking mode:
			 * Only mean phase error is used to define fine value to apply
			 * Goal of this state is to reach minimum phase error possible
			 */
			case TRACKING:
				state-> tracking_count_test++;
				log_info("tracking count %u", state-> tracking_count_test);
				print_inputs(state->inputs, WINDOW_TRACKING);
				/* Compute mean phase error over cycle */
				ret = compute_phase_error_mean(state->inputs, WINDOW_TRACKING, &mean_phase_error);
				if (ret != 0) {
					log_error("Mean phase error could not be computed");
					set_state(state, HOLDOVER);
					set_output(output, ADJUST_FINE,  (uint32_t) round(state->estimated_equilibrium_ES), 0);
					return 0;
				}
				/* Check inputs values does not contain any outlier */
				if (!check_no_outlier(state->inputs, WINDOW_TRACKING,
					mean_phase_error, config->ref_fluctuations_ns))
				{
					log_warn("Outlier detected ! entering holdover");
					set_state(state, HOLDOVER);
					set_output(output, ADJUST_FINE,  (uint32_t) round(state->estimated_equilibrium_ES), 0);
					return 0;
				}

				/* Check phase error is below threshold configured */
				if (fabs(mean_phase_error) < (float) config->phase_jump_threshold_ns)
				{
					/*
					 * Phase error is below reference and control value in midrange
					 * We can compute a new estimated equilibrium
					 */
					if (fabs(mean_phase_error) < config->ref_fluctuations_ns
						&& (state->fine_ctrl_value >= state->ctrl_range_fine[0]
						&& state->fine_ctrl_value <= state->ctrl_range_fine[1])
						&& fabs(state->inputs[WINDOW_TRACKING - 1].phase_error - state->inputs[0].phase_error)
						< (float) config->ref_fluctuations_ns)
					{
						/* Update estimated equilibrium ES and smoothed temperature according to smoothing coefficient */
						state->estimated_equilibrium_ES =
							(smoothing_coefficient * state->fine_ctrl_value
							+ (1.0 - smoothing_coefficient) * state->estimated_equilibrium_ES);

						state->current_phase_convergence_count++;
						if (state->current_phase_convergence_count  == UINT16_MAX)
							state->current_phase_convergence_count = round(48.0 / ALPHA_ES_TRACKING) + 1;

						/* Update ready to go in holdover variable */
						if (state->current_phase_convergence_count > round(48.0 / ALPHA_ES_TRACKING))
							state->ready_to_go_in_holdover_class = true;
					} else {
						/* We cannot compute a new estimated equilibrium, logging why */
						log_warn("Estimated equilibrium will not be updated at this step, not updating convergence count as well");
						if (fabs(mean_phase_error) >= config->ref_fluctuations_ns){
							log_warn("Mean phase error is too high: %f >= %d", fabs(mean_phase_error), config->ref_fluctuations_ns);
							// After 30 tracking cycles convergence should be reached, attempting coarse step to center fine control as 400 fine step is 1 coarse step
							if (state-> tracking_count_test > 30){
								log_warn("Attempting coarse step");
								uint32_t new_coarse = input->coarse_setpoint;
								if ((uint32_t) state->fine_ctrl_value < 2000)
									new_coarse = input->coarse_setpoint + 1;
								else if ((uint32_t) state->fine_ctrl_value > 2800)
									new_coarse = input->coarse_setpoint - 1;
								log_info("Adjusting coarse value to %u", new_coarse);
								set_output(output, ADJUST_COARSE, new_coarse, 0);

								/* Reset Tracking state */
								set_state(state, TRACKING);
								state-> tracking_count_test = 0;

								/* Update estimated equilibrium ES to initial guess */
								if (dsc_config->estimated_equilibrium_ES != 0)
									state->estimated_equilibrium_ES = (float) dsc_config->estimated_equilibrium_ES;
								else
									state->estimated_equilibrium_ES = (float) state->estimated_equilibrium;

								if (config->oscillator_factory_settings)
									dsc_config->coarse_equilibrium_factory = new_coarse;
								else
									dsc_config->coarse_equilibrium = new_coarse;
								return 0;
							}
						}
						if (state->fine_ctrl_value < state->ctrl_range_fine[0]
							|| state->fine_ctrl_value > state->ctrl_range_fine[1])
							log_warn("Last fine ctrl value is out of middle range: %u", state->fine_ctrl_value);
						if (fabs(state->inputs[WINDOW_TRACKING - 1].phase_error - state->inputs[0].phase_error)
							>= (float) config->ref_fluctuations_ns)
							log_warn("Phase error diff is too high: %f >= %d",
								fabs(state->inputs[WINDOW_TRACKING - 1].phase_error - state->inputs[0].phase_error),
								config->ref_fluctuations_ns);
					}
					log_info("Estimated equilibrium with exponential smooth is %f",
						state->estimated_equilibrium_ES);
					log_debug("convergence_count: %d", state->current_phase_convergence_count);

					float r = TRACKING_PHASE_CONVERGENCE_REACTIVITY_MAX;
					if (state->current_phase_convergence_count <= round(12 / ALPHA_ES_TRACKING)){
						r = get_reactivity(
							fabs(mean_phase_error),
							config->ref_fluctuations_ns,
							config->reactivity_min,
							config->reactivity_max,
							config->reactivity_power
						);
					} else {
						r = get_reactivity(
							fabs(mean_phase_error),
							config->ref_fluctuations_ns,
							(int) TRACKING_PHASE_CONVERGENCE_REACTIVITY_MIN,
							(int) TRACKING_PHASE_CONVERGENCE_REACTIVITY_MAX,
							config->reactivity_power
						);
					}
					float react_coeff = - mean_phase_error / r;
					log_info("get_reactivity gives %f, react coeff is now %f", r, react_coeff);


					if (state->current_phase_convergence_count <= round(12 / ALPHA_ES_TRACKING)){
						ret = compute_fine_value(state, react_coeff, &state->fine_ctrl_value);
						if (ret != 0) {
							log_error("Error computing fine value");
							return ret;
						}
					}
					else{
						int delta_fine  = round(react_coeff/(MRO_FINE_STEP_SENSITIVITY * 1.E9));
						state->fine_ctrl_value  = (uint16_t) (state->estimated_equilibrium_ES + delta_fine);
					}

					log_debug("New fine control value: %u", state->fine_ctrl_value);


					/* If fine estimated equilibrium ES is in tolerance range */
					if ((uint32_t) round(state->estimated_equilibrium_ES) >= (uint32_t) FINE_MID_RANGE_MIN + config->fine_stop_tolerance &&
						(uint32_t) round(state->estimated_equilibrium_ES) <= (uint32_t) FINE_MID_RANGE_MAX - config->fine_stop_tolerance)
					{
						/* Check if we reached convergence enough time to switch to lock low resolution */
						if (state->current_phase_convergence_count > round(6.0 / ALPHA_ES_TRACKING)) {
							/* Update estimated equilibrium ES in discplining parameters */
							dsc_config->estimated_equilibrium_ES = (uint16_t) round(state->estimated_equilibrium_ES);
							/* Consider card as calibrated if user parameters are used */
							if (!od->minipod_config.oscillator_factory_settings) {
								dsc_config->calibration_valid = true;
								dsc_config->calibration_date = time(NULL);
							}
							/* Smooth convergence reached, adjust to estimated equilibrium smooth */
							log_info("Smoothing convergence reached");
							state->estimated_drift = react_coeff;
						}
					} else {
						/* Check if we did more that 2 convergence count threshold, if so adjust coarse and restart tracking */
						if (state->current_phase_convergence_count > 2 * round(6.0 / ALPHA_ES_TRACKING)) {
							log_warn("Estimated equilibrium is out of range !");
							uint32_t new_coarse = input->coarse_setpoint;
							if ((uint32_t) round(state->estimated_equilibrium_ES) < (uint32_t) FINE_MID_RANGE_MIN + config->fine_stop_tolerance)
								new_coarse = input->coarse_setpoint + 1;
							else if ((uint32_t) round(state->estimated_equilibrium_ES) > (uint32_t) FINE_MID_RANGE_MAX - config->fine_stop_tolerance)
								new_coarse = input->coarse_setpoint - 1;
							log_info("Adjusting coarse value to %u", new_coarse);
							set_output(output, ADJUST_COARSE, new_coarse, 0);

							/* Reset Tracking state */
							set_state(state, TRACKING);

							/* Update estimated equilibrium ES to initial guess */
							if (dsc_config->estimated_equilibrium_ES != 0)
								state->estimated_equilibrium_ES = (float) dsc_config->estimated_equilibrium_ES;
							else
								state->estimated_equilibrium_ES = (float) state->estimated_equilibrium;

							if (config->oscillator_factory_settings)
								dsc_config->coarse_equilibrium_factory = new_coarse;
							else
								dsc_config->coarse_equilibrium = new_coarse;
							return 0;
						}
					}

					/*
					 * Estimated equilibrium is in tolerance range and current_phase_convergence_count
					 * is below current_phase_convergence_count_threshold
					 * or we did not reach 2 convergence count
					 * We apply fine value if it is not out of range
					 */
					if (state->fine_ctrl_value >= FINE_RANGE_MIN + config->fine_stop_tolerance
						&& state->fine_ctrl_value <= FINE_RANGE_MAX - config->fine_stop_tolerance)
					{
						state->estimated_drift = react_coeff;
						set_output(output, ADJUST_FINE, state->fine_ctrl_value, 0);

						/* Do not add temperature value at the beginning of tracking */
						if (config->learn_temperature_table
							&& state->current_phase_convergence_count > round(6.0 / ALPHA_ES_TRACKING)
							&& fabs(mean_phase_error) <= config->ref_fluctuations_ns) {
							/* Add fine Estimated ES values into fine ciruclar buffer for temperature impact */
							log_debug("Add fine estimated ES");
							ret = add_fine_from_temperature(state->fine_estimated_es_buffer, state->estimated_equilibrium_ES, state->mRO_EP_temperature);
							if (ret != 0) {
								log_warn("Could not add data to fine_estimated_es_buffer\n");
							}
							ret = write_buffers_in_file(state->fine_estimated_es_buffer, state->fine_estimated_buffer_buffer_output_path);
							if (ret != 0) {
								log_error("Error writing temperature table in %s", state->fine_estimated_buffer_buffer_output_path);
							}
						}
					}
					else
					{
						log_warn("Control value is out of range, if convergence is not reached"
							" consider recalibration or other reactivity parameters");

						float stop_value;

						if (state->fine_ctrl_value < FINE_RANGE_MIN + config->fine_stop_tolerance)
							stop_value = FINE_RANGE_MIN + config->fine_stop_tolerance;
						else
							stop_value = FINE_RANGE_MAX - config->fine_stop_tolerance;

						ret = lin_interp(
							state->ctrl_points,
							state->ctrl_drift_coeffs,
							state->ctrl_points_length,
							X_INTERPOLATION,
							stop_value,
							&state->estimated_drift
						);
						log_info("Estimated drift is now %f", state->estimated_drift);

						if (ret < 0)
						{
							log_error("Error occured in lin_interp: %d", ret);
							return -1;
						}
						set_output(output, ADJUST_FINE, stop_value, 0);
					}
					return 0;
				} else {
					/* Phase error is superior to threshold, we need to do a phase jump */
					if (state->current_phase_convergence_count <= round(12.0 / ALPHA_ES_TRACKING)) {
						/* Phase jump needed */
						set_output(output, PHASE_JUMP, 0, input->phase_error.tv_nsec);
					} else {
						/* Reset tracking state if phase error too large after convergence*/
					    log_warn("Phase error too large, resetting Tracking state");
					    set_state(state, TRACKING);
						set_output(output, ADJUST_FINE, (uint32_t) round(state->estimated_equilibrium_ES), 0);
					}
					return 0;
				}
				break;
			default:
				log_error("Unhandled state %d", state->status);
				return -1;
			}
		/* else if gnss is unstable, apply estimated equilibrium to prevent reaching holdover on an unstable state */
		} else if (gnss_state == GNSS_UNSTABLE && mro50_lock_state && (phasemeter_status == PHASEMETER_BOTH_TIMESTAMPS || phasemeter_status == PHASEMETER_INIT)) {
			log_warn("Unstable GNSS: Applying estimated equilibrium");
			set_output(output, ADJUST_FINE, (uint32_t) round(state->estimated_equilibrium_ES), 0);
		/* else go into holdover mode if gnss is bad for 3 cycles */
		} else {
			/* Wait 3 cycles with bad GNSS before really going into holdover
			 * Only apply estimated equilibrium without really entering holdover
			 * which will loose any progress in the states convergence
			 */

			float fine_applied_in_holdover = state->estimated_equilibrium_ES;

			state->disciplining_ko_count++;
			if (state->disciplining_ko_count >= 3) {
				log_warn("HOLDOVER activated: GNSS data is not valid and/or oscillator's lock has been lost and/or phasemeter inconsistency");
				log_info("Applying estimated equilibrium until going out of holdover");
				if (state->status != HOLDOVER) {
					set_state(state, HOLDOVER);
				} else if(config->use_temperature_table) {
					/* Temperature compensation */
					const float dc = 0.75;
					float delta_temp_composite = (state->mRO_EP_temperature - state->holdover_mRO_EP_temperature) + dc*(input->temperature - state->mRO_EP_temperature);
					float temp_composite = state->holdover_mRO_EP_temperature + delta_temp_composite;
					if (fabs(delta_temp_composite) > 0.1) {
						float temp_up = ceil(temp_composite*4)/4. + 0.125;
						float temp_down = floor(temp_composite*4)/4. + 0.125;
						log_debug("temp_up = %.3f, temp_down = %.3f",temp_up,temp_down);
						float fine_temp_up = get_fine_from_table(state,temp_up);
						float fine_temp_down = get_fine_from_table(state,temp_down);
						float fine_holdover = get_fine_from_table(state, state->holdover_mRO_EP_temperature) > 0.0 ?
							get_fine_from_table(state, state->holdover_mRO_EP_temperature) :
							state->estimated_equilibrium_ES;

						float fine_composite = 0.0;
						if (fine_temp_up < 0.0 || fine_temp_down < 0.0) {
							/*
							 * At least one of the fine for tempetarure is not filled in temperature table
							 * No compensation is applied
							 */
							set_output(output, NO_OP, 0, 0);
							if (fine_temp_up < 0.0 || fine_temp_down < 0.0)
								log_warn("Learned temperature table is not filled for %.2f, No extra compensation is applied", temp_composite);
							return 0;
						} else {
							/* Temperature table is known at this temperature range, we can apply extra compensation */
							if (fabs(temp_up - temp_down) < 0.1) {
								/* As fine_temp_up and fine_temp_down are not negative, fine_composite cannot be negative */
								fine_composite = get_fine_from_table(state, temp_composite);
							} else {
								fine_composite = fine_temp_down + (temp_composite-temp_down)*(fine_temp_up-fine_temp_down)/(temp_up - temp_down);
							}

							float fine_temperature_compensated = state->estimated_equilibrium_ES + (fine_composite - fine_holdover);

							log_debug("Temperature Compensation: fine_temperature_compensated=%.2f,  state->estimated_equilibrium_ES=%.2f, fine_composite=%.2f,fine_holdover=%.2f",
								fine_temperature_compensated,
								state->estimated_equilibrium_ES,
								fine_composite,
								fine_holdover
							);
							if (fine_temperature_compensated >= FINE_RANGE_MIN && fine_temperature_compensated <= FINE_RANGE_MAX) {
								float delta_fine_temperature = fine_temperature_compensated - state->estimated_equilibrium_ES;

								float effective_coefficient = delta_fine_temperature/delta_temp_composite;
								log_debug("Temperature Compensation: state->estimated_equilibrium_ES=%.2f, delta_temp_composite=%.2f, delta_fine=%.2f, effective_coefficient=%.2f",
										state->estimated_equilibrium_ES,
										delta_temp_composite,
										delta_fine_temperature,
										effective_coefficient);
#define MAX_DELTA_FINE_COEFFICIENT 100.0
								if (fabs(effective_coefficient) > MAX_DELTA_FINE_COEFFICIENT) {
									delta_fine_temperature = MAX_DELTA_FINE_COEFFICIENT * delta_temp_composite * (effective_coefficient / fabs(effective_coefficient));
									log_warn("Strong effective coefficient, bounding delta_fine_temperature to %.2f", delta_fine_temperature);
								}
								fine_applied_in_holdover += delta_fine_temperature;
							} else {
								log_error("Inconsistent fine temperature compensated computed: %.2f", fine_temperature_compensated);
							}
						}
					}
				} else {
					log_info("Temperature Compensation: Using only mRo50 internal compensation");
				}
				log_debug("fine_applied_in_holdover is %.2f", fine_applied_in_holdover);
				log_debug("Temperature when entering holdover was %.2f", state->holdover_mRO_EP_temperature);

			} else {
				log_warn("Bad GNSS and/or Lock has been lost and/or Phasemeter inconsistency: Waiting 3 bad cycles before entering holdover (%d/3)", state->disciplining_ko_count);
			}

			set_output(output, ADJUST_FINE, (uint32_t) round(fine_applied_in_holdover), 0);
		}
	} else {
		set_output(output, NO_OP, 0, 0);
	}
	return 0;
}

static void update_temperature_mean_values(struct disciplining_parameters* dsc_params, struct algorithm_state *state)
{
	int i;
	/* Copy only range between 30 and 49 for now */
	for (i = 0; i < MEAN_TEMPERATURE_ARRAY_MAX; i ++) {
		if (compute_mean_value(&state->fine_estimated_es_buffer[i]) == 0) {
			dsc_params->temp_table.mean_fine_over_temperature[i] = round(state->fine_estimated_es_buffer[i].mean_fine * 10.0);
		} else {
			dsc_params->temp_table.mean_fine_over_temperature[i] = 0;
		}
	}
	return;
}

int od_get_disciplining_parameters(struct od *od, struct disciplining_parameters* dsc_params) {
	if (od == NULL) {
		log_error("Library context is null");
		return -1;
	}
	/* Update temperature table values stored in disciplining parameters */
	memcpy(dsc_params, &od->dsc_parameters, sizeof(struct disciplining_parameters));
	update_temperature_mean_values(dsc_params, &od->state);
	return 0;
}

struct calibration_parameters * od_get_calibration_parameters(struct od *od)
{
	int i;

	if (od == NULL)
	{
		log_error("Library context is null");
		return NULL;
	}
	struct disciplining_config *dsc_config = &(od->dsc_parameters.dsc_config);

	if (dsc_config->ctrl_nodes_length <= 0)
	{
		log_error("get_calibration_parameters: Length cannot be negative");
		return NULL;
	}

	struct calibration_parameters *calib_params = malloc(sizeof(struct calibration_parameters));
	if (calib_params == NULL)
	{
		log_error("Could not allocate memory to create calibration parameters data");
		return NULL;
	}

	calib_params->ctrl_points = malloc(dsc_config->ctrl_nodes_length * sizeof(uint16_t));
	if (calib_params->ctrl_points == NULL) {
		log_error("Could not allocate memory to create ctrl points in calibration parameters data");
		free(calib_params);
		calib_params = NULL;
		return NULL;
	}

	for (i = 0; i < dsc_config->ctrl_nodes_length; i++)
	{
		calib_params->ctrl_points[i] = od->state.ctrl_points[i];
	}

	calib_params->length = dsc_config->ctrl_nodes_length;
	calib_params->nb_calibration = od->minipod_config.nb_calibration;
	od->state.calib = true;
	return calib_params;
}

static void free_calibration(struct calibration_parameters *calib_params, struct calibration_results *calib_results)
{
	free(calib_params->ctrl_points);
	calib_params->ctrl_points = NULL;
	free(calib_params);
	calib_params = NULL;
	free(calib_results->measures);
	calib_results->measures = NULL;
	free(calib_results);
	calib_results = NULL;
	return;
}

void od_calibrate(struct od *od, struct calibration_parameters *calib_params, struct calibration_results *calib_results)
{
	struct disciplining_config *dsc_config;
	struct algorithm_state *state;
	int length;
	int i, j;
	int ret;

	if (od == NULL || calib_params == NULL || calib_results == NULL)
	{
		log_error("od_calibration: at least one input parameter is null");
		return;
	}
	dsc_config = &od->dsc_parameters.dsc_config;
	state = &od->state;

	if (calib_params->length != dsc_config->ctrl_nodes_length || calib_params->length != calib_results->length)
	{
		log_error("od_calibrate: length mismatch");
		free_calibration(calib_params, calib_results);
		return;
	}
	length = dsc_config->ctrl_nodes_length;

	if (calib_params->nb_calibration != calib_results->nb_calibration)
	{
		log_error("od_calibrate: nb_calibration mismatch");
		free_calibration(calib_params, calib_results);
		return;
	}

	/* Create array representing all the integers between 0 and n_calibration */
	float x[calib_params->nb_calibration];
	for (i = 0; i < calib_params->nb_calibration; i++)
		x[i] = (float) i;

	/* Update drift coefficients */
	log_debug("CALIBRATION: Updating drift coefficients:");
	for (i = 0; i < length; i++)
	{
		float v[calib_params->nb_calibration];
		for(j = 0; j < calib_params->nb_calibration; j++)
			v[j] = *(calib_results->measures + i * calib_params->nb_calibration + j);

		struct linear_func_param func_params;

		ret = simple_linear_reg(
			x,
			v,
			calib_params->nb_calibration,
			&func_params
		);


		if (ret < 0)
		{
			log_error("od_calibrate: error occured in simple_linear_reg, err %d", ret);
			free_calibration(calib_params, calib_results);
			return;
		}
		dsc_config->ctrl_drift_coeffs[i] = func_params.a;
		log_debug("\t[%d]: %f", i, dsc_config->ctrl_drift_coeffs[i]);
	}

	for (i = 0; i < length; i++)
		state->ctrl_drift_coeffs[i] = dsc_config->ctrl_drift_coeffs[i];

	ret = compute_fine_value(state, 0, &state->estimated_equilibrium);
	if (ret != 0) {
		log_error("Error computing fine value");
		return;
	}
	log_debug("Estimated equilibrium is now %d", state->estimated_equilibrium);
	log_debug("Resetting estimated equiblibrium exponential smooth");
	state->estimated_equilibrium_ES = (float) state->estimated_equilibrium;

	if (state->ctrl_points[length - 1] - state->ctrl_points[0] == 0)
	{
		log_error("Control points cannot have the same value !");
		free_calibration(calib_params, calib_results);
		return;
	}

	state->mRO_fine_step_sensitivity = 1E-9
		* ( dsc_config->ctrl_drift_coeffs[length - 1] - dsc_config->ctrl_drift_coeffs[0])
		/ ( state->ctrl_points[length - 1] - state->ctrl_points[0] );

	free_calibration(calib_params, calib_results);
	return;
}

void od_destroy(struct od **od)
{
	if (od == NULL || *od == NULL)
		return;
	free((*od)->state.ctrl_points);
	(*od)->state.ctrl_points = NULL;
	free((*od)->state.ctrl_drift_coeffs);
	(*od)->state.ctrl_drift_coeffs = NULL;
	free((*od)->state.inputs);
	(*od)->state.inputs = NULL;
	free(*od);
	*od = NULL;
}

int od_get_monitoring_data(struct od *od, struct od_monitoring *monitoring) {
	if (od == NULL || monitoring == NULL)
		return -1;

	monitoring->clock_class = clock_class_from_disciplining_state(od->state.status);
	monitoring->status = od->state.status;
	monitoring->ready_for_holdover = od->state.ready_to_go_in_holdover_class;
	if (od->state.current_phase_convergence_count >  round(1.0 / ALPHA_ES_TRACKING)) {
		monitoring->clock_class = CLOCK_CLASS_LOCK;
	}

	/* Special case: If we are in Holdover for more than 24H, set clock class to UNCALIBRATED */
	if (od->state.status == HOLDOVER
		&& (!od->state.ready_to_go_in_holdover_class
			|| time(NULL) - od->state.timestamp_entering_holdover > DAY_IN_SECONDS)
	) {
		monitoring->clock_class = CLOCK_CLASS_UNCALIBRATED;
	}

	switch(od->state.status) {
		case TRACKING: {
			monitoring->current_phase_convergence_count = od->state.current_phase_convergence_count;
			monitoring->valid_phase_convergence_threshold = round(6.0 / ALPHA_ES_TRACKING);
			break;
		}

		default: {
			monitoring->current_phase_convergence_count = 0;
			monitoring->valid_phase_convergence_threshold = -1;
		}
	}

	float progress = ((float) monitoring->current_phase_convergence_count / (float) monitoring->valid_phase_convergence_threshold) * 100.0;
	monitoring->convergence_progress = (progress < 100.0 ? progress : 100.0);
	return 0;
}
