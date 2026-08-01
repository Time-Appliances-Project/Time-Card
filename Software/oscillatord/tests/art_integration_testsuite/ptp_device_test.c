#include <math.h>
#include <time.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/timex.h>

#include "ptp_device_test.h"

#include "log.h"
#include "utils.h"

#define CLOCKFD           3
#define FD_TO_CLOCKID(fd) ((clockid_t)((((unsigned int)~fd) << 3) | CLOCKFD))

/* Compute diff between two timespec */
static void timespec_diff(struct timespec* ts1, struct timespec* ts2, int64_t* diff_ns) {
    uint64_t ts1_ns = ts1->tv_sec * NS_IN_SECOND + ts1->tv_nsec;
    uint64_t ts2_ns = ts2->tv_sec * NS_IN_SECOND + ts2->tv_nsec;
    *diff_ns        = ts1_ns - ts2_ns;
    return;
}

/*
 * When setting time, a small delay is added because of the time
 * it takes to get time and set it on the PTP Clock
 */
#define SET_TIME_PHASE_ERROR 200000
/* Test adjust time */
static bool test_ptp_adjtime(clockid_t clkid, int64_t adjust_ns) {
    int             ret;
    struct timespec ts_real;
    struct timespec ts_before_adjustment;
    struct timespec ts_after_adjustment;
    int64_t         diff_ns_before_adjustment;
    int64_t         diff_ns;

    log_info("\t- Making an adjustment of %lldns to ptp clock", adjust_ns);
    // Get realtime
    ret = clock_gettime(CLOCK_REALTIME, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server\n");
        return false;
    }
    // Set PTP clock time to realtime
    ret = clock_settime(clkid, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not set time of ptp clock\n");
        return false;
    }

    // Compute time diff between realtime clock and ptp_clock time before adjustment
    ret = clock_gettime(CLOCK_REALTIME, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server\n");
        return false;
    }
    ret = clock_gettime(clkid, &ts_before_adjustment);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server\n");
        return false;
    }
    timespec_diff(&ts_before_adjustment, &ts_real, &diff_ns_before_adjustment);
    log_trace("\t- Diff before adjustment is %lld", diff_ns_before_adjustment);

    struct timex timex = {
        .modes        = ADJ_SETOFFSET | ADJ_NANO,
        .offset       = 0,
        .time.tv_sec  = adjust_ns > 0 || (adjust_ns % NS_IN_SECOND == 0.0) ?
                            (long long)floor(adjust_ns / NS_IN_SECOND) :
                            (long long)floor(adjust_ns / NS_IN_SECOND) - 1,
        .time.tv_usec = adjust_ns > 0 || (adjust_ns % NS_IN_SECOND == 0.0) ?
                            adjust_ns % NS_IN_SECOND :
                            adjust_ns % NS_IN_SECOND + NS_IN_SECOND,
    };

    // Adjust ptp clock time
    log_info("\t- Applying phase offset correction of %" PRIi32 "ns", adjust_ns);
    ret = clock_adjtime(clkid, &timex);
    // Wait for change to take effect
    sleep(3);

    ret = clock_gettime(CLOCK_REALTIME, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not read time of ptp clock\n");
        return false;
    }
    ret = clock_gettime(clkid, &ts_after_adjustment);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server\n");
        return false;
    }

    timespec_diff(&ts_after_adjustment, &ts_real, &diff_ns);
    log_info("\t- Diff time between clock real time and adjusted ptp clock time is: "
             "%lldns",
             diff_ns - diff_ns_before_adjustment);
    if (diff_ns - diff_ns_before_adjustment > adjust_ns + SET_TIME_PHASE_ERROR
        || diff_ns - diff_ns_before_adjustment < adjust_ns - SET_TIME_PHASE_ERROR) {
        log_error("\t- Error making an adjustment of %lld  to ptp clock time");
        return false;
    }
    log_info("\t- Adjustment of %lldns passed\n", adjust_ns);
    return true;
}

/*
 * Get current time from server and set ptp clock to this time
 * Check time is correctly written
 * Also tests adjust time func
 */
bool test_ptp_device(int ptp) {
    int             ret;
    clockid_t       clkid;
    struct timespec ts_real;
    struct timespec ts_set;
    int64_t         diff_ns;
    int64_t         adjust_time_ns_test[2] = {
                -1 * NS_IN_SECOND,
                -500000000,
    };

    ret = clock_gettime(CLOCK_REALTIME, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server\n");
        return false;
    }

    clkid = FD_TO_CLOCKID(ptp);
    ret   = clock_settime(clkid, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not set time of ptp clock\n");
        return false;
    }
    ret = clock_gettime(clkid, &ts_set);
    if (ret != 0) {
        log_error("\t- Could not read time of ptp clock\n");
        return false;
    }

    // Test diff between gettime and settime is inferior to 250µs
    timespec_diff(&ts_real, &ts_set, &diff_ns);
    if (diff_ns > SET_TIME_PHASE_ERROR || diff_ns < -SET_TIME_PHASE_ERROR) {
        log_error("\t- Timespec between get and settime is to big");
        return false;
    }
    log_info("\t- PTP Clock time correctly set\n");

    for (int i = 0; i < 2; i++)
        if (!test_ptp_adjtime(clkid, adjust_time_ns_test[i]))
            log_warn("Error adjusting ptp clock time");

    return true;
}
