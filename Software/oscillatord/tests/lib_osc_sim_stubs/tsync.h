#ifndef TESTS_LIB_OSC_SIM_STUBS__TSYNC_H_
#define TESTS_LIB_OSC_SIM_STUBS__TSYNC_H_

typedef void* TSYNC_BoardHandle;
typedef enum TSYNC_ERROR {
    TSYNC_SUCCESS
} TSYNC_ERROR;
TSYNC_ERROR TSYNC_open(TSYNC_BoardHandle* hnd, const char* deviceName);
TSYNC_ERROR TSYNC_GR_getValidity(TSYNC_BoardHandle hnd, unsigned int nInstance, int* bTimeValid, int* bPpsValid);
TSYNC_ERROR TSYNC_close(TSYNC_BoardHandle hnd);

#endif /* TESTS_LIB_OSC_SIM_STUBS__TSYNC_H_ */
