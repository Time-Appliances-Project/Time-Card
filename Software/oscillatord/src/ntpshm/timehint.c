/*
 * timehint.c - put time information in SHM segment for ntpd, or to chrony
 *
 * Note that for easy debugging all logging from this file is prefixed
 * with PPS or NTP.
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
#include <time.h>        /* for timespec */
#include <unistd.h>

#include "timespec.h"
// #include "gpsd.h"

#include "ntpshm.h"
#include "log.h"
#include "gnss.h"
#include "ppsthread.h"

/* Note: you can start gpsd as non-root, and have it work with ntpd.
 * However, it will then only use the ntpshm segments 2 3, and higher.
 *
 * Ntpd always runs as root (to be able to control the system clock).
 * After that it often (depending on its host configuration) drops to run as
 * user ntpd and group ntpd.
 *
 * As of February 2015 its rules for the creation of ntpshm segments are:
 *
 * Segments 0 and 1: permissions 0600, i.e. other programs can only
 *                   read and write as root.
 *
 * Segments 2, 3, and higher:
 *                   permissions 0666, i.e. other programs can read
 *                   and write as any user.  I.e.: if ntpd has been
 *                   configured to use these segments, any
 *                   unprivileged user is allowed to provide data
 *                   for synchronisation.
 *
 * By default ntpd creates 0 segments (though the documentation is
 * written in such a way as to suggest it creates 4).  It can be
 * configured to create up to 217.  gpsd creates two segments for each
 * device it can drive; by default this is 8 segments for 4
 * devices,but can be higher if it was compiled with a larger value of
 * MAX_DEVICES.
 *
 * Started as root, gpsd does as ntpd when attaching (creating) the
 * segments.  In contrast to ntpd, which only attaches (creates)
 * configured segments, gpsd creates all segments.  Thus a gpsd will
 * by default create eight segments 0-7 that an ntpd with default
 * configuration does not watch.
 *
 * Started as non-root, gpsd will only attach (create) segments 2 and
 * above, with permissions 0666.  As the permissions are for any user,
 * the creator does not matter.
 *
 * For each GPS module gpsd controls, it will use the attached ntpshm
 * segments in pairs (for coarse clock and pps source, respectively)
 * starting from the first found segments.  I.e. started as root, one
 * GPS will deliver data on all segments including 0 and 1; started as
 * non-root, gpsd will be deliver data only on segments 2 and higher.
 *
 * Segments are allocated to activated devices on a first-come-first-served
 * basis. A device's segment is marked unused when the device is closed and
 * may be re-used by devices connected later.
 *
 * To debug, try looking at the live segments this way:
 *
 *  ipcs -m
 *
 * results  should look like this:
 * ------ Shared Memory Segments --------
 *  key        shmid      owner      perms      bytes      nattch     status
 *  0x4e545030 0          root       700        96         2
 *  0x4e545031 32769      root       700        96         2
 *  0x4e545032 163842     root       666        96         1
 *  0x4e545033 196611     root       666        96         1
 *  0x4e545034 253555     root       666        96         1
 *  0x4e545035 367311     root       666        96         1
 *
 * For a bit more data try this:
 *  cat /proc/sysvipc/shm
 *
 * If gpsd can not open the segments be sure you are not running SELinux
 * or apparmor.
 *
 * if you see the shared segments (keys 1314148400 -- 1314148405), and
 * no gpsd or ntpd is running, you can remove them like this:
 *
 * ipcrm  -M 0x4e545030
 * ipcrm  -M 0x4e545031
 * ipcrm  -M 0x4e545032
 * ipcrm  -M 0x4e545033
 * ipcrm  -M 0x4e545034
 * ipcrm  -M 0x4e545035
 *
 * Removing these segments is usually not necessary, as the operating system
 * garbage-collects them when they have no attached processes.
 */

static volatile struct shmTime *getShmTime(struct gps_context_t *context,
                                           int unit)
{
    int shmid;
    unsigned int perms;
    volatile struct shmTime *p;
    // set the SHM perms the way ntpd does
    if (unit < 2) {
        // we are root, be careful
        perms = 0600;
    } else {
        // we are not root, try to work anyway
        perms = 0666;
    }

    /*
     * Note: this call requires root under BSD, and possibly on
     * well-secured Linux systems.  This is why ntpshm_context_init() has to be
     * called before privilege-dropping.
     */
    shmid = shmget((key_t) (NTPD_BASE + unit),
                   sizeof(struct shmTime), (int)(IPC_CREAT | perms));
    if (shmid == -1) {
        log_error("NTP: shmget(%ld, %zd, %o) fail: %s",
                 (long int)(NTPD_BASE + unit), sizeof(struct shmTime),
                 (int)perms, strerror(errno));
        return NULL;
    }
    p = (struct shmTime *)shmat(shmid, 0, 0);
    if ((int)(long)p == -1) {
        log_error("NTP: shmat failed: %s",
				strerror(errno));
        return NULL;
    }
    log_trace("NTP: shmat(%d,0,0) succeeded, segment %d",
		shmid, unit);
    return p;
}

void ntpshm_context_init(struct gps_context_t *context)
/* Attach all NTP SHM segments. Called once at startup, while still root. */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++) {
        // Only grab the first two when running as root.
        if (2 <= i || 0 == getuid()) {
            context->shmTime[i] = getShmTime(context, i);
        }
    }
    memset(context->shmTimeInuse, 0, sizeof(context->shmTimeInuse));
}

/* allocate NTP SHM segment.  return its segment number, or -1 */
static volatile struct shmTime *ntpshm_alloc(struct gps_context_t *context)
{
    int i;
    for (i = 0; i < NTPSHMSEGS; i++) {
        if (context->shmTime[i] != NULL && !context->shmTimeInuse[i]) {
            context->shmTimeInuse[i] = true;

            /*
             * In case this segment gets sent to ntpd before an
             * ephemeris is available, the LEAP_NOTINSYNC value will
             * tell ntpd that this source is in a "clock alarm" state
             * and should be ignored.  The goal is to prevent ntpd
             * from declaring the GPS a falseticker before it gets
             * all its marbles together.
             */
            memset((void *)context->shmTime[i], 0, sizeof(struct shmTime));
            context->shmTime[i]->mode = 1;
            context->shmTime[i]->leap = LEAP_NOTINSYNC;
            context->shmTime[i]->precision = -20;/* initially 1 micro sec */
            context->shmTime[i]->nsamples = 3;  /* stages of median filter */
            log_info("NTP:PPS: using SHM(%d)", i);

            return context->shmTime[i];
        }
    }

    return NULL;
}

static bool ntpshm_free(struct gps_context_t * context,
                        volatile struct shmTime *s)
/* free NTP SHM segment */
{
    int i;

    for (i = 0; i < NTPSHMSEGS; i++)
        if (s == context->shmTime[i]) {
            context->shmTimeInuse[i] = false;
            return true;
        }

    return false;
}

void ntpshm_session_init(struct gps_device_t *session)
{
    /* mark NTPD shared memory segments as unused */
    session->shm_clock = NULL;
    session->shm_pps = NULL;
}

/* put a received fix time into shared memory for NTP */
int ntpshm_put(struct gps_device_t *session, volatile struct shmTime *shmseg,
               struct timedelta_t *td)
{
    char real_str[TIMESPEC_LEN];
    char clock_str[TIMESPEC_LEN];

    /* Any NMEA will be about -1 or -2. Garmin GPS-18/USB is around -6 or -7. */
    int precision = -20; /* default precision, 1 micro sec */

    if (shmseg == NULL) {
        log_trace("NTP:PPS: missing shm");
        return 0;
    }

    // FIXME: make NMEA precision -1
    // if (shmseg == session->shm_pps) {
    //     /* precision is a floor so do not make it tight */
    //     if ( source_usb == session->sourcetype ) {
    //         /* if PPS over USB, then precision = -10, 1 milli sec  */
    //         precision = -10;
    //     } else {
    //         /* likely PPS over serial, precision = -20, 1 micro sec */
    //         precision = -20;
    //     }
    // }

    ntp_write(shmseg, td, precision, session->context->leap_notify);

    log_trace("NTP: ntpshm_put(%d) %s @ %s",
             precision,
             timespec_str(&td->real, real_str, sizeof(real_str)),
             timespec_str(&td->clock, clock_str, sizeof(clock_str)));

    return 1;
}

#define SOCK_MAGIC 0x534f434b
struct sock_sample {
    struct timeval tv;
    double offset;
    int pulse;
    int leap;    /* notify that a leap second is upcoming */
    int _pad;
    int magic;      /* must be SOCK_MAGIC */
};


static char *report_hook(volatile struct pps_thread_t *pps_thread,
                                        struct timedelta_t *td)
/* ship the time of a PPS event to ntpd and/or chrony */
{
    char *log1;
    struct gps_device_t *session = (struct gps_device_t *)pps_thread->context;

    /* PPS only source never get any serial info
     * so no NTPTIME_IS or fixcnt */
    if ( source_pps != session->sourcetype) {
        /* FIXME! these two validations need to move back into ppsthread.c */

        /*
         * Only listen to PPS after several consecutive fixes,
         * otherwise time may be inaccurate.  We know this is
         * required on all Garmin and u-blox.  Safest to do it
         * for all cases as we have no other general way to know
         * if PPS is good.
         */
        if (session->fixcnt <= NTP_MIN_FIXES)
            return "no fix";
    }

    /* FIXME?  how to log socket AND shm reported? */
    log1 = "accepted";
    if (session->shm_pps != NULL)
        (void)ntpshm_put(session, session->shm_pps, td);

    /* session context might have a hook set, too */
    if (session->context->pps_hook != NULL)
        session->context->pps_hook(session, td);

    return log1;
}

void ntpshm_link_deactivate(struct gps_device_t *session)
/* release ntpshm storage for a session */
{
    if (session == NULL)
        return;
    if (session->shm_clock != NULL) {
        (void)ntpshm_free(session->context, session->shm_clock);
        session->shm_clock = NULL;
    }
    if (session->shm_pps != NULL) {
        pps_thread_deactivate(&session->pps_thread);
        (void)ntpshm_free(session->context, session->shm_pps);
        session->shm_pps = NULL;
    }
}

/* set up ntpshm storage for a session */
void ntpshm_link_activate(struct gps_device_t *session)
{
    /* don't talk to NTP when we're running inside the test harness */
    if (session->sourcetype == source_pty)
        return;

    if (session->sourcetype != source_pps ) {
        /* allocate a shared-memory segment for "NMEA" time data */
        session->shm_clock = ntpshm_alloc(session->context);

        if (session->shm_clock == NULL) {
            log_warn("NTP: ntpshm_alloc() failed");
            return;
        }
    }

    if (session->sourcetype == source_usb ||
        session->sourcetype == source_rs232 ||
        session->sourcetype == source_pps) {
        /* We also have the 1pps capability, allocate a shared-memory segment
         * for the 1pps time data and launch a thread to capture the 1pps
         * transitions
         */
        session->shm_pps = ntpshm_alloc(session->context);
        if (NULL == session->shm_pps) {
            log_warn("PPS: ntpshm_alloc(1) failed");
        } else {
            session->pps_thread.report_hook = report_hook;
            pps_thread_activate(&session->pps_thread);
        }
    }
}

/* end */
// vim: set expandtab shiftwidth=4
