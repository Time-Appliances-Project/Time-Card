#include <stdio.h>
#include <assert.h>

#include "../src/checks.h"
#include "../src/algorithm_structs.h"

int main(int argc, char *argv[]) {
    printf("TEST checks program\n");
    int i;

    struct algorithm_input inputs[40];
    for (i = 0; i < 40; i++) {
        inputs[i].valid = true;
    }

    printf("Test condition which should lead to GNSS_OK\n");
    /* All flags are valid --> GNSS should be considered OK */
    assert(check_gnss_valid_over_cycle(inputs, 40) == GNSS_OK);

    printf("Test condition which should lead to GNSS_UNSTABLE\n");
    /* Create a streak of 9 false values --> GNSS should be considered unstable */
    for (i = 0; i < 3; i++) {
        inputs[10+i].valid = false;
    }
    assert(check_gnss_valid_over_cycle(inputs, 40) == GNSS_UNSTABLE);

    printf("Test condition which should lead to GNSS_KO\n");
    /* Create a streak of 10 false values --> GNSS should be considered KO */
    inputs[19].valid = false;
    assert(check_gnss_valid_over_cycle(inputs, 40) == GNSS_KO);
    /* Create a streak of 9 false values but more than a third of the values are false --> GNSS should be considered KO */
    inputs[19].valid = true;
    for (i = 0; i < 4; i++)
        inputs[23 + 3*i].valid = false;
    assert(check_gnss_valid_over_cycle(inputs, 40) == GNSS_KO);

    printf("TEST PASSED\n");
}