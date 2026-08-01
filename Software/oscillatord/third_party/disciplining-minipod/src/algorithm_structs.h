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
 * @file algorithm_structs.h
 * @brief Header containing all algorithm's structures.
 */
#ifndef ALGORITHM_STRUCTS_H
#define ALGORITHM_STRUCTS_H

#include <math.h>
#include <stdint.h>
#include <oscillator-disciplining/oscillator-disciplining.h>

/** mRO base fine step sensitivity */
#define MRO_FINE_STEP_SENSITIVITY -3.E-12
/** mRO base coarse step sensitivity */
#define MRO_COARSE_STEP_SENSITIVITY 1.24E-9

/** Minimum possible value of coarse control */
#define COARSE_RANGE_MIN 0
/** Maximum possible value of coarse control */
#define COARSE_RANGE_MAX 4194303
/** Minimum possible value of fine control */
#define FINE_RANGE_MIN 0
/** Maximum possible value of fine control */
#define FINE_RANGE_MAX 4800
/** Minimum possible value of fine control used for calibration*/
#define FINE_MID_RANGE_MIN 1600
/** Maximum possible value of fine control used for calibration*/
#define FINE_MID_RANGE_MAX 3200
/**
 * Maximum drift coefficient
 * (Fine mid value * abs(mRO base fine step sensitivity) in s/s)
 */
#define DRIFT_COEFFICIENT_ABSOLUTE_MAX 7.2

/**
 * @brief Number of temperature steps, starting at 20°C
 * Each step is a 0.25° range
 */
#define TEMPERATURE_STEPS 160

struct fine_circular_buffer {
    float buffer[CIRCULAR_BUFFER_SIZE];
    float mean_fine;
    uint8_t read_index;
    uint8_t write_index;
    uint8_t buffer_length;
};

/**
 * @struct algorithm_inputs
 * @brief Algorithm inputs for Minipod
 */
struct algorithm_input {
	/* Phase error corrected with Quantization Error */
	float phase_error;
	/* Flag indicating GNSS data are valid */
	bool valid;
	/* Flag indicating mRO50 is locked */
	bool lock;
	/* Enum indicating Phasemeter state*/
	enum PHASEMETER_STATUS phasemeter_status;
};

/**
 * @struct algorithm_state
 * @brief Algorithm data stored in od context
 */
struct algorithm_state {
	struct algorithm_input *inputs;
	/** State value */
	enum disciplining_state status;
	/** Frequency adjustement for one value on the fine control */
	double mRO_fine_step_sensitivity;
	/** Frequency adjustement for one value on the coarse control */
	double mRO_coarse_step_sensitivity;
	/** Indicate a calibration has been requested either by software or
	 * user and is on going
	 */
	bool calib;
	/** Range of possible coarse values applicable to the oscillator */
	uint32_t ctrl_range_coarse[2];
	/** Range of possible fine values applicable to the oscillator */
	uint16_t ctrl_range_fine[2];
	/** Number of ctrl_points */
	int ctrl_points_length;
	/** Fine values for which a drift coefficient is computed */
	float *ctrl_points;
	/** Ctrl drift coeffs */
	float *ctrl_drift_coeffs;
	/** Median value of the fine range */
	uint16_t fine_mid;
	/** Fine control value computed by the algorithm */
	uint16_t fine_ctrl_value;
	/** Counts how many inputs has been given to minipod */
	uint16_t od_inputs_count;
	/** Estimated equilibrium value of the fine control */
	uint16_t estimated_equilibrium;
	/** Exponential Smooth of the estimated equilibrium */
	float estimated_equilibrium_ES;
	/** Estimated drift based on last fine control value and drift coefficients */
	float estimated_drift;
	/** Counter of number of cycles where phase error is below reference during tracking phase */
	uint16_t current_phase_convergence_count;
	/** Counter for tracking cycles */
	uint16_t tracking_count_test;
	float previous_freq_error;
	int disciplining_ko_count;
	/* Timestamp at which we entered holdover */
	time_t timestamp_entering_holdover;
	/** MRO50's smoothed temperature over disciplining */
	float mRO_EP_temperature;
	/** MRO50's smoothed temperature when entering holdover */
	float holdover_mRO_EP_temperature;
	/* Buffer to store fine estimated ES in temperature ranges */
	struct fine_circular_buffer fine_estimated_es_buffer[TEMPERATURE_STEPS];
	char fine_estimated_buffer_buffer_output_path[256];
	/* Indicate wether going in Holdover state should display Holdover Clock Class or Uncalibrated */
	bool ready_to_go_in_holdover_class;
};

#endif /* ALGORITHM_STRUCTS_H */
