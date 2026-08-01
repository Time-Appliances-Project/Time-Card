#ifndef MRO_DEVICE_TEST_H
#define MRO_DEVICE_TEST_H

#include <stdbool.h>
#include <stdint.h>

bool test_mro50_device(int mro50);
int  mro50_read_coarse(int mro50, uint32_t* coarse);

#endif /* MRO_DEVICE_TEST_H */