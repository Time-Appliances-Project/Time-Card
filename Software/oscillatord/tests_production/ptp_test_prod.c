#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>

#include "log.h"
#include "utils.h"

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)	((clockid_t) ((((unsigned int) ~fd) << 3) | CLOCKFD))

/* Compute diff between two timespec */
static void timespec_diff(struct timespec *ts1, struct timespec *ts2,
                   int64_t *diff_ns)
{
    uint64_t ts1_ns = ts1->tv_sec * NS_IN_SECOND + ts1->tv_nsec;
    uint64_t ts2_ns = ts2->tv_sec * NS_IN_SECOND + ts2->tv_nsec;
    *diff_ns = ts1_ns - ts2_ns;
    return;
}

/*
 * When setting time, a small delay is added because of the time
 * it takes to get time and set it on the PTP Clock
 */
#define SET_TIME_PHASE_ERROR 200000
/* Test adjust time */
static bool test_ptp_adjtime(clockid_t clkid, int64_t adjust_ns)
{
    int ret;
    struct timespec ts_real;
    struct timespec ts_before_adjustment;
    struct timespec ts_after_adjustment;
    int64_t diff_ns_before_adjustment;
    int64_t diff_ns;

    log_info("\t- Making an adjustment of %lldns to ptp clock", adjust_ns);
    // Get realtime
    ret = clock_gettime(CLOCK_REALTIME, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server");
        return false;
    }
    // Set PTP clock time to realtime
    ret = clock_settime(clkid, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not set time of ptp clock");
        return false;
    }

    // Compute time diff between realtime clock and ptp_clock time before adjustment
    ret = clock_gettime(CLOCK_REALTIME, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server");
        return false;
    }
    ret = clock_gettime(clkid, &ts_before_adjustment);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server");
        return false;
    }
    timespec_diff(&ts_before_adjustment, &ts_real, &diff_ns_before_adjustment);
    log_info("\t- Diff before adjustment is %lldns", diff_ns_before_adjustment);

    struct timex timex = {
        .modes = ADJ_SETOFFSET | ADJ_NANO,
        .offset = 0,
        .time.tv_sec = adjust_ns > 0 || (adjust_ns % NS_IN_SECOND == 0.0) ?
            (long long) floor(adjust_ns / NS_IN_SECOND):
            (long long) floor(adjust_ns / NS_IN_SECOND) - 1,
        .time.tv_usec = adjust_ns > 0 || (adjust_ns % NS_IN_SECOND == 0.0) ?
            adjust_ns % NS_IN_SECOND:
            adjust_ns % NS_IN_SECOND + NS_IN_SECOND,
    };

    // Adjust ptp clock time
    log_info("\t- Applying phase offset correction of %"PRIi32"ns", adjust_ns);
    ret = clock_adjtime(clkid, &timex);
    // Wait for change to take effect
    sleep(3);

    ret = clock_gettime(CLOCK_REALTIME, &ts_real);
    if (ret != 0) {
        log_error("\t- Could not read time of ptp clock");
        return false;
    }
    ret = clock_gettime(clkid, &ts_after_adjustment);
    if (ret != 0) {
        log_error("\t- Could not read clock realtime on server");
        return false;
    }

    timespec_diff(&ts_after_adjustment, &ts_real, &diff_ns);
    log_info(
        "\t- Diff time between clock real time and adjusted ptp clock time is: %lldns", diff_ns - diff_ns_before_adjustment);
    if (diff_ns - diff_ns_before_adjustment > adjust_ns + SET_TIME_PHASE_ERROR
        || diff_ns - diff_ns_before_adjustment < adjust_ns - SET_TIME_PHASE_ERROR) {
        log_error("\t- Error making an adjustment of %lld  to ptp clock time");
        return false;
    }
    log_info("\t- Adjustment of %lldns passed", adjust_ns);
    return true;
}


int main(int argc, char *argv[])
{
	char ocp_path[256] = {0};
	char ptp_path[256] = {0};
    bool ptp_path_valid;
    bool ocp_path_valid;
    bool ptp_test_passed = true;

	/* Set log level */
	log_set_level(1);

	log_info("Checking input:");
    snprintf(ocp_path, sizeof(ocp_path) - 1, "/sys/class/timecard/%s", argv[1]);

	log_info("\t-ocp path is: \"%s\", checking...", ocp_path);
	if (access(ocp_path, F_OK) != -1)
	{
		ocp_path_valid = true;
        log_info("\t\tocp path exists !");
    }
	else
	{
		ocp_path_valid = false;
        log_info("\t\tocp path doesn't exists !");
    }

    if (ocp_path_valid)
    {
        log_info("\t-sysfs path %s", ocp_path);

        DIR* ocp_dir = opendir(ocp_path);
        struct dirent * entry = readdir(ocp_dir);
        while (entry != NULL)
        {
            if (strcmp(entry->d_name, "ptp") == 0)
            {
                find_dev_path(ocp_path, entry, ptp_path);
                log_info("\t-ptp clock device detected: %s", ptp_path);
                ptp_path_valid = true;
            }
            entry = readdir(ocp_dir);
        }
    }

    if (ptp_path_valid)
    {
        int ptp_clock = open(ptp_path, O_RDWR);
        if (ptp_clock > 0)
        {
            int ret;
            clockid_t clkid;
            struct timespec ts_real;
            struct timespec ts_set;
            int64_t diff_ns;
            int64_t adjust_time_ns_test[2] = {
                -1 * NS_IN_SECOND,
                -500000000,
            };

            ret = clock_gettime(CLOCK_REALTIME, &ts_real);
            if (ret != 0)
            {
                log_warn("\t- Could not read clock realtime on server");
                ptp_test_passed = false;
            }

            clkid = FD_TO_CLOCKID(ptp_clock);
            ret = clock_settime(clkid, &ts_real);
            if (ret != 0)
            {
                log_warn("\t- Could not set time of ptp clock");
                ptp_test_passed = false;
            }
            ret = clock_gettime(clkid, &ts_set);
            if (ret != 0)
            {
                log_warn("\t- Could not read time of ptp clock");
                ptp_test_passed = false;
            }

            // Test diff between gettime and settime is inferior to 250µs
            timespec_diff(&ts_real, &ts_set, &diff_ns);
            if (diff_ns > SET_TIME_PHASE_ERROR || diff_ns < -SET_TIME_PHASE_ERROR)
            {
                log_warn("\t- Timespec between get and settime is to big");
                ptp_test_passed = false;
            }
            if (ptp_test_passed)
            {
                log_info("\t- PTP Clock time correctly set");
            }
            for (int i = 0; i < 2; i++)
            {
                if (!test_ptp_adjtime(clkid, adjust_time_ns_test[i]))
                {
                    log_warn("Error adjusting ptp clock time");
                    ptp_test_passed = false;
                }
            }
            if (ptp_test_passed)
            {
                log_info("PTP test succeeded");
            }
        }
        else
        {
            log_warn("PTP Test Aborted: failed to open clock device");
        }
    }
    else
    {
        log_warn("PTP Test Aborted");
    }
}
