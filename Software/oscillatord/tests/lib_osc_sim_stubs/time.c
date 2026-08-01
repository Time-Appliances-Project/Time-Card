#include <stdio.h>

#include "time.h"

static __time_t tv_sec;

int             clock_gettime(clockid_t clock_id, struct timespec* tp) {
                *tp = (struct timespec){.tv_nsec = 0, .tv_sec = tv_sec};
                tv_sec++;

                return 0;
}
