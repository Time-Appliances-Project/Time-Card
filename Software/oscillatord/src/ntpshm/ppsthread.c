/*
 * ppsthread.c - manage PPS watcher threads
 *
 * To enable KPPS, this file needs to be compiled with HAVE_SYS_TIMEPPS_H
 *
 * If you are not good at threads do not touch this file!
 * For example: errno is thread safe; strerror() is not.
 *
 * It helps to know that there are two PPS measurement methods in
 * play.  One is defined by RFC2783 and typically implemented in the
 * kernel.  It is available on FreeBSD, Linux, and NetBSD.  In gpsd it
 * is referred to as KPPS.  KPPS is accessed on Linux via /dev/ppsN
 * devices.  On BSD it is accessed via the same device as the serial
 * port.  This mechanism is preferred as it should provide the smallest
 * latency and jitter from control line transition to timestamp.
 *
 * The other mechanism is user-space PPS, which uses the (not
 * standardized) TIOCMIWAIT ioctl to wait for PPS transitions on
 * serial port control lines.  It is implemented on Linux and OpenBSD.
 *
 * On Linux, RFC2783 PPS requires root permissions for initialization;
 * user-space PPS does not.  User-space PPS loses some functionality
 * when not initialized as root.  In gpsd, user-space PPS is referred
 * to as "plain PPS".
 *
 * On {Free,Net}BSD, RFC2783 PPS should only require access to the
 * serial port, but details have not yet been tested and documented
 * here.
 *
 * Note that for easy debugging all logging from this file is prefixed
 * with PPS or KPPS.
 *
 * To use the thread manager, you need to first fill in the
 * devicefd, devicename, and the four hook function members in the thread
 * context structure. The void *context member is available for your hook
 * functions to use; the thread-monitor code doesn't touch it.
 *
 * After this setup, you can call pps_thread_activate() and the
 * thread will launch.  It is OK to do this before the device is open,
 * the thread will wait on that.
 *
 * WARNING!  Loss of precision
 * UNIX time to nanoSec precision is 62 significant bits
 * UNIX time to nanoSec precision after 2038 is 63 bits
 * a double is only 53 significant bits.
 *
 * You cannot do PPS math with doubles
 *
 * This file is Copyright 2013 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */

// #include "../include/gpsd_config.h"  /* must be before all includes */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>            /* pacifies OpenBSD's compiler */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/limits.h>

#include "log.h"

#define HAVE_SYS_TIMEPPS_H

/* use RFC 2783 PPS API */
/* this needs linux >= 2.6.34 and
 * CONFIG_PPS=y
 * CONFIG_PPS_DEBUG=y  [optional to kernel log pulses]
 * CONFIG_PPS_CLIENT_LDISC=y
 *
 * Also beware that setting
 * CONFIG_PPS_CLIENT_KTIMER=y
 * adds a fake software-generated PPS intended for testing.  This
 * doesn't even run at exactly 1Hz, so any attempt to use it for
 * real timing is disastrous.  Hence we try to avoid it.
 */
#define FAKE_PPS_NAME "ktimer"

#if defined(HAVE_SYS_TIMEPPS_H)
// include unistd.h here as it is missing on older pps-tools releases.
// 'close' is not defined otherwise.
#include <unistd.h>
#include <sys/timepps.h>
#endif

#include "timespec.h"
#include "os_compat.h"
#include "ppsthread.h"

/*
 * Tell GCC that we want thread-safe behavior with _REENTRANT;
 * in particular, errno must be thread-local.
 * Tell POSIX-conforming implementations with _POSIX_THREAD_SAFE_FUNCTIONS.
 * See http://www.unix.org/whitepapers/reentrant.html
 */
#ifndef _REENTRANT
#define _REENTRANT
#endif
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#define _POSIX_THREAD_SAFE_FUNCTIONS
#endif

/*
 * Warning: This is a potential portability problem.
 * It's needed so that TIOCMIWAIT will be defined and the plain PPS
 * code will work, but it's not a SuS/POSIX standard header.  We're
 * going to include it unconditionally here because we expect both
 * Linux and BSD to have it and we want compilation to break with
 * an audible snapping sound if it's not present.
 */
#include <sys/ioctl.h>

#if defined(HAVE_SYS_TIMEPPS_H)
#include <glob.h>
#include <fcntl.h>      /* needed for open() and friends */
#endif

#if defined(TIOCMIWAIT)
static int get_edge_tiocmiwait( volatile struct pps_thread_t *,
                         struct timespec *, int *,
                         volatile struct timedelta_t *);
#endif /* TIOCMIWAIT */

struct inner_context_t {
    volatile struct pps_thread_t        *pps_thread;
    bool pps_canwait;                   /* can RFC2783 wait? */
#if defined(HAVE_SYS_TIMEPPS_H)
    int pps_caps;                       /* RFC2783 getcaps() */
    pps_handle_t kernelpps_handle;
#endif /* defined(HAVE_SYS_TIMEPPS_H) */
};

#if defined(HAVE_SYS_TIMEPPS_H)
static int get_edge_rfc2783(struct inner_context_t *,
                         struct timespec *,
                         int *,
                         struct timespec *,
                         int *,
                         volatile struct timedelta_t *);
#endif  /* defined(HAVE_SYS_TIMEPPS_H) */

static pthread_mutex_t ppslast_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Version of strerror_r() which explicitly ignores the return value.
 * This is needed to avoid warnings from some overly pedantic compilers.
 * Unfortunately, the return type is platform-dependent, making it
 * impractical to actually use the return value.
 */
static void pps_strerror_r(int errnum, char *buf, size_t len)
{
    if (strerror_r(errnum, buf, len)) {
        return;
    } else {
        return;
    }
}

static void thread_lock(volatile struct pps_thread_t *pps_thread)
{
    int pthread_err = pthread_mutex_lock(&ppslast_mutex);
    if ( 0 != pthread_err ) {
        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        pps_thread->log_hook(pps_thread, THREAD_ERROR,
                "PPS:%s pthread_mutex_lock() : %s",
                pps_thread->devicename, errbuf);
    }
}

static void thread_unlock(volatile struct pps_thread_t *pps_thread)
{
    int pthread_err = pthread_mutex_unlock(&ppslast_mutex);
    if ( 0 != pthread_err ) {
        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        pps_thread->log_hook(pps_thread, THREAD_ERROR,
                    "TPPS:%s pthread_mutex_unlock() : %s",
                    pps_thread->devicename, errno, errbuf);
    }
}

#if defined(HAVE_SYS_TIMEPPS_H)
#ifdef __linux__
/* Obtain contents of specified sysfs variable; put in buf
 * empty (NUL-terminated) string if failure */
static void get_sysfs_var(const char *path, char *buf, size_t bufsize)
{
    buf[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (0 <= fd ) {
        ssize_t r = read(fd, buf, bufsize - 1);
        if (0 < r) {
            buf[r - 1] = '\0'; /* remove trailing \x0a */
        } else {
            // pacify coverity
            buf[0] = '\0';
        }
        (void)close(fd);
    }
}

/* Check to see whether the named PPS source is the fake one */
int pps_check_fake(const char *name) {
    char path[PATH_MAX] = "";
    char buf[32];

    memset(buf, 0, sizeof(buf));
    snprintf(path, sizeof(path), "/sys/devices/virtual/pps/%s/name", name);
    get_sysfs_var(path, buf, sizeof(buf));
    return strcmp(buf, FAKE_PPS_NAME) == 0;
}

/* Get first "real" PPS device, skipping the fake, if any */
char *pps_get_first(void)
{
    if (pps_check_fake("pps0"))
        return "/dev/pps1";
    return "/dev/pps0";
}
#endif /* __linux__ */

/* return handle for kernel pps, or -1; requires root privileges */
static int init_kernel_pps(struct inner_context_t *inner_context)
{
    pps_params_t pp;
    int ret;
#ifdef __linux__
    /* These variables are only needed by Linux to find /dev/ppsN. */
    int ldisc = 18;   /* the PPS line discipline */
    glob_t globbuf;

#endif
    char path[PATH_MAX] = "";
    volatile struct pps_thread_t *pps_thread = inner_context->pps_thread;

    inner_context->kernelpps_handle = (pps_handle_t)-1;
    inner_context->pps_canwait = false;

    /*
     * This next code block abuses "ret" by storing the file descriptor
     * to use for RFC2783 calls.
     */
#ifdef __linux__
    /*
     * Some Linuxes, like the RasPi's, have PPS devices preexisting.
     * Other OS have no way to automatically determine the proper /dev/ppsX.
     * Allow user to pass in an explicit PPS device path.
     *
     * (We use strncpy() here because this might be compiled where
     * strlcpy() is not available.)
     */
    if (strncmp(pps_thread->devicename, "/dev/pps", 8) == 0) {
        if (pps_check_fake(pps_thread->devicename + 5))
            pps_thread->log_hook(pps_thread, THREAD_WARN,
                                 "KPPS:%s is fake PPS,"
                                 " timing will be inaccurate",
                                 pps_thread->devicename);
        (void)strncpy(path, pps_thread->devicename, sizeof(path)-1);
    } else {
        char pps_num = '\0';  /* /dev/pps[pps_num] is our device */
        size_t i;             /* to match type of globbuf.gl_pathc */
        /*
         * Otherwise one must make calls to associate a serial port with a
         * /dev/ppsN device and then grovel in system data to determine
         * the association.
         */

        /* Attach the line PPS discipline, so no need to ldattach */
        /* This activates the magic /dev/pps0 device */
        /* Note: this ioctl() requires root, and device is a tty */
        if (0 > ioctl(pps_thread->devicefd, TIOCSETD, &ldisc)) {
            char errbuf[BUFSIZ] = "unknown error";
            pps_strerror_r(errno, errbuf, sizeof(errbuf));
            pps_thread->log_hook(pps_thread, THREAD_INF,
                                 "KPPS:%s cannot set PPS line discipline %s",
                                 pps_thread->devicename, errbuf);
            return -1;
        }

        /* uh, oh, magic file names!, RFC2783 neglects to specify how
         * to associate the serial device and pps device names */
        /* need to look in /sys/devices/virtual/pps/pps?/path
         * (/sys/class/pps/pps?/path is just a link to that)
         * to find the /dev/pps? that matches our serial port.
         * this code fails if there are more then 10 pps devices.
         *
         * yes, this could be done with libsysfs, but trying to keep
         * the number of required libs small, and libsysfs would still
         * be linux only */
        memset( (void *)&globbuf, 0, sizeof(globbuf));
        (void)glob("/sys/devices/virtual/pps/pps?/path", 0, NULL, &globbuf);

        memset( (void *)&path, 0, sizeof(path));
        for ( i = 0; i < globbuf.gl_pathc; i++ ) {
            get_sysfs_var(globbuf.gl_pathv[i], path, sizeof(path));
            pps_thread->log_hook(pps_thread, THREAD_PROG,
                                 "KPPS:%s checking %s, %s",
                                 pps_thread->devicename,
                                 globbuf.gl_pathv[i], path);
            if (0 == strncmp( path, pps_thread->devicename, sizeof(path))) {
                /* this is the pps we are looking for */
                /* FIXME, now build the proper pps device path */
                pps_num = globbuf.gl_pathv[i][28];
                break;
            }
            memset( (void *)&path, 0, sizeof(path));
        }
        /* done with blob, clear it */
        globfree(&globbuf);

        if ( 0 == (int)pps_num ) {
            pps_thread->log_hook(pps_thread, THREAD_INF,
                                 "KPPS:%s device not found.",
                                 pps_thread->devicename);
            return -1;
        }

        /* construct the magic device path */
        (void)snprintf(path, sizeof(path), "/dev/pps%c", pps_num);
    }

    /* root privs are probably required for this device open
     * do not bother to check uid, just go for the open() */

    ret = open(path, O_RDWR);
    if (0 > ret) {
        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        // sometimes getuid() and geteuid() are long
        pps_thread->log_hook(pps_thread, THREAD_INF,
                    "KPPS:%s running as %ld/%ld, cannot open %s: %s",
                    pps_thread->devicename,
                    (long)getuid(), (long)geteuid(),
                    path, errbuf);
        return -1;
    }
#else /* not __linux__ */
    /*
     * On BSDs that support RFC2783, one uses the API calls on serial
     * port file descriptor.
     *
     * FIXME! need more specific than 'not linux'
     */
    (void)strlcpy(path, pps_thread->devicename, sizeof(path));
    ret  = pps_thread->devicefd;
#endif

    /* assert(ret >= 0); */
    pps_thread->log_hook(pps_thread, THREAD_INF,
                "KPPS:%s RFC2783 path:%s, fd is %d",
                pps_thread->devicename, path,
                ret);

    /* RFC 2783 implies the time_pps_setcap() needs privileges *
     * keep root a tad longer just in case */
    if (0 > time_pps_create(ret,
                            (pps_handle_t *)&inner_context->kernelpps_handle)) {
        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        pps_thread->log_hook(pps_thread, THREAD_INF,
                    "KPPS:%s time_pps_create(%d) failed: %s",
                    pps_thread->devicename,
                    ret, errbuf);
        return -1;
    }


    /* have kernel PPS handle */
    /* get RFC2783 features supported */
    inner_context->pps_caps = 0;
    if ( 0 > time_pps_getcap(inner_context->kernelpps_handle,
                                &inner_context->pps_caps)) {
        char errbuf[BUFSIZ] = "unknown error";
        inner_context->pps_caps = 0;
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        pps_thread->log_hook(pps_thread, THREAD_INF,
                    "KPPS:%s time_pps_getcap() failed: %.100s",
                    pps_thread->devicename, errbuf);
        return -1;
    } else {
        pps_thread->log_hook(pps_thread, THREAD_INF,
                    "KPPS:%s pps_caps 0x%02X",
                    pps_thread->devicename,
                    inner_context->pps_caps);
    }

    /* construct the setparms structure */
    memset( (void *)&pp, 0, sizeof(pps_params_t));
    pp.api_version = PPS_API_VERS_1;  /* version 1 protocol */
    if ( 0 == (PPS_TSFMT_TSPEC & inner_context->pps_caps ) ) {
       /* PPS_TSFMT_TSPEC means return a timespec
        * mandatory for driver to implement, require it */
        pps_thread->log_hook(pps_thread, THREAD_WARN,
                    "KPPS:%s fail, missing mandatory PPS_TSFMT_TSPEC",
                    pps_thread->devicename);
        return -1;
    }
    if ( 0 != (PPS_CANWAIT & inner_context->pps_caps ) ) {
       /* we can wait! so no need for TIOCMIWAIT */
       pps_thread->log_hook(pps_thread, THREAD_INF,
                   "KPPS:%s have PPS_CANWAIT",
                   pps_thread->devicename);
        inner_context->pps_canwait = true;
    }

    pp.mode = PPS_TSFMT_TSPEC;
    switch ((PPS_CAPTUREASSERT | PPS_CAPTURECLEAR) & inner_context->pps_caps) {
    case PPS_CAPTUREASSERT:
        pps_thread->log_hook(pps_thread, THREAD_WARN,
                    "KPPS:%s missing PPS_CAPTURECLEAR, pulse may be offset",
                    pps_thread->devicename);
        pp.mode |= PPS_CAPTUREASSERT;
        break;
    case PPS_CAPTURECLEAR:
        pps_thread->log_hook(pps_thread, THREAD_WARN,
                    "KPPS:%s missing PPS_CAPTUREASSERT, pulse may be offset",
                    pps_thread->devicename);
        pp.mode |= PPS_CAPTURECLEAR;
        break;
    case PPS_CAPTUREASSERT | PPS_CAPTURECLEAR:
        pp.mode |= PPS_CAPTUREASSERT | PPS_CAPTURECLEAR;
        break;
    default:
        /* THREAD_ERR in the calling routine */
        pps_thread->log_hook(pps_thread, THREAD_INF,
                    "KPPS:%s missing PPS_CAPTUREASSERT and CLEAR",
                    pps_thread->devicename);
        return -1;
    }

    if ( 0 > time_pps_setparams(inner_context->kernelpps_handle, &pp)) {
        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        pps_thread->log_hook(pps_thread, THREAD_ERROR,
            "KPPS:%s time_pps_setparams(mode=0x%02X) failed: %s",
            pps_thread->devicename, pp.mode,
            errbuf);
        (void)time_pps_destroy(inner_context->kernelpps_handle);
        return -1;
    }
    return 0;
}
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

#if defined(TIOCMIWAIT)
/* wait for, and get, an edge using TIOCMIWAIT
 * return -1 for error
 *         0 for OK
 */
static int get_edge_tiocmiwait( volatile struct pps_thread_t *thread_context,
                                struct timespec *clock_ts,
                                int *state,
                                volatile struct timedelta_t *last_fixtime)
{
    char ts_str[TIMESPEC_LEN];

    /* we are lucky to have TIOCMIWAIT, so wait for next edge */
    #define PPS_LINE_TIOC (TIOCM_CD|TIOCM_RI|TIOCM_CTS|TIOCM_DSR)
    /*
     * DB9  DB25  Name      Full name
     * ---  ----  ----      --------------------
     *  3     2    TXD  --> Transmit Data
     *  2     3    RXD  <-- Receive Data
     *  7     4    RTS  --> Request To Send
     *  8     5    CTS  <-- Clear To Send
     *  6     6    DSR  <-- Data Set Ready
     *  4    20    DTR  --> Data Terminal Ready
     *  1     8    DCD  <-- Data Carrier Detect
     *  9    22    RI   <-- Ring Indicator
     *  5     7    GND      Signal ground
     *
     * Wait for status change on any handshake line.  Just one edge,
     * we do not want to be spinning waiting for the trailing edge of
     * a pulse. The only assumption here is that no GPS lights up more
     * than one of these pins.  By waiting on all of them we remove a
     * configuration switch.
     *
     * Note that it only makes sense to wait on handshake lines
     * activated from the receive side (DCE->DTE) here; in this
     * context "DCE" is the GPS. {CD,RI,CTS,DSR} is the
     * entire set of these.
     *
     */

    if (ioctl(thread_context->devicefd, TIOCMIWAIT, PPS_LINE_TIOC) != 0) {
        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        thread_context->log_hook(thread_context, THREAD_WARN,
                "TPPS:%s ioctl(TIOCMIWAIT) failed: %d %.40s",
                thread_context->devicename, errno, errbuf);
        return -1;;
    }

    /*
     * Start of time critical section
     * Only error reporting, not success reporting in critical section
     */

    /* duplicate copy in get_edge_rfc2783 */
    /* quick, grab a copy of last_fixtime before it changes */
    thread_lock(thread_context);
    *last_fixtime = thread_context->fix_in;
    thread_unlock(thread_context);
    /* end duplicate copy in get_edge_rfc2783 */
    /* get the time after we just woke up */
    if ( 0 > clock_gettime(CLOCK_REALTIME, clock_ts) ) {
        /* uh, oh, can not get time! */
        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        thread_context->log_hook(thread_context, THREAD_ERROR,
                    "TPPS:%s clock_gettime() failed: %.100s",
                    thread_context->devicename, errbuf);
        return -1;;
    }

    /* got the edge, got the time just after the edge, now quickly
     * get the edge state */
    if (ioctl(thread_context->devicefd, (unsigned long)TIOCMGET, state) != 0) {
        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        thread_context->log_hook(thread_context, THREAD_ERROR,
                    "TPPS:%s ioctl(TIOCMGET) failed: %.100s",
                    thread_context->devicename, errbuf);
        return -1;
    }
    /* end of time critical section */
    /* mask for monitored lines */

    *state &= PPS_LINE_TIOC;

    thread_context->log_hook(thread_context, THREAD_PROG,
                "TPPS:%s ioctl(TIOCMIWAIT) succeeded, time:%s,  state: %d",
                thread_context->devicename,
                timespec_str(clock_ts, ts_str, sizeof(ts_str)),
                *state);

    return 0;
}

#endif /* TIOCMIWAIT */

#if defined(HAVE_SYS_TIMEPPS_H)
/* wait for, and get, last two edges using RFC2783
 * return -1 for error
 *         0 for OK
 *         1 no edge found, continue
 *
 * on a quad core 2.4GHz Xeon using KPPS timestamp instead of plain
 * PPS timestamp removes about 20uS of latency, and about +/-5uS
 * of jitter
 */
static int get_edge_rfc2783(struct inner_context_t *inner_context,
                            struct timespec *prev_clock_ts,
                            int *prev_edge,
                            struct timespec *clock_ts,
                            int *edge,
                            volatile struct timedelta_t *last_fixtime)
{
    pps_info_t pi;
    char ts_str1[TIMESPEC_LEN], ts_str2[TIMESPEC_LEN];
    struct timespec kernelpps_tv;
    volatile struct pps_thread_t *thread_context = inner_context->pps_thread;

    if ( inner_context->pps_canwait ) {
        /*
         * RFC2783 specifies that a NULL timeval means to wait, if
         * PPS_CANWAIT is available.
         *
         * since we pps_canwait, we skipped the TIOMCIWAIT
         *
         * 3 second time out, some GPS output 0.5Hz and some RFC2783
         * can only trigger on one edge
         * a better and more complex solution would be to wait
         * for 1/20 second and suffer the cycles
         */
        kernelpps_tv.tv_sec = 3;
        kernelpps_tv.tv_nsec = 0;
    } else {
        /*
         * We use of a non-NULL zero timespec here,
         * which means to return immediately with -1 (section
         * 3.4.3).  This is because we know we just got a pulse because
         * TIOCMIWAIT just woke up.
         * The timestamp has already been captured in the kernel, and we
         * are merely fetching it here.
         */
        memset( (void *)&kernelpps_tv, 0, sizeof(kernelpps_tv));
    }
    memset( (void *)&pi, 0, sizeof(pi)); /* paranoia */
    if (0 > time_pps_fetch(inner_context->kernelpps_handle, PPS_TSFMT_TSPEC,
                            &pi, &kernelpps_tv)) {

        char errbuf[BUFSIZ] = "unknown error";
        pps_strerror_r(errno, errbuf, sizeof(errbuf));
        if (ETIMEDOUT == errno || EINTR == errno) {
                /* just a timeout */
                thread_context->log_hook(thread_context, THREAD_INF,
                                         "KPPS:%s kernel PPS timeout %s",
                                         thread_context->devicename, errbuf);
                return 1;
        }
        thread_context->log_hook(thread_context, THREAD_WARN,
                                 "KPPS:%s kernel PPS failed %s",
                                 thread_context->devicename, errbuf);
        return 0;
    }

    if ( inner_context->pps_canwait ) {
        /* get_edge_tiocmiwait() got this if !pps_canwait */

        /* quick, grab a copy of last fixtime before it changes */
        thread_lock(thread_context);
        *last_fixtime = thread_context->fix_in;
        thread_unlock(thread_context);
    }


    // find the last edge
    if (pi.assert_timestamp.tv_sec > pi.clear_timestamp.tv_sec) {
        /* assert 1 sec or more after than clear */
        *edge = 1;
    } else if (pi.assert_timestamp.tv_sec < pi.clear_timestamp.tv_sec) {
        /* assert 1 sec or more before than clear */
        *edge = 0;
    } else if (pi.assert_timestamp.tv_nsec > pi.clear_timestamp.tv_nsec) {
        /* assert less than 1 sec after clear */
        *edge = 1;
    } else {
        /* assert less than 1 sec before clear */
        *edge = 0;
    }
    if (1 == *edge) {
        /* assert after clear */
        *prev_edge = 0;
        if (0 == pi.clear_timestamp.tv_sec) {
                /* brain damaged pps-gpio sometimes never fills in clear
                 * so make it look like an invisible pulse
                 * if clear is the leading edge, then we are off by the
                 * pulse width */
                *prev_clock_ts = pi.assert_timestamp;
        } else {
                *prev_clock_ts = pi.clear_timestamp;
        }
        *clock_ts = pi.assert_timestamp;
    } else {
        /* assert before clear */
        *prev_edge = 1;
        *prev_clock_ts = pi.assert_timestamp;
        *clock_ts = pi.clear_timestamp;
    }
    /*
     * pps_seq_t is uint32_t on NetBSD, so cast to
     * unsigned long as a wider-or-equal type to
     * accommodate Linux's type.
     */
    thread_context->log_hook(thread_context, THREAD_RAW,
                             "KPPS:%s assert %s, sequence: %lu, "
                             "clear  %s, sequence: %lu - using: %.10s",
                             thread_context->devicename,
                             timespec_str(&pi.assert_timestamp,
                                          ts_str1, sizeof(ts_str1)),
                             (unsigned long) pi.assert_sequence,
                             timespec_str(&pi.clear_timestamp,
                                          ts_str2, sizeof(ts_str2)),
                             (unsigned long) pi.clear_sequence,
                             *edge ? "assert" : "clear");

    return 0;
}
#endif  /* defined(HAVE_SYS_TIMEPPS_H) */

/* gpsd_ppsmonitor()
 *
 * the core loop of the PPS thread.
 * All else is initialization, cleanup or subroutine
 */
static void *gpsd_ppsmonitor(void *arg)
{
    char ts_str1[TIMESPEC_LEN], ts_str2[TIMESPEC_LEN];
    struct inner_context_t inner_context = *((struct inner_context_t *)arg);
    volatile struct pps_thread_t *thread_context = inner_context.pps_thread;
    /* the GPS time and system clock time, to the nSec,
     * when the last fix received
     * using a double would cause loss of precision */
    volatile struct timedelta_t last_fixtime = {{0, 0}, {0, 0}};
    struct timespec clock_ts = {0, 0};
    time_t last_second_used = 0;
    int64_t cycle = 0, duration = 0;
    /* state is the last state of the tty control signals */
    int state = 0;
    /* count of how many cycles unchanged data */
    int  unchanged = 0;
    /* state_last is previous state */
    int state_last = 0;
    /* edge, used as index into pulse to find previous edges */
    int edge = 0;       /* 0 = clear edge, 1 = assert edge */

#if defined(TIOCMIWAIT)
    int edge_tio = 0;
    int64_t cycle_tio = 0;
    int64_t duration_tio = 0;
    int state_tio = 0;
    int state_last_tio = 0;
    struct timespec clock_ts_tio = {0, 0};
    /* pulse stores the time of the last two edges */
    struct timespec pulse_tio[2] = { {0, 0}, {0, 0} };
#endif /* TIOCMIWAIT */

#if defined(HAVE_SYS_TIMEPPS_H)
    int64_t cycle_kpps = 0, duration_kpps = 0;
    /* kpps_pulse stores the time of the last two edges */
    struct timespec pulse_kpps[2] = { {0, 0}, {0, 0} };
#endif /* defined(HAVE_SYS_TIMEPPS_H) */
    bool not_a_tty = false;

    /* Acknowledge that we've grabbed the inner_context data */
    ((volatile struct inner_context_t *)arg)->pps_thread = NULL;

    /* before the loop, figure out how we can detect edges:
     * TIOMCIWAIT, which is linux specific
     * RFC2783, a.k.a kernel PPS (KPPS)
     * or if KPPS is deficient a combination of the two */
    if (0 > thread_context->devicefd ||
        0 == isatty(thread_context->devicefd)) {
        thread_context->log_hook(thread_context, THREAD_PROG,
            "KPPS:%s gps_fd:%d not a tty, can not use TIOMCIWAIT",
            thread_context->devicename,
            thread_context->devicefd);
        /* why do we care the device is a tty? so as not to ioctl(TIO..)
        * /dev/pps0 is not a tty and we need to use it */
        not_a_tty = true;
    }
    /* if no TIOCMIWAIT, we hope to have PPS_CANWAIT */

    if ( not_a_tty && !inner_context.pps_canwait ) {
        /* for now, no way to wait for an edge, in the future maybe figure out
         * a sleep */
    }

    /*
     * this is the main loop, exit and never any further PPS processing.
     *
     * Four stages to the loop,
     * an unwanted condition at any point and the loop restarts
     * an error condition and we exit for all time.
     *
     * Stage One: wait for the next edge.
     *      If we have KPPS
     *         If we have PPS_CANWAIT
     *             use KPPS and PPS_CANWAIT - this is the most accurate
     *         else
     *             use KPPS and TIOMCIWAIT together - this is pretty accurate
     *      else If we have TIOMCIWAIT
     *         use TIOMCIWAIT - this is the least accurate
     *      else
     *         give up
     *
     * Success is we have a good edge, otherwise loop some more
     *
     * On a successful stage one, we know this about the exact moment
     * of current pulse:
     *      GPS (real) time
     *      system (clock) time
     *      edge type: Assert (rising) or Clear (falling)
     *
     * From the above 3 items, we can compute:
     *      cycle length - elapsed time from the previous edge of the same type
     *      pulse length (duration) - elapsed time from the previous edge
     *                            (the previous edge would be the opposite type)
     *
     * Stage Two:  Categorize the current edge
     *      Decide if we have 0.5Hz, 1Hz, 5 Hz cycle time
     *      knowing cycle time determine if we have the leading or trailing edge
     *      restart the loop if the edge looks dodgy
     *
     * Stage Three: Calculate
     *      Calculate the offset (difference) between the system time
     *      and the GPS time at the pulse moment
     *      restart the loop if the offset looks dodgy
     *
     * Stage Four: Tell ntpd, chronyd, or gpsmon what we learned
     *       a few more sanity checks
     *       call the report hook with our PPS report
     */
    while (thread_context->report_hook != NULL) {
        bool ok = false;
        char *log = NULL;
        char *edge_str = "";
        if (++unchanged == 10) {
            /* last ten edges no good, stop spinning, just wait 10 seconds */
            unchanged = 0;
            thread_context->log_hook(thread_context, THREAD_WARN,
                    "PPS:%s unchanged state, ppsmonitor sleeps 10",
                    thread_context->devicename);
            (void)sleep(10);
        }

        /* Stage One; wait for the next edge */
#if defined(TIOCMIWAIT)
        if ( !not_a_tty && !inner_context.pps_canwait ) {
            int ret;

            /* we are a tty, so can TIOCMIWAIT */
            /* we have no PPS_CANWAIT, so must TIOCMIWAIT */

            ret = get_edge_tiocmiwait( thread_context, &clock_ts_tio,
                        &state_tio, &last_fixtime );
            if ( 0 != ret ) {
                thread_context->log_hook(thread_context, THREAD_PROG,
                            "PPS:%s die: TIOCMIWAIT Error",
                            thread_context->devicename);
                break;
            }

            edge_tio = (state_tio > state_last_tio) ? 1 : 0;

            state_last_tio = state_tio;

            /* three things now known about the current edge:
             * clock_ts - time of the edge
             * state - the serial line input states
             * edge - rising edge (1), falling edge (0) or invisible edge (0)
             */

            /* calculate cycle and duration from previous edges */
            cycle_tio = timespec_diff_ns(clock_ts_tio, pulse_tio[edge_tio]);
            cycle_tio /= 1000;  /* nsec to usec */
            duration_tio = timespec_diff_ns(clock_ts_tio,
                                            pulse_tio[edge_tio ? 0 : 1]) / 1000;

            /* save this edge so we know next cycle time */
            pulse_tio[edge_tio] = clock_ts_tio;

            /* use this data */
            ok = true;
            clock_ts = clock_ts_tio;
            state = edge_tio;
            edge = edge_tio;
            edge_str = edge ? "Assert" : "Clear";
            cycle = cycle_tio;
            duration = duration_tio;

            // (long long) for 32-bit compat.  PRId64 segfaults
            thread_context->log_hook(thread_context, THREAD_RAW,
                    "TPPS:%s %.10s, cycle: %lld, duration: %lld @ %s",
                    thread_context->devicename, edge_str,
                    (long long)cycle, (long long)duration,
                    timespec_str(&clock_ts, ts_str1, sizeof(ts_str1)));

        }
#endif /* TIOCMIWAIT */

        /* ok and log used by KPPS and TIOCMIWAIT */
        log = NULL;
#if defined(HAVE_SYS_TIMEPPS_H)
        if ((pps_handle_t)0 <= inner_context.kernelpps_handle) {
            int ret;
            int edge_kpps = 0;       /* 0 = clear edge, 1 = assert edge */
            /* time of the last edge */
            struct timespec clock_ts_kpps = {0, 0};
            /* time of the edge before the last edge */
            struct timespec prev_clock_ts = {0, 0};
            /* direction of next to last edge 1 = assert, 0 = clear */
            int prev_edge = 0;

            /* get last and previous edges, in order
             * optionally wait for good data
             */
            ret = get_edge_rfc2783(&inner_context,
                         &prev_clock_ts,
                         &prev_edge,
                         &clock_ts_kpps,
                         &edge_kpps,
                         &last_fixtime);
            if ( -1 == ret ) {
                /* error, so break */
                thread_context->log_hook(thread_context, THREAD_ERROR,
                            "PPS:%s die: RFC2783 Error",
                            thread_context->devicename);
                break;
            }

            if ( 1 == ret ) {
                /* no edge found, so continue */
                /* maybe use TIOCMIWAIT edge instead?? */
                continue;
            }
            /* for now, as we have been doing all of gpsd 3.x, just
             *use the last edge, not the previous edge */
            /* compute time from previous saved similar edge */
            cycle_kpps = timespec_diff_ns(clock_ts_kpps, pulse_kpps[edge_kpps]);
            cycle_kpps /= 1000;
            /* compute time from previous saved dis-similar edge */
            duration_kpps = timespec_diff_ns(clock_ts_kpps, prev_clock_ts) /
                            1000;

            /* save for later */
            pulse_kpps[edge_kpps] = clock_ts_kpps;
            pulse_kpps[edge_kpps ? 0 : 1] = prev_clock_ts;
            /* sanity checks are later */

            /* use this data */
            state = edge_kpps;
            edge = edge_kpps;
            edge_str = edge ? "Assert" : "Clear";
            clock_ts = clock_ts_kpps;
            cycle = cycle_kpps;
            duration = duration_kpps;
            // (long long) for 32-bit compat.  PRId64 segfaults
            thread_context->log_hook(thread_context, THREAD_RAW,
                "KPPS:%s %.10s cycle: %lld, duration: %lld @ %s",
                thread_context->devicename,
                edge_str,
                (long long)cycle_kpps, (long long)duration_kpps,
                timespec_str(&clock_ts_kpps, ts_str1, sizeof(ts_str1)));
        }
#endif /* defined(HAVE_SYS_TIMEPPS_H) */

        if ( not_a_tty && !inner_context.pps_canwait ) {
            /* uh, oh, no TIOMCIWAIT, nor RFC2783, die */
            thread_context->log_hook(thread_context, THREAD_WARN,
                        "PPS:%s die: no TIOMCIWAIT, nor RFC2783 CANWAIT",
                        thread_context->devicename);
            break;
        }
        /*
         * End of Stage One
         * we now know this about the exact moment of current pulse:
         *      GPS (real) time
         *      system (clock) time
         *      edge type: Assert (rising) or Clear (falling)
         *
         * we have computed:
         *      cycle length
         *      pulse length (duration)
         */

        /*
         * Stage Two:  Categorize the current edge
         *      Decide if we have 0.5Hz, 1Hz, 5 Hz cycle time
         *      determine if we have the leading or trailing edge
         */
        /* FIXME! this block duplicates a lot of the next block
         * of cycle detection code */
        if (state != state_last) {
            thread_context->log_hook(thread_context, THREAD_RAW,
                        "PPS:%s %.10s pps-detect changed to %d",
                        thread_context->devicename, edge_str, state);
            unchanged = 0;
        } else if ( (180000 < cycle &&  220000 > cycle)      /* 5Hz */
                ||  (900000 < cycle && 1100000 > cycle)      /* 1Hz */
                || (1800000 < cycle && 2200000 > cycle) ) {  /* 0.5Hz */

            /* some pulses may be so short that state never changes
             * and some RFC2783 only can detect one edge */

            duration = 0;
            unchanged = 0;
            thread_context->log_hook(thread_context, THREAD_RAW,
                        "PPS:%s %.10s pps-detect invisible pulse",
                        thread_context->devicename, edge_str);
        }
        /* else, unchanged state, and weird cycle time */

        state_last = state;
        // (long long) for 32-bit compat.  PRId64 segfaults
        thread_context->log_hook(thread_context, THREAD_RAW,
            "PPS:%s %.10s cycle: %lld, duration: %lld @ %s",
            thread_context->devicename,
            edge_str,
            (long long)cycle, (long long)duration,
            timespec_str(&clock_ts, ts_str1, sizeof(ts_str1)));
        if (unchanged) {
            // strange, try again
            continue;
        }

        /*
         * The PPS pulse is normally a short pulse with a frequency of
         * 1 Hz, and the UTC second is defined by the front edge. But we
         * don't know the polarity of the pulse (different receivers
         * emit different polarities). The duration variable is used to
         * determine which way the pulse is going.  When the duration
         * is less than 1/2 the cycle we are on the trailing edge.
         *
         * Some GPSes instead output a square wave that is 0.5 Hz and each
         * edge denotes the start of a second.
         *
         * Some GPSes, like the Globalsat MR-350P, output a 1uS pulse.
         * The pulse is so short that TIOCMIWAIT sees a state change
         * but by the time TIOCMGET is called the pulse is gone.  gpsd
         * calls that an invisible pulse.
         *
         * A few stupid GPSes, like the Furuno GPSClock, output a 1.0 Hz
         * square wave where the leading edge is the start of a second
         * gpsd can only guess the correct edge.
         *
         * 5Hz GPS (Garmin 18-5Hz) pulses at 5Hz. Set the pulse length to
         * 40ms which gives a 160ms pulse before going high.
         *
         * You may think that PPS is very accurate, so the cycle time
         * valid window should be very small.  This is not the case,
         * The Raspberry Pi clock is very coarse when it starts and/or chronyd
         * may be doing a fast slew.  chronyd by default will slew up
         * to 8.334%!  So the cycle time as measured by the system clock
         * may be almost +/- 9%. Therefore, gpsd uses a 10% window.
         * Don't worry, ntpd and chronyd will do further validation.
         */

        log = "Unknown error";
        if (0 > cycle) {
            log = "Rejecting negative cycle";
        } else if (180000 > cycle) {
            /* shorter than 200 milliSec - 10%
             * too short to even be a 5Hz pulse */
            log = "Too short for 5Hz";
        } else if (201000 > cycle) {
            /* longer than 200 milliSec - 10%
             * shorter than 200 milliSec + 10%
             * about 200 milliSec cycle */
            /* looks like 5hz PPS pulse */
            if (100000 > duration) {
                /* this is the end of the long part */
                /* BUG: how does the code know to tell ntpd
                 * which 1/5 of a second to use?? */
                ok = true;
                log = "5Hz PPS pulse";
            }
        } else if (900000 > cycle) {
            /* longer than 200 milliSec + 10%
             * shorter than 1.000 Sec - 10% */
            /* Yes, 10% window.  The Raspberry Pi clock is very coarse
             * when it starts and chronyd may be doing a fast slew.
             * chronyd by default will slew up to 8.334% ! */
            log = "Too long for 5Hz, too short for 1Hz";
        } else if (1100000 > cycle) {
            /* longer than 1.000 Sec - 10%
             * shorter than 1.000 Sec + 10% */
            /* Yes, 10% window.  */
            /* looks like 1Hz PPS pulse or square wave */
            if (0 == duration) {
                ok = true;
                log = "invisible pulse";
            } else if (450000 > duration) {
                /* pulse shorter than 500 milliSec - 10%
                 * end of the short "half" of the cycle
                 * aka the trailing edge */
                log = "1Hz trailing edge";
            } else if (555000 > duration) {
                /* pulse longer than 500 milliSec - 10%
                 * pulse shorter than 500 milliSec + 10%
                 * looks like 1.0 Hz square wave, ignore trailing edge
                 * except we can't tell which is which, so we guess */
                if (edge == 1) {
                    ok = true;
                    log = "square";
                }
            } else {
                /* pulse longer than 500 milliSec + 10%
                 * end of the long "half" of the cycle
                 * aka the leading edge,
                 * the edge that marks the start of the second */
                ok = true;
                log = "1Hz leading edge";
            }
        } else if (1800000 > cycle) {
            /* cycle longer than 1.000 Sec + 10%
             * cycle shorter than 2.000 Sec - 10%
             * Too long for 1Hz, too short for 2Hz */
            log = "Too long for 1Hz, too short for 2Hz";
        } else if (2200000 > cycle) {
            /* cycle longer than 2.000 Sec - 10%
             * cycle shorter than 2.000 Sec + 10%
             * looks like 0.5 Hz square wave */
            if (990000 > duration) {
                 /* pulse shorter than 1.000 Sec - 10%
                  * too short to be a 2Hx square wave */
                log = "0.5 Hz square too short duration";
            } else if (1100000 > duration) {
                 /* pulse longer than 1.000 Sec - 10%
                  * pulse shorter than 1.000 Sec + 10%
                  * and nice 0.5Hz square wave */
                ok = true;
                log = "0.5 Hz square wave";
            } else {
                log = "0.5 Hz square too long duration";
            }
        } else {
            /* cycle longer than 2.000 Sec + 10%
             * can't be anything */
            log = "Too long for 0.5Hz";
        }

        /* end of Stage two
         * we now know what type of PPS pulse, and if we have  a good
         * leading edge or not
         */

        /* Stage Three: Calculate
         *      Calculate the offset (difference) between the system time
         *      and the GPS time at the pulse moment
         */
        /*
         * If there has not yet been any valid in-band time stashed
         * from the GPS when the PPS event was asserted, we can do
         * nothing further.  gpsd can not tell what second this pulse is
         * in reference to.
         *
         * Some GPSes like Garmin always send a PPS, valid or not.
         * Other GPSes like some uBlox may only send PPS when time is valid.
         * It is common to get PPS, and no fixtime, while autobauding.
         */
        /* FIXME, some GPS, like Skytraq, may output a the fixtime so
         * late in the cycle as to be ambiguous. */
        if (last_fixtime.real.tv_sec == 0) {
            /* probably should log computed offset just for grins here */
            ok = false;
            log = "missing last_fixtime";
			log_error(log);
        } else if (ok && last_second_used >= last_fixtime.real.tv_sec) {
            /* uh, oh, this second already handled */
            ok = false;
            log = "this second already handled";
			log_error(log);
        }

        if (!ok) {
            /* can not use this pulse, reject and retry */
            thread_context->log_hook(thread_context, THREAD_PROG,
                        "PPS:%s %.10s ignored %.100s",
                        thread_context->devicename, edge_str,  log);
            continue;
        }

        /* we have validated a good cycle, mark it */
        unchanged = 0;
        /* offset is the skew from expected to observed pulse time */
        struct timespec offset;
        /* offset as a printable string */
        char offset_str[TIMESPEC_LEN];
        /* delay after last fix */
        struct timespec  delay;
        /* delay as a printable string */
        char delay_str[TIMESPEC_LEN];
        char *log1 = "";
        /* ppstimes.real is the time we think the pulse represents  */
        struct timedelta_t ppstimes;
        thread_context->log_hook(thread_context, THREAD_RAW,
                    "PPS:%s %.10s categorized %.100s",
                    thread_context->devicename, edge_str, log);

        /* FIXME! The GR-601W at 38,400 or faster can send the
         * serial fix before the interrupt event carrying the PPS
         * line assertion by about 10 mSec!
         */

        /*
         * We get the time of the last fix recorded before the PPS came in,
         * which is for the previous cycle.  Only works for integral cycle
         * times, but more than 1Hz is pointless.
         */

        ppstimes.real.tv_sec = (time_t)last_fixtime.real.tv_sec + 1;
        ppstimes.real.tv_nsec = 0;  /* need to be fixed for 5Hz */
        ppstimes.clock = clock_ts;

        // Here would be a good place to apply qErr
        TS_SUB( &offset, &ppstimes.real, &ppstimes.clock);
        TS_SUB( &delay, &ppstimes.clock, &last_fixtime.clock);
        timespec_str(&delay, delay_str, sizeof(delay_str));

        /* end Stage Three: now known about the exact edge moment:
         *      UTC time of PPS edge
         *      offset of system time to PS time
         */

        /* Stage Four: Tell ntpd, chronyd, or gpsmon what we learned
         *       a few more sanity checks
         *       call the report hook with our PPS report
         */
        if (0 > delay.tv_sec || 0 > delay.tv_nsec) {
            thread_context->log_hook(thread_context, THREAD_WARN,
                        "PPS:%s %.10s system clock went backwards: %.20s",
                        thread_context->devicename,
                        edge_str,
                        delay_str);
            log1 = "system clock went backwards";
        } else if ((2 < delay.tv_sec)
          || (1 == delay.tv_sec && 100000000 < delay.tv_nsec)) {
            /* system clock could be slewing so allow up to 1.1 sec delay */
            /* chronyd can slew +/-8.33% */
            thread_context->log_hook(thread_context, THREAD_WARN,
                        "PPS:%s %.10s no current GPS seconds: %.20s",
                        thread_context->devicename,
                        edge_str,
                        delay_str);
            log1 = "timestamp out of range";
        } else {
            if (thread_context->report_hook != NULL)
                log1 = thread_context->report_hook(thread_context, &ppstimes);
            else
                log1 = "no report hook";
            thread_lock(thread_context);
            thread_context->pps_out = ppstimes;
            thread_context->ppsout_count++;
            thread_unlock(thread_context);
            thread_context->log_hook(thread_context, THREAD_RAW,
                "PPS:%s %.10s hooks called clock: %s real: %s: %.20s",
                thread_context->devicename,
                edge_str,
                timespec_str(&ppstimes.clock, ts_str1, sizeof(ts_str1)),
                timespec_str(&ppstimes.real, ts_str2, sizeof(ts_str2)),
                log1);
        }
        thread_context->log_hook(thread_context, THREAD_RAW,
                "PPS:%s %.10s %.30s @ %s offset %.20s",
                thread_context->devicename,
                edge_str,
                log1,
                timespec_str(&clock_ts, ts_str1, sizeof(ts_str1)),
                timespec_str(&offset, offset_str, sizeof(offset_str)));
        /* end Stage four, end of the loop, do it again */
    }
#if defined(HAVE_SYS_TIMEPPS_H)
    if (inner_context.kernelpps_handle > (pps_handle_t)0) {
        thread_context->log_hook(thread_context, THREAD_PROG,
            "KPPS:%s descriptor cleaned up",
            thread_context->devicename);
        (void)time_pps_destroy(inner_context.kernelpps_handle);
    }
#endif
    thread_context->log_hook(thread_context, THREAD_PROG,
                "PPS:%s gpsd_ppsmonitor exited.",
                thread_context->devicename);
    return NULL;
}

/*
 * Entry points begin here.
 */

/* activate a thread to watch the device's PPS transitions */
void pps_thread_activate(volatile struct pps_thread_t *pps_thread)
{
    int retval;
    pthread_t pt;
    struct timespec start_delay = {0, 1000000};  /* 1 ms */

    /*
     * FIXME: this launch code is not itself thread-safe!
     * It would be if inner_context could be auto, but the monitor
     * routine gets garbage when we try that.  Ideally the body
     * of this function would be guarded by a separate mutex.
     * Either that, or this should be an exception to the no-malloc rule.
     */
    static struct inner_context_t       inner_context;

    inner_context.pps_thread = pps_thread;
#if defined(HAVE_SYS_TIMEPPS_H)
    /* some operations in init_kernel_pps() require root privs */
    (void)init_kernel_pps(&inner_context);
    if ((pps_handle_t)0 <= inner_context.kernelpps_handle) {
        pps_thread->log_hook(pps_thread, THREAD_INF,
                    "KPPS:%s kernel PPS will be used",
                    pps_thread->devicename);
    } else {
        pps_thread->log_hook(pps_thread, THREAD_WARN,
                    "KPPS:%s kernel PPS unavailable, "
                    "PPS accuracy will suffer",
                    pps_thread->devicename);
    }
#else
    pps_thread->log_hook(pps_thread, THREAD_WARN,
                "KPPS:%s no HAVE_SYS_TIMEPPS_H, PPS accuracy will suffer",
                pps_thread->devicename);
#endif

    memset( &pt, 0, sizeof(pt));
    retval = pthread_create(&pt, NULL, gpsd_ppsmonitor, (void *)&inner_context);
    pps_thread->log_hook(pps_thread, THREAD_PROG, "PPS:%s thread %s",
                pps_thread->devicename,
                (retval==0) ? "launched" : "FAILED");
    /* The monitor thread may not run immediately, particularly on a single-
     * core machine, so we need to wait for it to acknowledge its copying
     * of the inner_context struct before proceeding.
     */
    while (inner_context.pps_thread)
        (void) nanosleep(&start_delay, NULL);
}

void pps_thread_deactivate(volatile struct pps_thread_t *pps_thread)
/* cleanly terminate PPS thread */
{
    pps_thread->report_hook = NULL;
}

/* thread-safe update of last fix time - only way we pass data in */
void pps_thread_fixin(volatile struct pps_thread_t *pps_thread,
                      volatile struct timedelta_t *fix_in)
{
    thread_lock(pps_thread);
    pps_thread->fix_in = *fix_in;
    thread_unlock(pps_thread);
}

/* thread-safe update of qErr and qErr_time - only way we pass data in */
void pps_thread_qErrin(volatile struct pps_thread_t *pps_thread,
                       long qErr, struct timespec qErr_time)
{
    thread_lock(pps_thread);
    pps_thread->qErr = qErr;
    pps_thread->qErr_time = qErr_time;
    thread_unlock(pps_thread);
}

/* return the delta at the time of the last PPS - only way we pass data out */
int pps_thread_ppsout(volatile struct pps_thread_t *pps_thread,
                      volatile struct timedelta_t *td)
{
    volatile int ret;

    thread_lock(pps_thread);
    *td = pps_thread->pps_out;
    ret = pps_thread->ppsout_count;
    thread_unlock(pps_thread);

    return ret;
}

/* end */

// vim: set expandtab shiftwidth=4
