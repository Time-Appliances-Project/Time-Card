#include <errno.h>
#include <fcntl.h>
#include <linux/ptp_clock.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "extts.h"
#include "log.h"

int enable_extts(int fd, unsigned int extts_index)
{
	struct ptp_extts_request extts_request = {
		.index = extts_index,
		.flags = PTP_RISING_EDGE | PTP_ENABLE_FEATURE
	};

	if (ioctl(fd, PTP_EXTTS_REQUEST, &extts_request) < 0) {
		log_error("PTP_EXTTS_REQUEST enable");
		return -1;
	}

	return 0;
}

int disable_extts(int fd, unsigned int extts_index)
{
	struct ptp_extts_request extts_request = {
		.index = extts_index,
		.flags = 0
	};

	if (ioctl(fd, PTP_EXTTS_REQUEST, &extts_request) < 0) {
		log_error("PTP_EXTTS_REQUEST disable");
		return 1;
	}

	return 0;
}

int read_extts(int fd, int64_t *nsec)
{	
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

	/* Timestamp is passed as two unsigned 32 bits integers,
	 * We hack the data structure to get a signed 32 bits
	 */
	log_debug("sec %lu, nsec %lu", event.t.sec, event.t.nsec);
	*nsec = event.t.sec * 1000000000ULL + event.t.nsec;

	log_info(
		"%s timestamp: %llu",
		event.index == EXTTS_INDEX_TS_GNSS? "GNSS" :
		event.index == EXTTS_INDEX_TS_1 ? "TS1" :
		event.index == EXTTS_INDEX_TS_2 ? "TS2" :
		event.index == EXTTS_INDEX_TS_3 ? "TS3" :
		event.index == EXTTS_INDEX_TS_4 ? "TS4" :
		event.index == EXTTS_INDEX_TS_INTERNAL ? "Internal PPS": "Unknown",
		*nsec);

	return 0;
}
