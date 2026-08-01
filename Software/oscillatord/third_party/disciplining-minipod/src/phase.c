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
 * @file phase.c
 * @brief Functions to compute mean phase error and frequency error
 * @date 2022-05-06
 *
 * @copyright Copyright (c) 2022
 *
 */
#include <math.h>

#include "phase.h"

#include "log.h"

/**
 * @brief Compute mean phase error
 *
 * @param inputs array of input data containing gnss valid flag
 * @param length array length
 * @param mean_phase_error pointer where mean phase error will be stored
 * @return int 0 in success else -1
 */
int compute_phase_error_mean(struct algorithm_input *inputs, int length, float *mean_phase_error)
{
	float sum_phase_error = 0.0;
	int i;

	if (inputs == NULL)
		return -1;
	if (length <= 0)
		return -1;

	for (i = 0; i < length; i ++)
		sum_phase_error += inputs[i].phase_error;
	*mean_phase_error = sum_phase_error / length;
	log_debug("Mean Phase error: %f", *mean_phase_error);

	return 0;
}

/**
 * @brief Compute frequency error
 *
 * @param inputs array of input data containing gnss valid flag
 * @param length array length
 * @param func parameters of the linear function
 * @return int
 */
int compute_frequency_error(struct algorithm_input *inputs, int length, struct linear_func_param *func)
{
	if (!inputs || !func)
		return -1;

	float x[length];
	float y[length];
	int ret, i;

	for (i = 0; i < length; i++) {
		x[i] = (float) i;
		y[i] = inputs[i].phase_error;
	}

	ret = simple_linear_reg(x, y, length, func);
	if (ret != 0) {
		return -1;
	}
	return 0;
}
