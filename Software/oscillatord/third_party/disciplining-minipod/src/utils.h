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

#ifndef UTILS_H
#define UTILS_H
/**
 * @file utils.h
 * @brief Provide mathematical functions to compute linear regression and linear interpolation.
 *
 */

#include <stdbool.h>
#include <stdio.h>

/** Define to use for interp_value of lin_interp function */
#define X_INTERPOLATION true
/** Define to use for interp_value of lin_interp function */
#define Y_INTERPOLATION false

/**
 * @struct linear_func_param
 * @brief Description of a linear function such as y = a*x + b
 */
struct linear_func_param {
	double a;
	double b;
	double a_std;
	double b_std;
	double R2;
	double t0;
};

/**
 * @brief Simple linear regression
 * @param x Array of values corresponding to the abscissa
 * @param y Array of values corresponding to ordinate
 * @param length Length of both arrays
 * @param func_params Output structure of the linear regression
 * @return 0 in case of success, else errno
 */
int simple_linear_reg(float x[], float y[], int length, struct linear_func_param * func_params);
/**
 * @brief Linear interpolation
 * @param x Array of values corresponding to the abscissa
 * @param y Array of values corresponding to ordinate
 * @param length Length of both arrays
 * @param x_interp If true, do X interpolation, else a Y interpolation
 * @param interp_value Depending on x_interp, Y value for which to find the X value, or vice versa
 * @param interp_result output value of the linear interpolation
 * @return 0 in case of success, else errno
 */

int lin_interp(float x[], float y[], int length, bool x_interp, float interp_value, float *interp_result);

void file_cleanup(FILE **f);
void string_cleanup(char **s);
void fd_cleanup(int *fd);

#endif /* UTILS_H */
