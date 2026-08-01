/*
 * This file is Copyright 2015 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

#ifndef GPSD_NTPSHM_H
#define GPSD_NTPSHM_H

#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "gnss.h"

#define NTPD_BASE       0x4e545030      /* "NTP0" */

/*
 * How to read and write fields in an NTP shared segment.
 * This definition of shmTime is from ntpd source ntpd/refclock_shm.c
 *
 * The fields aren't documented there.  It appears the only use of
 * nsamples is internal to the (obsolete and deprecated) EES M201
 * receiver refclock. The precision field is nominally log(2) of the
 * source's jitter in seconds:
 *      -1 is about 100mSec jitter
 *      -10 is about 1 mSec jitter (GR-601W or other USB with 1ms poll interval)
 *      -13 is about 100 uSec
 *      -20 is about 1 uSec (typical for serial PPS)
 */

struct shmTime
{
    int mode;   /* 0 - if valid set
                 *       use values,
                 *       clear valid
                 * 1 - if valid set
                 *       if count before and after read of values is equal,
                 *         use values
                 *       clear valid
                 */
    volatile int count;
    time_t clockTimeStampSec;
    int clockTimeStampUSec;
    time_t receiveTimeStampSec;
    int receiveTimeStampUSec;
    int leap;                   /* not leap second offset, a notification code */
    int precision;              /* log(2) of source jitter */
    int nsamples;               /* not used */
    volatile int valid;
    unsigned        clockTimeStampNSec;     /* Unsigned ns timestamps */
    unsigned        receiveTimeStampNSec;   /* Unsigned ns timestamps */
    int             dummy[8];
};


/*
 * These types are internal to GPSD
 */
enum segstat_t {OK, NO_SEGMENT, NOT_READY, BAD_MODE, CLASH};

struct shm_stat_t {
    enum segstat_t status;
    struct timespec tvc;    /* System time when SHM read, for debug only */
    struct timespec tvr;    /* System time at GPS time */
    struct timespec tvt;    /* GPS time */
    int precision;
    int leap;
};

#ifndef TIMEDELTA_DEFINED

struct timedelta_t {
    struct timespec     real;
    struct timespec     clock;
};

#define TIMEDELTA_DEFINED
#endif /* TIMEDELTA_DEFINED */

struct shmTime *shm_get(int, bool, bool);
extern char *ntp_name(const int);
enum segstat_t ntp_read(struct shmTime *, struct shm_stat_t *, const bool);
void ntp_write(volatile struct shmTime *, struct timedelta_t *, int, int);

void ntpshm_context_init(struct gps_context_t *);
void ntpshm_session_init(struct gps_device_t *);
int ntpshm_put(struct gps_device_t *, volatile struct shmTime *, struct timedelta_t *);
void ntpshm_link_deactivate(struct gps_device_t *);
void ntpshm_link_activate(struct gps_device_t *);

#endif /* GPSD_NTPSHM_H */

/* end */
// vim: set expandtab shiftwidth=4
