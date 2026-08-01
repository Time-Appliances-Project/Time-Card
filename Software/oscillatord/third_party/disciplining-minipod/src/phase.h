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
 * @file phase.h
 * @brief Functions to compute mean phase error and frequency error
 * @date 2022-05-06
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef MINIPOD_PHASE_H
#define MINIPOD_PHASE_H

#include <oscillator-disciplining/oscillator-disciplining.h>

#include "algorithm_structs.h"
#include "utils.h"

int compute_phase_error_mean(struct algorithm_input *inputs, int length, float *mean_phase_error);
int compute_frequency_error(struct algorithm_input *inputs, int length, struct linear_func_param *func);

#endif /* MINIPOD_PHASE_H */
