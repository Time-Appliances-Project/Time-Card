#ifndef PHASE_ERROR_TRACKING_TEST_H
#define PHASE_ERROR_TRACKING_TEST_H

#include <stdbool.h>

#include "config.h"

#define TEST_PHASE_ERROR_TRACKING_OK 0
#define TEST_PHASE_ERROR_TRACKING_KO -1

int test_phase_error_tracking(char* ocp_name, struct config* config);

#endif /* PHASE_ERROR_TRACKING_TEST_H */
