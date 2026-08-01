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
 * @mainpage liboscillator-disciplining
 *
 * C library for disciplining an oscillator using miniCod algorithm.
 *
 * @see oscillator-disciplining.h for API documentation.
 * @see algorithm_structs.h for algorithm's structures definitions.
 */

/**
 * @file oscillator-disciplining.h
 * @brief liboscillator-disciplining's API header file.
 *
 * liboscillator-disciplining is a small library responsible of abstracting
 * disciplining algorithms used for an oscillator for which we want to control
 * the frequency.
 *
 */

#ifndef INCLUDE_OSCILLATOR_DISCIPLINING_OSCILLATOR_DISCIPLINING_H_
#define INCLUDE_OSCILLATOR_DISCIPLINING_OSCILLATOR_DISCIPLINING_H_

#include <time.h>
#include <stdbool.h>
#include <inttypes.h>

/**
 * @def SETTLING_TIME
 * @brief Time oscillator needs to apply new control values
 */
#define SETTLING_TIME 6

/**
 * @def OD_ERR_MSG_LEN
 * @brief Required size an error message buffer must have.
 */
#define OD_ERR_MSG_LEN 0x400

/**
 * @def PS_IN_NS
 * @brief Number of picoseconds in one nanosecond
 */
#define PS_IN_NS 1000


/**
 * @struct minipod_config
 * @brief Minipod configuration
 */
struct minipod_config {
	/** Used to filter phase */
	int ref_fluctuations_ns;
	/** Threshold above which as phase jump is requested */
	int phase_jump_threshold_ns;
	/** Phasemeter's resolution in ns */
	int phase_resolution_ns;
	/** Enable debug logs */
	int debug;
	/** Minimal reactivity */
	int reactivity_min;
	/** Maximal reactivity */
	int reactivity_max;
	/** Power used in reactivity computation */
	int reactivity_power;
	/** number of phase error measures for one control node
	 * when doing a calibration
	 */
	int nb_calibration;
	/**
	 * Set tolerance range to check if fine equilibrium is inside this range
	 * to validate calibration.
	 * After a calibration fine equilibrium point must be between
	 * ctrl_range_fine[0] + fine_stop_tolerance and
	 * ctrl_range_fine[1] - fine_stop_tolerance.
	 */
	int fine_stop_tolerance;
	/** Maximum difference allowed when changin coarse value */
	int max_allowed_coarse;
	/** Triggers calibration when starting the program */
	bool calibrate_first;
	/** Define wether to use factory settings or not */
	bool oscillator_factory_settings;
	/** Set to learn temperature table during disciplining */
	bool learn_temperature_table;
	/** Wether to use temperature table or not for enhanced temperature compensation */
	bool use_temperature_table;
	/** file path to store temperature fine table */
	const char *fine_table_output_path;
};

/**
 * Maximum number of points that can be stored in the memory of the card
 */
#define CALIBRATION_POINTS_MAX 10

#define MEAN_TEMPERATURE_ARRAY_MAX 160

/**
 * @brief Number of steps by degrees (1/ 0.25° range)
 */
#define STEPS_BY_DEGREE 4

#define MIN_TEMPERATURE 20.0
#define MAX_TEMPERATURE 60.0

#define CIRCULAR_BUFFER_SIZE 25

#define MIN_VALUES_FOR_MEAN CIRCULAR_BUFFER_SIZE

#define DISCIPLINING_CONFIG_VERSION 1
#define disciplining_parameters disciplining_parameters_V_1
#define disciplining_config disciplining_config_V_1
#define temperature_table temperature_table_V_1

#define HEADER_MAGIC 'O'
/**
 * @struct disciplining_parameters_V0
 * @brief Disciplining parameters corresponding to mRO50 device disciplined
 *
 */

struct __attribute__((__packed__)) disciplining_parameters_V_0 {
	/**
	 * Array containing the control node, in percentage
	 * value of the control range.
	 * Array contains ctrl_nodes_length valid values.
	 */
	float ctrl_load_nodes[CALIBRATION_POINTS_MAX];
	/**
	 * Array of drift coefficients for each control node.
	 * Array contains ctrl_nodes_length valid values.
	 */
	float ctrl_drift_coeffs[CALIBRATION_POINTS_MAX];
	/** Equilibrium Coarse value define during calibration */
	/**
	 * Array containing the control node, in percentage
	 * value of the control range.
	 * Array contains ctrl_nodes_length_factory valid values.
	 */
	float ctrl_load_nodes_factory[3];
	/**
	 * Array of drift coefficients for each control node.
	 * Array contains ctrl_nodes_length_factory valid values.
	 */
	float ctrl_drift_coeffs_factory[3];
	/** Equilibrium Coarse value for factory_settings */
	int32_t coarse_equilibrium_factory;
	int32_t coarse_equilibrium;
	/** Date at which calibration has been made */
	time_t calibration_date;
	/** Factory Settings that can be used with any mRO50 */
	/** Number of control nodes in ctrl_load_nodes_factory */
	uint8_t ctrl_nodes_length_factory;
	/** Number of control nodes in ctrl_load_nodes */
	uint8_t ctrl_nodes_length;
	/** Indicate wether calibration parameters are valid */
	bool calibration_valid;
	uint8_t __padding0;
	/** estimated_equilibrium ES from previous tracking phases */
	uint16_t estimated_equilibrium_ES;
	uint16_t mean_fine_over_temperature[MEAN_TEMPERATURE_ARRAY_MAX];
};

struct __attribute__((__packed__)) disciplining_config_V_1 {
	uint8_t header;
	uint8_t version;
	uint8_t __pad0[2];
	/**
	 * Array containing the control node, in percentage
	 * value of the control range.
	 * Array contains ctrl_nodes_length valid values.
	 */
	float ctrl_load_nodes[CALIBRATION_POINTS_MAX];
	/**
	 * Array of drift coefficients for each control node.
	 * Array contains ctrl_nodes_length valid values.
	 */
	float ctrl_drift_coeffs[CALIBRATION_POINTS_MAX];
	/** Equilibrium Coarse value define during calibration */
	/**
	 * Array containing the control node, in percentage
	 * value of the control range.
	 * Array contains ctrl_nodes_length_factory valid values.
	 */
	float ctrl_load_nodes_factory[3];
	/**
	 * Array of drift coefficients for each control node.
	 * Array contains ctrl_nodes_length_factory valid values.
	 */
	float ctrl_drift_coeffs_factory[3];
	/** Equilibrium Coarse value for factory_settings */
	int32_t coarse_equilibrium_factory;
	int32_t coarse_equilibrium;
	uint8_t __pad1[4];
	/** Date at which calibration has been made */
	time_t calibration_date;
	/** Factory Settings that can be used with any mRO50 */
	/** Number of control nodes in ctrl_load_nodes_factory */
	uint8_t ctrl_nodes_length_factory;
	/** Number of control nodes in ctrl_load_nodes */
	uint8_t ctrl_nodes_length;
	/** Indicate wether calibration parameters are valid */
	bool calibration_valid;
	uint8_t __pad2;
	/** estimated_equilibrium ES from previous tracking phases */
	uint16_t estimated_equilibrium_ES;
};

struct __attribute__((__packed__)) temperature_table_V_1 {
	uint8_t header;
	uint8_t version;
	uint16_t mean_fine_over_temperature[MEAN_TEMPERATURE_ARRAY_MAX];
};

struct __attribute__((__packed__)) disciplining_parameters_V_1 {
	struct disciplining_config_V_1 dsc_config;
	struct temperature_table_V_1 temp_table;
};

/**
 * @enum PHASEMETER_STATUS
 * @brief Enumeration of the possible phasemeter status
 */
enum PHASEMETER_STATUS {
	PHASEMETER_INIT,
	PHASEMETER_NO_GNSS_TIMESTAMPS,
	PHASEMETER_NO_ART_INTERNAL_TIMESTAMPS,
	PHASEMETER_BOTH_TIMESTAMPS,
	PHASEMETER_ERROR
};

/**
 * @struct od_input
 * @brief Structure containing all the input parameters for the disciplining
 * algorithm.
 */
struct od_input {
	/** temperature, only used for logging. */
	double temperature;
	/** phase error measured between the oscillator and the GNSS */
	struct timespec phase_error;
	/** Fine adjustement setpoint */
	uint32_t fine_setpoint;
	/** Coarse adjustement setpoint */
	int32_t coarse_setpoint;
	/** Quantization Error **/
	int32_t qErr;
	/** Calibration requested by software of user */
	bool calibration_requested;
	/** is mRO locked */
	bool lock;
	/** is GNSS available (and hence, is the phase error meaningful) */
	bool valid;
	/** current phasemeter status */
	enum PHASEMETER_STATUS phasemeter_status;
	/** Survey in successfully completed */
	bool survey_completed;
};

/**
 * @enum output_action
 * @brief Enumeration of the possible action that must be done to control
 * the oscillator
 */
enum output_action {
	NO_OP,
	PHASE_JUMP,
	ADJUST_FINE,
	ADJUST_COARSE,
	CALIBRATE,
	SAVE_DISCIPLINING_PARAMETERS,
	NUM_ACTIONS,
};

/**
 * @struct od_output
 * @brief Structure which will be fed with the algorithm's outputs after
 * returning from od_process.
 */

struct od_output {
	/** frequence adjustment value, ranges depend wether concerning
	 * fine or coarse alignement:
	 * coarse: [0, 4194303]
	 * fine: [1600, 3200]
	 */
	uint32_t setpoint;
	/** Indicate action that should be done */
	enum output_action action;
	/** value of the phase jump */
	int32_t value_phase_ctrl;
};

/** Algorithm state value. */
enum disciplining_state {
	/** Oscillator warm up State */
	WARMUP,
	/** Initialization State */
	INIT,
	/** Quick convergence phase, tracking phase error to reach 0 */
	TRACKING,
	/** Holdover state, when gnss data is not valid */
	HOLDOVER,
	/** Calibration state, when drift coefficients are computed */
	CALIBRATION,
	disciplining_state_count
};

const char *cstring_from_disciplining_state(enum disciplining_state state);

/**
 * @struct od
 * @brief Opaque library context.
 */
struct od;

/**
 * @struct calibration_parameters
 * @brief structure used to pass parameters for mRO50's calibration
 */
struct calibration_parameters {
	/** control points used for the calibration */
	uint16_t *ctrl_points;
	/** Number of control points */
	int length;
	/** For each control points, nb_calibration measures
	 * of the phase error must be done
	 */
	int nb_calibration;
};

/**
 * @struct calibration_results
 * @brief structure containing the results of a calibration done
 * from calibration_parameters structures.
 */
struct calibration_results {
	/** all results are stored in timespec double array of pointer
	 * calib_results can be represented as
	 * a double array of size length * nb_calibration
	*/
	float *measures;
	/** Number of control points.
	 * Should be equal to calibration_parameters.length
	 */
	int length;
	/** For each control points, number of phase error measures done.
	 * Should be equal to calibration_parameters.nb_calibration
	 */
	int nb_calibration;
};


/**
 * @brief Creates a liboscillator-disciplining context.
 * @param minipod_config Algorithm parameters
 * @param disciplining_config Disciplining parameters specific to mRO50 being disciplined
 * @param err_msg Char buffer of size OD_ERR_MSG_LEN which will contain
 * additional human readable information on failure.
 * @return Context newly created, which must be destroyed by calling
 * od_destroy().
 */
struct od *od_new_from_config(struct minipod_config *minipod_config, struct disciplining_parameters *disciplining_config, char err_msg[OD_ERR_MSG_LEN]);
/**
 * @brief Processes input data using the disciplining algorithm.
 * @param od Library context.
 * @param input Input parameters fed into the algorithm.
 * @param output Output data produced by the algorithm step ran.
 * @return 0 on success, errno-compatible negative value or error.
 */
int od_process(struct od *od, const struct od_input *input,
		struct od_output *output);



/**
 * @brief Output disciplining parameters used by minipod.
 * @param od Library context.
 * @param disciplining_parameters pointer to output disciplining parameters.
 * @return int, 0 on success.
 */
int od_get_disciplining_parameters(struct od *od, struct disciplining_parameters *disciplining_parameters);

/**
 * @brief Outputs parameters needed for the mRO50 calibration.
 * @param od Library context.
 * @return struct calibration_parameters.
 */
struct calibration_parameters * od_get_calibration_parameters(struct od *od);

/**
 * @brief Process calibration results to compute phase time and ctrl coeffs.
 * @param od Library context.
 * @param calib_params calibration parameters obtained from od_get_calibration_paramters.
 * @param calib_results calibration measures done by the daemon.
 * @return 0 on success, errno-compatible negative value or error.
 */

void od_calibrate(struct od *od, struct calibration_parameters *calib_params, struct calibration_results *calib_results);

/**
 * @brief Destroys a context previously created by a call to od_new().
 * @param od Context to destroy, put to NULL in output, can be NULL or point to
 * NULL.
 */
void od_destroy(struct od **od);

enum clock_class {
	CLOCK_CLASS_UNCALIBRATED,
	CLOCK_CLASS_CALIBRATING,
	CLOCK_CLASS_HOLDOVER,
	CLOCK_CLASS_LOCK,
	clock_class_count
};

const char      *cstring_from_clock_class(enum clock_class);
enum clock_class clock_class_from_disciplining_state(enum disciplining_state);

struct od_monitoring {
	enum disciplining_state status;
	enum clock_class clock_class;
	int current_phase_convergence_count;
	int valid_phase_convergence_threshold;
	float convergence_progress;
	bool ready_for_holdover;
};

int od_get_monitoring_data(struct od *od, struct od_monitoring *monitoring);

#endif /* INCLUDE_OSCILLATOR_DISCIPLINING_OSCILLATOR_DISCIPLINING_H_ */
