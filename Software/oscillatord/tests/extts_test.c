#include <fcntl.h>

#include "extts.h"
#include "log.h"
#include <signal.h>

static volatile int keepRunning = 1;

static void         intHandler(int dummy) {
            keepRunning = 0;
}

int main(int argc, char* argv[]) {
    int64_t timestamp;
    int     fd_clock;
    int     ret;

    if (argc < 2) {
        log_error("Please specify path to ptp device !");
        return -1;
    }

    signal(SIGINT, intHandler);
    log_set_level(LOG_INFO);
    fd_clock = open(argv[1], O_RDWR);

    for (int i = 0; i < NUM_EXTTS; i++) {
        ret = enable_extts(fd_clock, i);
        if (ret != 0) {
            log_error("Could not enable external events for index %d", i);
            return -1;
        }
    }

    while (keepRunning) {
        ret = read_extts(fd_clock, &timestamp);
        if (ret != 0) {
            log_warn("Could not read ptp clock external timestamp");
            continue;
        }
    }

    log_debug("Closing extts test program");
    for (int i = 0; i < NUM_EXTTS; i++) {
        ret = disable_extts(fd_clock, i);
        if (ret != 0)
            log_error("Could not disable external events for index %d", i);
    }
    return 0;
}
