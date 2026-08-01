/*
 * This file is Copyright 2015 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#ifndef GPSD_TIMESPEC_H
#define GPSD_TIMESPEC_H

#include <math.h>          /* for modf() */
#include <stdbool.h>       /* for bool */

#define NS_IN_SEC       1000000000LL     /* nanoseconds in a second */
#define MS_IN_NS        1000000LL        /* milliseconds in a nano second */
#define US_IN_SEC       1000000LL        /* microseconds in a second */
#define MS_IN_SEC       1000LL           /* milliseconds in a second */

/* convert a timespec_t to an int64_t of milli seconds */
#define TSTOMS(ts) ((int64_t)((ts)->tv_sec) * MS_IN_SEC + \
                    (int64_t)((ts)->tv_nsec / MS_IN_NS))

/* normalize a timespec
 *
 * three cases to note
 * if tv_sec is positive, then tv_nsec must be positive
 * if tv_sec is negative, then tv_nsec must be negative
 * if tv_sec is zero, then tv_nsec may be positive or negative.
 *
 * this only handles the case where two normalized timespecs
 * are added or subtracted.  (e.g. only a one needs to be borrowed/carried
 *
 * NOTE: this normalization is not the same as ntpd uses
 */

/* return the difference between timespecs in nanoseconds
 * int may be too small, 32 bit long is too small, floats are too imprecise,
 * doubles are not quite precise enough 
 * MUST be at least int64_t to maintain precision on 32 bit code */
#define timespec_diff_ns(x, y) \
    (int64_t)((((x).tv_sec-(y).tv_sec)*NS_IN_SEC)+(x).tv_nsec-(y).tv_nsec)

static inline void TS_NORM( struct timespec *ts)
{
    if ( (  1 <= ts->tv_sec ) ||
         ( (0 == ts->tv_sec ) && (0 <= ts->tv_nsec ) ) ) {
        /* result is positive */
        if ( NS_IN_SEC <= ts->tv_nsec ) {
            /* borrow from tv_sec */
            ts->tv_nsec -= NS_IN_SEC;
            ts->tv_sec++;
        } else if ( 0 > (ts)->tv_nsec ) {
            /* carry to tv_sec */
            ts->tv_nsec += NS_IN_SEC;
            ts->tv_sec--;
        }
    }  else {
        /* result is negative */
        if ( -NS_IN_SEC >= ts->tv_nsec ) {
            /* carry to tv_sec */
            ts->tv_nsec += NS_IN_SEC;
            ts->tv_sec--;
        } else if ( 0 < ts->tv_nsec ) {
            /* borrow from tv_sec */
            ts->tv_nsec -= NS_IN_SEC;
            ts->tv_sec++;
        }
    }
}

/* normalize a timeval */
#define TV_NORM(tv)  \
    do { \
        if ( US_IN_SEC <= (tv)->tv_usec ) { \
            (tv)->tv_usec -= US_IN_SEC; \
            (tv)->tv_sec++; \
        } else if ( 0 > (tv)->tv_usec ) { \
            (tv)->tv_usec += US_IN_SEC; \
            (tv)->tv_sec--; \
        } \
    } while (0)

/* convert timespec to timeval, with rounding */
#define TSTOTV(tv, ts) \
    do { \
        (tv)->tv_sec = (ts)->tv_sec; \
        (tv)->tv_usec = ((ts)->tv_nsec + 500)/1000; \
        TV_NORM( tv ); \
    } while (0)

/* convert timeval to timespec */
#define TVTOTS(ts, tv) \
    do { \
        (ts)->tv_sec = (tv)->tv_sec; \
        (ts)->tv_nsec = (tv)->tv_usec*1000; \
        TS_NORM( ts ); \
    } while (0)

/* subtract two timespec */
#define TS_SUB(r, ts1, ts2) \
    do { \
        (r)->tv_sec = (ts1)->tv_sec - (ts2)->tv_sec; \
        (r)->tv_nsec = (ts1)->tv_nsec - (ts2)->tv_nsec; \
        TS_NORM( r ); \
    } while (0)

/* subtract two timespec, return a double */
#define TS_SUB_D(ts1, ts2) \
    ((double)((ts1)->tv_sec - (ts2)->tv_sec) + \
      ((double)((ts1)->tv_nsec - (ts2)->tv_nsec) * 1e-9))

// true if normalized timespec is non zero
#define TS_NZ(ts) (0 != (ts)->tv_sec || 0 != (ts)->tv_nsec)

// true if normalized timespec equal or greater than zero
#define TS_GEZ(ts) (0 <= (ts)->tv_sec && 0 <= (ts)->tv_nsec)

// true if normalized timespec greater than zero
#define TS_GZ(ts) (0 < (ts)->tv_sec || 0 < (ts)->tv_nsec)

// true if normalized timespec1 greater than timespec2
#define TS_GT(ts1, ts2) ((ts1)->tv_sec > (ts2)->tv_sec || \
                         ((ts1)->tv_sec == (ts2)->tv_sec && \
                          (ts1)->tv_nsec > (ts2)->tv_nsec))

// true if normalized timespec1 greater or equal to timespec2
#define TS_GE(ts1, ts2) ((ts1)->tv_sec > (ts2)->tv_sec || \
                         ((ts1)->tv_sec == (ts2)->tv_sec && \
                          (ts1)->tv_nsec >= (ts2)->tv_nsec))

// true if normalized timespec1 equal to timespec2
#define TS_EQ(ts1, ts2) ((ts1)->tv_sec == (ts2)->tv_sec && \
                         (ts1)->tv_nsec == (ts2)->tv_nsec)

/* convert a timespec to a double.
 * if tv_sec > 2, then inevitable loss of precision in tv_nsec
 * so best to NEVER use TSTONS() 
 * WARNING replacing 1e9 with NS_IN_SEC causes loss of precision */
#define TSTONS(ts) ((double)((ts)->tv_sec + ((ts)->tv_nsec / 1e9)))

/* convert a double to a timespec_t
 * if D > 2, then inevitable loss of precision in nanoseconds
 */
#define DTOTS(ts, d) \
    do { \
        double int_part; \
        (ts)->tv_nsec = (long)(modf(d, &int_part) * 1e9); \
        (ts)->tv_sec = (time_t)int_part; \
    } while (0)

/* convert integer (64 bit for full range) ms to a timespec_t */
#define MSTOTS(ts, ms) \
    do { \
        (ts)->tv_sec = (time_t)(ms / 1000); \
        (ts)->tv_nsec = (long)((ms % 1000) * 1000000L); \
    } while (0)

/* convert integer (64 bit for full range) us to a timespec_t */
#define USTOTS(ts, ms) \
    do { \
        (ts)->tv_sec = (time_t)(ms / 1000000); \
        (ts)->tv_nsec = (long)((ms % 1000000) * 1000L); \
    } while (0)

#define TIMESPEC_LEN    42      /* required length of a timespec buffer */

extern const char *timespec_str(const struct timespec *, char *, size_t);

// bool nanowait(int, struct timespec *);

#endif /* GPSD_TIMESPEC_H */

/* end */
// vim: set expandtab shiftwidth=4
