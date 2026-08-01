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

#include "utils.h"
#include "log.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <io.h>
#define close _close
#else
#include <unistd.h>
#endif

static float sum(float values[], int length) {
	float sum = 0.0;

	if (length <= 0) {
		return 0.0;
	}

	for(int i = 0; i < length; i ++) {
		sum += values[i];
	}

	return sum;
}

/*
 * Return the index where to insert item x in list a, assuming a is sorted.
 * The return value i is such that all e in a[:i] have e <= x, and all e in
 * a[i:] have e > x.
 */
static int bisect_right(float values[], int length, float x) {
	int low = 0;
	int step = 0;
	int hi = length;
	if (hi < 0) {
		log_error("bisect_right: length cannot be negative!");
		return -EINVAL;
	}

	while(low < hi) {
		step = (int) (low + hi) / 2;
		if (x < values[step])
			hi = step;
		else
			low = step + 1;
	}
	return low;
}

int simple_linear_reg(float x[], float y[], int length, struct linear_func_param * func_params) {
	float mean_x;
	float mean_y;
	int i;
	
	if (length <= 0) {
		log_error("simple_linear_red: length cannot be negative");
		return -EINVAL;
	}

	mean_x = sum(x, length) / length;
	mean_y = sum(y, length) / length;

	float xy[length];
	float xx[length];
	for (i = 0; i < length; i++) {
		xy[i] = x[i] * (y[i] - mean_y);
		xx[i] = x[i] * (x[i] - mean_x);

		if (xy[i] == HUGE_VAL || xx[i] == HUGE_VAL) {
			log_error("HUGE_VAL detected ! xy[%d] is %f and xx[%d] is %f", i, xy[i], i, xx[i]);
			return -ERANGE;
		}
	}
	float sxy = sum(xy, length);
	float sxx = sum(xx, length);
	if (sxx == 0.0) {
		log_error("sxx is equal to 0");
		return -EINVAL;
	}
	func_params->a = sxy / sxx;
	func_params->b = mean_y - func_params->a * mean_x;

	float y2[length];
	for (i = 0; i < length; i++) {
		y2[i] = y[i]*y[i];
	}

	//Sum of squares total
	float sst = sum(y2,length) - length*mean_y*mean_y;

	//Sum of squares regression
	float ssr = func_params->a * sxy;

	//Sum of squares errors
	float sse = sst - ssr;

	func_params->R2 = ssr / sst;
	float sigma2 = sse/(length-2);

	func_params->a_std = sqrt(sigma2 / sxx);
	func_params->b_std = sqrt(sigma2 * (1/(float)length + pow(mean_x,2)/sxx));

	// t-test for null slope hypothesis
	func_params->t0 = fabs(func_params->a/sqrt(sigma2/sxx));

	return 0;
}

/* piece-wise linear interpolation/extrapolation for given x and y array in ascending order
 * x_interp: allows to choose between x interpolation or y one.
 */
int lin_interp(float x[], float y[], int length, bool x_interp, float interp_value, float *interp_result) {
	float slopes[length - 1];
	int index;
	if (length < 0) {
		log_error("lin_interp: length cannot be negative (value is %d)", length);
		return -EINVAL;
	}

	for (int i = 0; i < length -1; i++) {
		if (x[i+1] - x[i] == 0.0) {
			log_error("difference between x values at %d and %d is null", i, i+1);
			return -EINVAL;
		}
		else
			slopes[i] = (y[i+1] - y[i]) / (x[i+1] - x[i]);
	}

	if (x_interp) {
		/* X interpolation */
		if (interp_value >= x[length - 1])
			index = length - 2;
		else {
			index = bisect_right(x, length, interp_value) - 1;
			if (index <= 0)
				index = 0;
		}
		
		*interp_result = y[index] + slopes[index] * (interp_value - x[index]);
	} else {
		/* Y interpolation */
		if (interp_value >= y[length - 1])
			index = length - 2;
		else {
			index = bisect_right(y, length, interp_value) - 1;
			if (index <= 0)
				index = 0;
		}

		if (slopes[index] == 0.0)
			*interp_result = x[index];
		else
			*interp_result = x[index] + (interp_value - y[index]) / slopes[index];
	}

	return 0;
}


void file_cleanup(FILE **f)
{
	if (f == NULL || *f == NULL)
		return;

	fclose(*f);
	*f = NULL;
}

void string_cleanup(char **s)
{
	if (s == NULL || *s == NULL)
		return;

	free(*s);
	*s = NULL;
}

void fd_cleanup(int *fd)
{
	if (fd == NULL)
		return;

	close(*fd);
	*fd = -1;
}

