/*
 * This file is Copyright 2015 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 * Oct 2019: Added qErr* to ppsthread_t
 */

#ifndef PPSTHREAD_H
#define PPSTHREAD_H

#include <time.h>

#ifndef TIMEDELTA_DEFINED
#define TIMEDELTA_DEFINED
struct timedelta_t {
    struct timespec     real;
    struct timespec     clock;
};
#endif /* TIMEDELTA_DEFINED */


/*
 * Set context, devicefd, and devicename at initialization time, before
 * you call pps_thread_activate().  The context pointer can be used to
 * pass data to the hook routines.
 *
 * Do not set the fix_in member or read the pps_out member directly,
 * these accesses need to be mutex-locked and that is what the last
 * two functions are for.
 *
 * The report hook is called when each PPS event is recognized.  The log
 * hook is called to log error and status indications from the thread.
 */
struct pps_thread_t {
    void *context;              /* PPS thread code leaves this alone */
    int devicefd;          // device file descriptor
    const char *devicename;           /* device path */
    char *(*report_hook)(volatile struct pps_thread_t *,
                         struct timedelta_t *);
    void (*log_hook)(volatile struct pps_thread_t *,
                     int errlevel, const char *fmt, ...);
    struct timedelta_t fix_in;  // real & clock time when in-band fix received
    struct timedelta_t pps_out; /* real & clock time of last PPS event */
    int ppsout_count;
    /* quantization error adjustment to PPS. aka "sawtooth" correction */
    long qErr;                  /* offset in picoseconds (ps) */
    /* time of PPS pulse that qErr applies to */
    struct timespec qErr_time;
};

#define THREAD_ERROR    4
#define THREAD_WARN     3
#define THREAD_INF      2
#define THREAD_PROG     1
#define THREAD_RAW      0

extern void pps_thread_activate(volatile struct pps_thread_t *);
extern void pps_thread_deactivate(volatile struct pps_thread_t *);
extern void pps_thread_fixin(volatile struct pps_thread_t *,
                             volatile struct timedelta_t *);
extern void pps_thread_qErrin(volatile struct pps_thread_t *pps_thread,
                              long qErr, struct timespec qErr_time);
extern int pps_thread_ppsout(volatile struct pps_thread_t *,
                             volatile struct timedelta_t *);
int pps_check_fake(const char *);
char *pps_get_first(void);

#endif /* PPSTHREAD_H */
// vim: set expandtab shiftwidth=4
