#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <error.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>

#include "../src/oscillators/sim_oscillator.h"
#include "config.h"
#include "log.h"
#include "ptspair.h"
#include "utils.h"

/* simulation parameters */
#define SETPOINT_MIN               31500
#define SETPOINT_MAX               1016052
/* offset to apply at maximum at each turn in ns */
#define TREND_OFFSET_NS            10
/* maximum absolute value of the error offset to add at each turn */
#define ERROR_AMPLITUDE_NS         5
#define INITIAL_ERROR_AMPLITUDE_NS 10000000

#define SETPOINT_AMPLITUDE         ((SETPOINT_MAX - SETPOINT_MIN) + 1)

static void signal_handler(int signum) {
    log_info("Caught signal %s.\n", strsignal(signum));
    if (!loop) {
        log_error("Signalled twice, brutal exit.\n");
        exit(EXIT_FAILURE);
    }
    loop = false;
}

/* returns a random number in [min, max) */
static long random_in_range(long min, long max) {
    return (random() % (min - max)) + min;
}

/* compute what to add to the current phase offset */
static long compute_delta(uint32_t setpoint) {
    /* find the center of the setpoints interval */
    int  center      = (SETPOINT_MIN + SETPOINT_MAX) / 2;
    /* compute the alg. distance of the current setpoint to this center */
    int  distance    = abs((int)setpoint - center);
    /*
     * base offset added is so that the maximum distance increases the
     * offset by TREND_OFFSET ns
     */
    long base_offset = (TREND_OFFSET_NS * distance) / SETPOINT_AMPLITUDE;
    /* add a random error */
    int  error       = (random() % (2 * ERROR_AMPLITUDE_NS)) - ERROR_AMPLITUDE_NS;

    return base_offset + error;
}

static void cleanup(void) {
    unlink(CONTROL_FIFO_PATH);
}

int main(int argc, char* argv[]) {
    const char*       prog_name;
    int               ret;
    int               tfd;
    struct itimerspec its;
    int               flags;
    struct timeval    tv;
    fd_set            readfds;
    uint64_t          expired;
    ssize_t           sret;
    /* read from the 1PPS device by oscillatord containing the error */
    int32_t           phase_error;
    /* data written by oscillatord to the 1PPS device */
    int32_t           phase_offset;
    uint32_t          setpoint;
    time_t            seed;
    int               control_fifo_fd;
    int               phase_error_fd;
    const char*       phase_error_pts;
    struct ptspair __attribute__((cleanup(ptspair_clean))) pts;
    int           pts_fd;
    long long     period;
    const char*   path;
    const char*   period_str;
    struct config config;

    /* must be done early because of the attribute cleanup */
    memset(&pts, 0, sizeof(pts));
    seed = time(NULL);
    srand(seed);

    prog_name = basename(argv[0]);
    if (argc != 2)
        error(EXIT_FAILURE, 0, "%s config_file_path", prog_name);
    path = argv[1];

    ret  = config_init(&config, path);
    if (ret != 0)
        error(EXIT_FAILURE, -ret, "config_init(%s)", path);

    log_set_level(config_get_bool_default(&config, "enable-debug", false) ? LOG_DEBUG : LOG_INFO);

    /* TODO implement a config_get ull ? */
    period_str = config_get(&config, "simulation-period");
    period     = atoll(period_str ?: "1000000000");
    log_info("simulation period is %lldns\n", period);

    ret = ptspair_init(&pts);
    if (ret < 0)
        error(EXIT_FAILURE, -ret, "ptspair_init");
    pts_fd          = ptspair_get_fd(&pts);
    phase_error_pts = ptspair_get_path(&pts, PTSPAIR_FOO);
    ptspair_raw(&pts, PTSPAIR_FOO);
    ptspair_raw(&pts, PTSPAIR_BAR);
    /* will be read by the sim_oscillator in oscillatord */
    printf("%s", ptspair_get_path(&pts, PTSPAIR_BAR));
    /*
     * close stdout, we have nothing more to say on it plus it guarantees
     * that reading in it will end
     */
    fclose(stdout);

    ret = mkfifo(CONTROL_FIFO_PATH, 0600);
    if (ret == -1)
        error(EXIT_FAILURE, errno, "mkfifo(%s)", CONTROL_FIFO_PATH);
    atexit(cleanup);
    control_fifo_fd = open(CONTROL_FIFO_PATH, O_RDONLY);
    if (control_fifo_fd == -1)
        error(EXIT_FAILURE, errno, "open(%s)", CONTROL_FIFO_PATH);

    phase_error_fd = open(phase_error_pts, O_RDWR);
    if (phase_error_fd == -1)
        error(EXIT_FAILURE, errno, "open(%s)", phase_error_pts);

    /* must be last because used as the first argument of select() */
    tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd == -1)
        error(EXIT_FAILURE, errno, "timerfd_create");

    its.it_value.tv_sec = its.it_interval.tv_sec = period / NS_IN_SECOND;
    its.it_value.tv_nsec = its.it_interval.tv_nsec = period % NS_IN_SECOND;
    flags                                          = 0;
    ret = timerfd_settime(tfd, flags, &its, NULL);
    if (ret == -1)
        error(EXIT_FAILURE, errno, "timerfd_settime");

    log_info("%s[%jd] started, seed %jd.\n", prog_name, (intmax_t)getpid(), (intmax_t)seed);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* choose initial setpoint */
    setpoint = random_in_range(SETPOINT_MIN, SETPOINT_MAX + 1);
    setpoint = SETPOINT_MIN;
    log_info("initial setpoint %" PRIu32 "\n", setpoint);
    /* choose initial phase_error */
    phase_error = random_in_range(-INITIAL_ERROR_AMPLITUDE_NS, INITIAL_ERROR_AMPLITUDE_NS);
    log_info("initial phase_error: %" PRIi32 "\n", phase_error);

    memset(&its, 0, sizeof(its));
    while (loop) {
        memset(&tv, 0, sizeof(tv));
        tv.tv_sec = 2;
        FD_ZERO(&readfds);
        FD_SET(control_fifo_fd, &readfds);
        FD_SET(phase_error_fd, &readfds);
        FD_SET(pts_fd, &readfds);
        FD_SET(tfd, &readfds);
        ret = select(tfd + 1, &readfds, NULL, NULL, &tv);
        if (ret == -1) {
            if (errno == EINTR && !loop)
                break;
            error(EXIT_FAILURE, errno, "select");
        }
        if (ret == 0) /* timeout */
            continue;
        if (FD_ISSET(tfd, &readfds)) {
            sret = read(tfd, &expired, sizeof(expired));
            if (sret < 0 && ret != -EINTR)
                error(EXIT_FAILURE, errno, "read");
            phase_error += compute_delta(setpoint);
            log_debug("phase error: %" PRIi32 "\n", phase_error);

            if (phase_error_fd != -1) {
                sret = write(phase_error_fd, &phase_error, sizeof(phase_error));
                if (sret == -1)
                    error(EXIT_FAILURE, errno, "write");
            }
        }
        if (FD_ISSET(control_fifo_fd, &readfds)) {
            sret = read(control_fifo_fd, &setpoint, sizeof(setpoint));
            if (sret < 0)
                error(EXIT_FAILURE, errno, "read");
            if (sret == 0 && ret != -EINTR) {
                log_info("Peer closed the control fifo\n");
                break;
            }
            log_debug("new setpoint: %" PRIu32 "\n", setpoint);
        }
        if (FD_ISSET(phase_error_fd, &readfds)) {
            sret = read(phase_error_fd, &phase_offset, sizeof(phase_offset));
            if (sret < 0)
                error(EXIT_FAILURE, errno, "read");
            log_debug("applying phase offset: %" PRIi32 "\n", phase_offset);
            phase_error += phase_offset;
        }
        if (FD_ISSET(pts_fd, &readfds)) {
            ret = ptspair_process_events(&pts);
            if (ret < 0 && ret != -EINTR)
                error(EXIT_FAILURE, -ret, "ptspair_process_events");
        }

        usleep(100000);
    }

    close(tfd);

    log_info("%s exiting.\n", prog_name);

    return EXIT_SUCCESS;
}
