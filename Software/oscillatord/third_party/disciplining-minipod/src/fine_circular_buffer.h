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
 * @file fine_circular_buffer.h
 * @date 2022-05-06
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef MINIPOD_FINE_CIRCULAR_BUFFER_H
#define MINIPOD_FINE_CIRCULAR_BUFFER_H

#include "stdint.h"
#include "algorithm_structs.h"

/**
 * @brief Default coefficient used when temperature table is not complete
 * or if temperature is outside of the temperature table
 */
#define DEFAULT_DELTA_TEMPERATURE_COEFF -5.0
/**
 * @brief Temperature below which we consider flat slope
 * on temperature table if we do not have values
 */
#define MIN_TEMPERATURE_DEFAULT 28.0
/**
 * @brief Temperature above which we considere slope of 2 * DEFAULT_DELTA_TEMPERATURE_COEFF
 * when temperature table is not complete
 * or if temperature is outside of the temperature table
 */
#define MAX_TEMPERATURE_DEFAULT 38.0

void print_tuples(struct fine_circular_buffer *circular_buffer);
int write_fine(struct fine_circular_buffer *circular_buffer, float fine);
int add_fine_from_temperature(struct fine_circular_buffer fine_buffer[TEMPERATURE_STEPS], float fine, double temp);
int write_buffers_in_file(struct fine_circular_buffer fine_buffer[TEMPERATURE_STEPS], const char* output_file);
int compute_mean_value(struct fine_circular_buffer *fine_buffer);
int get_index_of_temperature(float temperature);
int find_closest_left_circular_buffer_from_index(struct fine_circular_buffer *fine_buffer, int temperature_index);
int find_closest_right_circular_buffer_from_index(struct fine_circular_buffer *fine_buffer, int temperature_index);
float get_fine_from_table(struct algorithm_state *state, float input_temperature);
void update_mean_values(struct fine_circular_buffer fine_buffer[TEMPERATURE_STEPS]);

#endif /* MINIPOD_FINE_CIRCULAR_BUFFER_H */
