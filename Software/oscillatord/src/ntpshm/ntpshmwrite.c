/*
 * ntpshmwrite.c - put time information in SHM segment for ntpd
 *
 * This file is Copyright 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

// #include "../include/gpsd_config.h"  /* must be before all includes */

#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ntpshm.h"
#include "compiler.h"
#include "timespec.h"
#include "gnss.h"


/* put a received fix time into shared memory for NTP */
void ntp_write(volatile struct shmTime *shmseg,
               struct timedelta_t *td, int precision, int leap_notify)
{
    struct tm tm;

    /*
     * insist that leap seconds only happen in june and december
     * GPS emits leap pending for 3 months prior to insertion
     * NTP expects leap pending for only 1 month prior to insertion
     * Per http://bugs.ntp.org/1090
     *
     * ITU-R TF.460-6, Section 2.1, says leap seconds can be primarily
     * in Jun/Dec but may be in March or September
     */
    (void)gmtime_r( &(td->real.tv_sec), &tm);
    if ( 5 != tm.tm_mon && 11 != tm.tm_mon ) {
        /* Not june, not December, no way */
        leap_notify = LEAP_NOWARNING;
    }

    /* we use the shmTime mode 1 protocol
     *
     * ntpd does this:
     *
     * reads valid.
     * IFF valid is 1
     *    reads count
     *    reads values
     *    reads count
     *    IFF count unchanged
     *        use values
     *    clear valid
     *
     */

    // should not be needed, but sometimes is...
    TS_NORM(&td->real);
    TS_NORM(&td->clock);

    shmseg->valid = 0;
    shmseg->count++;
    /* We need a memory barrier here to prevent write reordering by
     * the compiler or CPU cache */
    memory_barrier();
    shmseg->clockTimeStampSec = (time_t)td->real.tv_sec;
    shmseg->clockTimeStampUSec = (int)(td->real.tv_nsec/1000);
    shmseg->clockTimeStampNSec = (unsigned)td->real.tv_nsec;
    shmseg->receiveTimeStampSec = (time_t)td->clock.tv_sec;
    shmseg->receiveTimeStampUSec = (int)(td->clock.tv_nsec/1000);
    shmseg->receiveTimeStampNSec = (unsigned)td->clock.tv_nsec;
    shmseg->leap = leap_notify;
    shmseg->precision = precision;
    memory_barrier();
    shmseg->count++;
    // make sure that is flushed, then mark valid
    memory_barrier();
    shmseg->valid = 1;
}

/* end */
// vim: set expandtab shiftwidth=4
