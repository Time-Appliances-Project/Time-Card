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
 * @file checks.h
 * @brief Functions used to perform check over input data of the algorithm
 * @date 2022-05-06
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef MINIPOD_CHECKS_H
#define MINIPOD_CHECKS_H

#include <oscillator-disciplining/oscillator-disciplining.h>

#include "algorithm_structs.h"

/**
 * @enum gnss_state
 * @brief GNSS State over all inputs of the algorithm
 */
enum gnss_state {
    GNSS_KO,
    GNSS_UNSTABLE,
    GNSS_OK
};

enum gnss_state check_gnss_valid_over_cycle(struct algorithm_input *inputs, int length);
bool check_lock_over_cycle(struct algorithm_input *inputs, int length);
enum PHASEMETER_STATUS check_phasemeter_status_over_cycle(struct algorithm_input *inputs, int length);
bool check_no_outlier(struct algorithm_input *inputs, int length, float mean_phase_error, int ref_fluctuation_ns);

#endif /* MINIPOD_CHECKS_H */
