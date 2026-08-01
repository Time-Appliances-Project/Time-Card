#ifndef COMPARE_FLOATS
#define COMPARE_FLOATS

#include <math.h>

#define TOLERANCE_PRECISION 0.00001

//compares if the float f1 is equal with f2 and returns 1 if true and 0 if false
static inline int compare_float(float f1, float f2)
{
    return fabs(f1 -f2) <= TOLERANCE_PRECISION ? 1 : 0;
}

#endif /* COMPARE_FLOATS */
