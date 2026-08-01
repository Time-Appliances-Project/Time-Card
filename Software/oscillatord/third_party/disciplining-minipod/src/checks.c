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
 * @file checks.c
 * @brief Functions used to perform check over input data of the algorithm
 * @date 2022-05-06
 *
 * @copyright Copyright (c) 2022
 *
 */
#include <math.h>

#include "checks.h"

#include "algorithm_structs.h"
#include "log.h"

#define GNSS_INVALID_MAX_STREAK 10

/**
 * @brief Define wether gnss is considered as valid over a cycle of length steps
 * by checking the gnss valid flag each input has.
 * GNSS is considered Ok if all valid flags are true over cycle
 * GNSS is considered Unstabled if less than 4 valid flags are flase
 * and the maximum streak of false values over the cycle is inferior to 10
 * GNSS is considered KO otherwise
 *
 * @param inputs array of input data containing gnss valid flag
 * @param length array length
 * @return enum gnss_state Indicate GNSS is considered OK, UNSTABLE or KO
 */
enum gnss_state check_gnss_valid_over_cycle(struct algorithm_input *inputs, int length)
{
	int i;
	int gnss_invalid_counter = 0;
	int gnss_invalid_max_streak = 0;
	int gnss_invalid_current_streak = 0;
	bool previous_gnss_state;

	if (inputs == NULL)
		return GNSS_KO;
	if (length <= 0)
		return GNSS_KO;

	if (!inputs[0].valid) {
		gnss_invalid_counter++;
		gnss_invalid_current_streak = 1;
		gnss_invalid_max_streak = 1;
	}
	previous_gnss_state = inputs[0].valid;

	for (i = 1; i < length; i ++) {
		if (!inputs[i].valid) {
			gnss_invalid_counter++;
			if (!previous_gnss_state) {
				gnss_invalid_current_streak++;
			} else {
				gnss_invalid_current_streak = 1;
			}
		}
		if (gnss_invalid_max_streak < gnss_invalid_current_streak) {
			gnss_invalid_max_streak = gnss_invalid_current_streak;
		}
		previous_gnss_state = inputs[i].valid;
	}

	log_trace("check_gnss_valid_over_cycle: gnss_invalid_counter: %d, gnss_invalid_max_streak: %d, ", gnss_invalid_counter, gnss_invalid_max_streak);

	if (gnss_invalid_counter == 0)
		return GNSS_OK;
	else if (gnss_invalid_counter < 4 && gnss_invalid_max_streak < GNSS_INVALID_MAX_STREAK)
		return GNSS_UNSTABLE;
	else
		return GNSS_KO;
}

/**
 * @brief Define wether oscillator lock status is valid over a cycle
 * by checking lock status each input has
 *
 * @param inputs array of input data containing gnss valid flag
 * @param length array length
 * @return Lock is valid on all inputs or not
 */
bool check_lock_over_cycle(struct algorithm_input *inputs, int length)
{
	bool lock_valid = true;
	int i;

	if (inputs == NULL)
		return false;
	if (length <= 0)
		return false;

	for (i = 0; i < length; i ++)
		lock_valid = lock_valid & inputs[i].lock;
	return lock_valid;
}

/**
 * @brief Define phasemeter status over a cycle
 * by checking phasemeter status each input has
 *
 * @param inputs array of input data containing phasemeter status
 * @param length array length
 * @return phasemeter status over cycle is the last phasemeter status encountered in the windows
 */
enum PHASEMETER_STATUS check_phasemeter_status_over_cycle(struct algorithm_input *inputs, int length)
{
	enum PHASEMETER_STATUS phasemeter_status = PHASEMETER_ERROR;

	if (inputs == NULL)
		return false;
	if (length <= 0)
		return false;

	phasemeter_status = inputs[length-1].phasemeter_status;
	return phasemeter_status;
}

/**
 * @brief Check for each input that the absolute difference between input's phase error
 * and mean phase error is inferior to ref_fluctuation_ns
 *
 * @param inputs array of input data containing gnss valid flag
 * @param length array length
 * @param mean_phase_error Mean phase error
 * @param ref_fluctuation_ns Fluctuaction reference
 * @return true
 * @return false
 */
bool check_no_outlier(struct algorithm_input *inputs, int length, float mean_phase_error, int ref_fluctuation_ns)
{
	int i;

	if (inputs == NULL)
		return false;
	if (length <= 0)
		return false;

	for (i = 0; i < length; i ++) {
		if (fabs(inputs[i].phase_error - mean_phase_error) > 1.5*ref_fluctuation_ns) {
			log_warn("Outlier detected at index %d", i);
			return false;
		}
	}
	return true;
}
