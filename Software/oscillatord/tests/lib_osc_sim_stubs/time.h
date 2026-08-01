#ifndef TESTS_LIB_OSC_SIM_STUBS__TIME_H_
#define TESTS_LIB_OSC_SIM_STUBS__TIME_H_
#include <sys/types.h>

typedef __clockid_t clockid_t;

int                 clock_gettime(clockid_t clock_id, struct timespec* tp);

#endif /* TESTS_LIB_OSC_SIM_STUBS__TIME_H_ */
