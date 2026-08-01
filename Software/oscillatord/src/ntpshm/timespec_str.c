/*
 * We also need to set the value high enough to signal inclusion of
 * newer features (like clock_gettime).  See the POSIX spec for more info:
 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_02_01_02
 *
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

// #include "../include/gpsd_config.h"  /* must be before all includes */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "timespec.h"

/* Convert a normalized timespec to a nice string
 * put in it *buf, buf should be at least 22 bytes
 *
 * the returned buffer will look like, shortest case:
 *    sign character ' ' or '-'
 *    one digit of seconds
 *    decimal point '.'
 *    9 digits of nanoSec
 *
 * So 12 chars, like this: "-0.123456789"
 *
 * Probable worst case is 10 digits of seconds,
 * but standards do not provide hard limits to time_t
 * So 21 characters like this: "-2147483647.123456789"
 *
 * date --date='@2147483647' is: Mon Jan 18 19:14:07 PST 2038
 * date --date='@9999999999' is: Sat Nov 20 09:46:39 PST 2286
 *
 */
const char *timespec_str(const struct timespec *ts, char *buf, size_t buf_size)
{
    char sign = ' ';

    if (!TS_GEZ(ts)) {
        sign = '-';
    }

    /* %lld and (long long) because some time_t is bigger than a long
     * mostly on 32-bit systems. */
    (void)snprintf(buf, buf_size, "%c%lld.%09ld",
                   sign,
                   (long long)llabs(ts->tv_sec),
                   (long)labs(ts->tv_nsec));
    return  buf;
}

/* end */
// vim: set expandtab shiftwidth=4
