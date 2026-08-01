/*
 * This file is Copyright 2017 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 * This is the header for os_compat.c, which contains functions dealing with
 * compatibility issues across OSes.
 *
 * Calling file needs to have previously included "gpsd_config.h"
 */
#ifndef _GPSD_OS_COMPAT_H_
#define _GPSD_OS_COMPAT_H_

# ifdef __cplusplus
extern "C" {
# endif

extern const char *gpsd_version;

#ifndef HAVE_CLOCK_GETTIME

/* Simulate ANSI/POSIX clock_gettime() on platforms that don't have it */

#include <time.h>

#ifndef CLOCKID_T_DEFINED
typedef int clockid_t;
#define CLOCKID_T_DEFINED
#endif /* !CLOCKID_T_DEFINED */

/*
 * OS X 10.5 and later use _STRUCT_TIMESPEC (like other OSes)
 * 10.4 uses _TIMESPEC
 * 10.3 and earlier use _TIMESPEC_DECLARED
 */
#if !defined(_STRUCT_TIMESPEC) && \
    !defined(_TIMESPEC) && \
    !defined(_TIMESPEC_DECLARED) && \
    !defined(__timespec_defined)
#define _STRUCT_TIMESPEC
struct timespec {
    time_t  tv_sec;
    long    tv_nsec;
};
#endif /* !_STRUCT_TIMESPEC ... */

/* OS X does not have clock_gettime */
#define CLOCK_REALTIME 0
int clock_gettime(clockid_t, struct timespec *);

#endif /* !HAVE_CLOCK_GETTIME */

/*
 * Wrapper or substitute for Linux/BSD daemon()
 *
 * There are some issues with this function even when it's present, so
 * wrapping it confines the issues to a single place in os_compat.c.
 */

int os_daemon(int nochdir, int noclose);


#ifdef HAVE_SYSLOG_H
#include <syslog.h>

  #if !defined(LOG_PERROR)
    // Slowlaris 10 does not define LOG_PERROR
    #define     LOG_PERROR      0x20    /* log to stderr as well */
  #endif // LOG_PERROR

#else // HAVE_SYSLOG_H
/*
 * Substitutes for syslog functions
 *  (only subset of defines used by gpsd components listed)
 *
 */
/* Copy of syslog.h defines when otherwise not available */
/* priorities (these are ordered) */
#define LOG_EMERG       0       /* system is unusable */
#define LOG_ALERT       1       /* action must be taken immediately */
#define LOG_CRIT        2       /* critical conditions */
#define LOG_ERR         3       /* error conditions */
#define LOG_WARNING     4       /* warning conditions */
#define LOG_NOTICE      5       /* normal but significant condition */
#define LOG_INFO        6       /* informational */
#define LOG_DEBUG       7       /* debug-level messages */
/* Option flags for openlog */
#define LOG_PID         0x01    /* log the pid with each message */
#define LOG_PERROR      0x20    /* log to stderr as well */
/* facility codes */
#define LOG_USER        (1<<3)  /* random user-level messages */
#define LOG_DAEMON      (3<<3)  /* system daemons */

void syslog(int priority, const char *format, ...);
void openlog(const char *__ident, int __option, int __facility);
void closelog(void);
#endif /* !HAVE_SYSLOG_H */


/* Provide BSD strlcat()/strlcpy() on platforms that don't have it */

#ifndef HAVE_STRLCAT

#include <string.h>
size_t strlcat(char *dst, const char *src, size_t size);

#endif /* !HAVE_STRLCAT */

#ifndef HAVE_STRLCPY

#include <string.h>
size_t strlcpy(char *dst, const char *src, size_t size);

#endif /* !HAVE_STRLCPY */

/* Provide missing signal numbers for non-POSIX builds */

#ifndef SIGHUP
#define SIGHUP  1
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif

/* Provide missing open flags for non-POSIX builds */

#ifndef O_NOCTTY
#define O_NOCTTY   0400
#endif

/* Provide missing sincos() if needed */

#ifndef HAVE_SINCOS
void sincos(double x, double *sinp, double *cosp);
#endif

# ifdef __cplusplus
}
# endif

#endif /* _GPSD_OS_COMPAT_H_ */
// vim: set expandtab shiftwidth=4
