#include <errno.h>
#include <fcntl.h>
#include <linux/ptp_clock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "utils.h"
#include "utils/extts.h"
#include "log.h"

#define PHC_OUT "OUT: MAC"
#define GNSS_OUT "OUT: GNSS"
#define PPS_IN "IN: PPS1"

static int configure_io(char * ocp_path, int io, char *mode)
{
    FILE *fptr;
    char io_path[128];

    if (io < 1 || io > 4) {
        log_error("Only IO 1-4 exists ! wanted %d", io);
        return -1;
    }

    if (strcmp(mode, PHC_OUT) && strcmp(mode, GNSS_OUT) && strcmp(mode, PPS_IN)) {
        log_error("Unknown mode: %s", mode);
        return -1;
    }

    sprintf(io_path, "%s/sma%d", ocp_path, io);
    fptr = fopen(io_path, "w");
    if (fptr == NULL) {
        log_error("Could not open %s", io_path);
        return -1;
    }

    fprintf(fptr, "%s", mode);
    fclose(fptr);
    return 0;
}

static int configure_ios(char *ocp_path, char *mode_1, char *mode_2, char *mode_3, char *mode_4)
{
    int ret;
    ret = configure_io(ocp_path, 1, mode_1);
    if (ret != 0) {
        log_error("Error configuring IO %d", 1);
        return ret;
    }
    ret = configure_io(ocp_path, 2, mode_2);
    if (ret != 0) {
        log_error("Error configuring IO %d", 2);
        return ret;
    }
    ret = configure_io(ocp_path, 3, mode_3);
    if (ret != 0) {
        log_error("Error configuring IO %d", 3);
        return ret;
    }
    ret = configure_io(ocp_path, 4, mode_4);
    if (ret != 0) {
        log_error("Error configuring IO %d", 4);
        return ret;
    }
    return ret;
}

/**
 * @brief Read 100 timestamps max and expect to see the one set in expected_timestamps
 *
 * @param fd file descriptor of the PHC
 * @param expected_timestamps flag of expected timestamps
 * @return int 0 on success else -1
 */
static int test_extts(int fd, uint8_t expected_timestamps)
{
    uint8_t read_timestamps = 0;
    for (int i = 0; i < 100; i++) {
        struct ptp_extts_event event = {0};

        if (read(fd, &event, sizeof(event)) != sizeof(event)) {
            log_error("failed to read extts event");
            return -1;
        }

        if (event.t.sec < 0) {
            errno = -EINVAL;
            log_error("EXTTS second field is supposed to be positive");
            return -EINVAL;
        }

        switch(event.index) {
        case EXTTS_INDEX_TS_1:
        case EXTTS_INDEX_TS_2:
        case EXTTS_INDEX_TS_3:
        case EXTTS_INDEX_TS_4:
            if (expected_timestamps & (1 << event.index)) {
                /* Timestamps is expected, saving it */
                read_timestamps += (1 << event.index);
            } else {
                log_error("Unexpected timestamps %d", event.index);
                return -1;
            }
            break;
        case EXTTS_INDEX_TS_GNSS:
        case EXTTS_INDEX_TS_INTERNAL:
            break;

        }
        /* Check if we read every timestamp we expected */
        if (read_timestamps == expected_timestamps)
            break;
    }
    return read_timestamps == expected_timestamps ? 0 : -1;
}

static int enable_all_extts(int fd_clock)
{
    int ret;
    for (int i = 0; i < NUM_EXTTS; i++) {
        ret = enable_extts(fd_clock, i);
        if (ret != 0) {
            log_error("Could not enable external events for index %d", i);
            break;
        }
    }
    return ret;
}

static int disable_all_extts(int fd_clock)
{
    int ret;
    for (int i = 0; i < NUM_EXTTS; i++) {
        ret = disable_extts(fd_clock, i);
        if (ret != 0)
            log_error("Could not disable external events for index %d", i);
    }
    return ret;
}

static bool test_configurable_io(char * ocp_path, char *ptp_path)
{
    int passed = false;
    int fd_clock;
    int ret;

    log_info("Starting Configurable IO test");
    fd_clock = open(ptp_path, O_RDWR);
    enable_all_extts(fd_clock);

    ret = configure_ios(ocp_path, PPS_IN, PHC_OUT, PPS_IN, PHC_OUT);
    if (ret != 0) {
        log_error("Error configuring IOs");
        goto out;
    }

    if (test_extts(fd_clock, (1 << EXTTS_INDEX_TS_1 | 1 << EXTTS_INDEX_TS_3)) != 0) {
        log_error("Did not read EXTTS on 1 & 3");
        passed = false;
        goto out;
    }
    log_info("Passed test on SMA 1 and 3");


    ret = configure_ios(ocp_path, PHC_OUT, PPS_IN, PHC_OUT, PPS_IN);
    if (ret != 0) {
        log_error("Error configuring IOs");
        goto out;
    }

    if (test_extts(fd_clock, 1 << EXTTS_INDEX_TS_2 | 1 << EXTTS_INDEX_TS_4) != 0) {
        log_error("Did not read EXTTS on 2 & 4");
        passed = false;
        goto out;
    }
    log_info("Passed test on SMA 2 and 4");
    passed = true;

out:
    configure_ios(ocp_path, PPS_IN, PPS_IN, PPS_IN, PPS_IN);
    disable_all_extts(fd_clock);
    close(fd_clock);
    return passed;
}

int main(int argc, char *argv[])
{
    char ocp_path[256] = "";
    char ptp_path[256] = "";
    bool ocp_path_valid;
    bool ptp_path_valid = false;

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

    if (ocp_path_valid && ptp_path_valid)
    {
        if (test_configurable_io(ocp_path, ptp_path))
        {
            log_info("IO Test Passed");
        }
        else
        {
            log_info("IO Test Failed");
        }
    }
    else
    {
        log_warn("IO Test Aborted");
    }
}
