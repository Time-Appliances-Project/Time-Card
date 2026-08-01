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

#include <oscillator-disciplining/oscillator-disciplining.h>

#include <assert.h>
#include <stdio.h>

#include "../src/compare_floats.h"

struct minipod_config minipod_config = {
    .ref_fluctuations_ns = 30,
    .phase_jump_threshold_ns = 100,
    .phase_resolution_ns = 5,
    .debug = 1,
    .reactivity_min = 20,
    .reactivity_max = 80,
    .reactivity_power = 4,
    .nb_calibration = 30,
    .fine_stop_tolerance = 200,
    .max_allowed_coarse = 30,
    .calibrate_first = false,
    .oscillator_factory_settings = true
};

struct disciplining_parameters disciplining_parameters = {
    .dsc_config = {
        .ctrl_load_nodes = { 0.0, 0.25, 0.75 },
        .ctrl_drift_coeffs = { 1.21, 0.000001, -1.21 },
        .ctrl_load_nodes_factory = { 0.0, 0.5, 1.0 },
        .ctrl_drift_coeffs_factory = { 1.2, 0.0, -1.2 },
        .coarse_equilibrium_factory = -1,
        .coarse_equilibrium = -1,
        .calibration_date = 0,
        .ctrl_nodes_length_factory = 3,
        .ctrl_nodes_length = 3,
        .calibration_valid = 0,
    }
};

/**
 * TEST MINIPOD LIBRARY
 * Test the interface of the oscillator disciplining library
 */
int main(int argc, char *argv[])
{
    char err_msg[OD_ERR_MSG_LEN];
    struct od *od;

    printf("*** TEST VALID CONFIG ***\n");
    od = od_new_from_config(&minipod_config, &disciplining_parameters, err_msg);
    assert(od != NULL);
    struct calibration_parameters *calibration_parameters;

    printf("*** TEST GET FACTORY DISCIPLINING PARAMETERS ***\n");
    /* Test od_get_calibration_parameters */
    struct disciplining_parameters dsc_params;

    int ret = od_get_disciplining_parameters(od, &dsc_params);
    struct disciplining_config *current_config = &dsc_params.dsc_config;
    assert(ret == 0);
    assert(current_config->calibration_valid == 0);
    assert(current_config->ctrl_load_nodes[0] == 0.0);
    assert(current_config->ctrl_load_nodes[1] == 0.25);
    assert(current_config->ctrl_load_nodes[2] == 0.75);
    assert(compare_float(current_config->ctrl_drift_coeffs[0], 1.21) == 1);
    assert(compare_float(current_config->ctrl_drift_coeffs[1], 0.000001) == 1);
    assert(compare_float(current_config->ctrl_drift_coeffs[2], -1.21) == 1);
    assert(current_config->ctrl_load_nodes_factory[0] == 0.0);
    assert(current_config->ctrl_load_nodes_factory[1] == 0.5);
    assert(current_config->ctrl_load_nodes_factory[2] == 1.0);
    assert(compare_float(current_config->ctrl_drift_coeffs_factory[0], 1.2) == 1);
    assert(compare_float(current_config->ctrl_drift_coeffs_factory[1], 0.0) == 1);
    assert(compare_float(current_config->ctrl_drift_coeffs_factory[2], -1.2) == 1);
    assert(current_config->coarse_equilibrium_factory == -1);
    assert(current_config->coarse_equilibrium == -1);
    assert(current_config->calibration_date == 0);
    assert(current_config->ctrl_nodes_length_factory == 3);
    assert(current_config->ctrl_nodes_length == 3);
    assert(current_config->calibration_valid == 0);
    printf("*** TEST PASSED ***\n\n");

    printf("*** TEST GET CALIBRATION PARAMETERS ***\n");
    calibration_parameters = od_get_calibration_parameters(od);
    assert(calibration_parameters->length == 3);
    assert(calibration_parameters->nb_calibration == 30);
    assert(calibration_parameters->ctrl_points[0] == 1600);
    assert(calibration_parameters->ctrl_points[1] == 2400);
    assert(calibration_parameters->ctrl_points[2] == 3200);
    printf("*** TEST PASSED ***\n\n");
}
