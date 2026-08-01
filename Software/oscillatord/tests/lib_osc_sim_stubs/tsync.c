#include <stdbool.h>
#include <stdio.h>

#include "tsync.h"

TSYNC_ERROR TSYNC_open(TSYNC_BoardHandle* hnd, const char* deviceName) {
    return TSYNC_SUCCESS;
}

TSYNC_ERROR
TSYNC_GR_getValidity(TSYNC_BoardHandle hnd, unsigned int nInstance, int* bTimeValid, int* bPpsValid) {
    *bTimeValid = *bPpsValid = true;
    return TSYNC_SUCCESS;
}

TSYNC_ERROR TSYNC_close(TSYNC_BoardHandle hnd) {
    return TSYNC_SUCCESS;
}

static __attribute__((constructor)) void tsync_stub_constructor(void) {
    fprintf(stderr, "libtsync stub library\n");
}
