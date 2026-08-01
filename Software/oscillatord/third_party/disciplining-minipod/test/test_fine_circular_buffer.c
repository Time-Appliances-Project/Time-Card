#include <assert.h>
#include "stdio.h"

#include "../src/compare_floats.h"
#include "../src/log.h"
#include "../src/fine_circular_buffer.h"
#include "../src/algorithm_structs.h"

static void test_get_index_of_temperature(void)
{
    assert(get_index_of_temperature(45.6) == 102);
    assert(get_index_of_temperature(38.32) == 73);
    assert(get_index_of_temperature(55.25) == 141);
    assert(get_index_of_temperature(19.7) == -1);
    assert(get_index_of_temperature(60) == TEMPERATURE_STEPS);
}

static void init_fine_circular_buffer(struct fine_circular_buffer *fine_buffer)
{
    int i;
    for ( i = 0; i < TEMPERATURE_STEPS; i ++) {
        fine_buffer[i].buffer_length = 0;
        fine_buffer[i].read_index = 0;
        fine_buffer[i].write_index = 0;
        int j;
        for (j = 0; j < CIRCULAR_BUFFER_SIZE; j++) {
            fine_buffer[i].buffer[j] = 0.0;
        }
    }
}

int main(int argc, char **argv)
{
    struct algorithm_state state;
    int i, j, ret;

    log_set_level(LOG_INFO);

    /* Test get_index_of_temperature function */
    test_get_index_of_temperature();

    init_fine_circular_buffer(state.fine_estimated_es_buffer);
    /* Fill buffer with data */
    /* Leave some circular buffer with less than MIN_VALUES_FOR_MEAN */
    for (i = 0; i < TEMPERATURE_STEPS; i++) {
        /* Fill in 1/3 of circular buffer all accross the temperature range */
        if (i % 3 == 0) {
            double temperature = (float) ((float) i + MIN_TEMPERATURE * STEPS_BY_DEGREE) / STEPS_BY_DEGREE;
            for (j = 0; j < MIN_VALUES_FOR_MEAN; j++) {
                ret = add_fine_from_temperature(state.fine_estimated_es_buffer, 2400.0 - i, temperature);
                if (ret != 0) {
                    printf("Could not add data to buffer\n");
                    return -1;
                }
            }
            /* Mean value should be computed automatically so check value is set as expected */
            assert(compare_float(state.fine_estimated_es_buffer[i].mean_fine, 2400.0 - i) == 1);
        }
    }
    /* Test functions to find closest bin of each side of a temperature */
    assert(find_closest_left_circular_buffer_from_index(state.fine_estimated_es_buffer, get_index_of_temperature(18.0)) == -1);
    assert(find_closest_left_circular_buffer_from_index(state.fine_estimated_es_buffer, get_index_of_temperature(34.60)) == 57);
    assert(find_closest_left_circular_buffer_from_index(state.fine_estimated_es_buffer, get_index_of_temperature(55.9)) == 141);
    assert(find_closest_left_circular_buffer_from_index(state.fine_estimated_es_buffer, get_index_of_temperature(70.0)) == 159);

    assert(find_closest_right_circular_buffer_from_index(state.fine_estimated_es_buffer, get_index_of_temperature(18.0)) == 0);
    assert(find_closest_right_circular_buffer_from_index(state.fine_estimated_es_buffer, get_index_of_temperature(34.60)) == 60);
    assert(find_closest_right_circular_buffer_from_index(state.fine_estimated_es_buffer, get_index_of_temperature(55.9)) == 144);
    assert(find_closest_right_circular_buffer_from_index(state.fine_estimated_es_buffer, get_index_of_temperature(70.0)) == -1);

    /* Test temperature compensation function */
    /* Test Table with 1/3 value */
    assert(compare_float(get_fine_from_table(&state, 18.6), -1.0));
    assert(compare_float(get_fine_from_table(&state, 21.6), 2394.0));
    assert(compare_float(get_fine_from_table(&state, 62.25), -1.0));
    /* Clear table */
    init_fine_circular_buffer(state.fine_estimated_es_buffer);

    /* Test with Holdover temperature @ 50° */
    state.holdover_mRO_EP_temperature = 50.0;
    state.estimated_equilibrium_ES = 2500.0;
    /* Test Empty table behaviour */
    assert(compare_float(get_fine_from_table(&state, 50.0), -1.0));

    for (i = 0; i < 48; i++) {
        assert(compare_float(get_fine_from_table(&state, i), -1.0));
    }

    /* Test with Holdover temperature @ 35° */
    state.holdover_mRO_EP_temperature = 35.0;
    assert(compare_float(get_fine_from_table(&state, 35.), -1.0));
    for (i = 0; i < 48; i++) {
        assert(compare_float(get_fine_from_table(&state, i), -1.0));
    }

    /* Clear table */
    init_fine_circular_buffer(state.fine_estimated_es_buffer);

    /* Fill table with one value between 30 and 40° */
    for (i = 0; i < MIN_VALUES_FOR_MEAN; i++) {
        add_fine_from_temperature(state.fine_estimated_es_buffer, 2400.0 + 10 * i, 35.0);
    }
    for (i = 0; i < 35; i++) {
        assert(compare_float(get_fine_from_table(&state, i), -1.0));
    }

    assert(compare_float(get_fine_from_table(&state, 35), 2520 ));

    for (i = 36; i < 48; i++) {
        assert(compare_float(get_fine_from_table(&state, i), -1.0));
    }

    /* Clear table */
    init_fine_circular_buffer(state.fine_estimated_es_buffer);

     /* Fill table with one value above 40° */
    for (i = 0; i < MIN_VALUES_FOR_MEAN; i++) {
        add_fine_from_temperature(state.fine_estimated_es_buffer, 2400.0 + 10 * i, 45.0);
    }

    for (i = 0; i < 45; i++) {
        assert(compare_float(get_fine_from_table(&state, i), -1.0));
    }

    assert(compare_float(get_fine_from_table(&state, 45), 2520.0 ));

    for (i = 46; i < 48; i ++) {
        assert(compare_float(
            get_fine_from_table(&state, 46 + i),
            -1.0
        ));
    }

    ret = write_buffers_in_file(state.fine_estimated_es_buffer, "./output_buffer.txt");
    if (ret != 0) {
        printf("Error writing file\n");
        return -1;
    }
    log_info("PASSED");
    return 0;
}
