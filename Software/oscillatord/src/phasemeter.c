/**
 * @file phasemeter.c
 * @author your name (you@domain.com)
 * @brief Phasemeter computing phase difference between two PPS
 * @version 0.1
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2022
 * Both PPS's timestamps are received through a PTP clock event
 */
#include <errno.h>
#include <linux/ptp_clock.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/timex.h>
#include <inttypes.h> // PRI*

#include <oscillator-disciplining/oscillator-disciplining.h>

#include "log.h"
#include "phasemeter.h"

#define EXTTS_INDEX_ART_INTERNAL_PPS 5
#define EXTTS_INDEX_GNSS_PPS 0

#define MILLISECONDS_500 500000000

struct external_timestamp {
	int64_t timestamp; // ns
	int index;
};

/**
 * @brief Read an external timestamp
 *
 * @param fd handle of PHS
 * @param nsec pointer where timestamp will be stored
 * @return int index of external timestamp received on success, -1 on error
 */
static int read_extts(int fd, int64_t *nsec)
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
	*nsec = (int64_t) event.t.sec * 1000000000ULL + event.t.nsec;
	log_trace(
		"%s timestamp: %" PRIi64,
		event.index == 0? "GNSS     " : "Internal ",
		*nsec);

	return event.index;
}

/**
 * @brief Activate an external timestamp
 *
 * @param fd PHC handler
 * @param extts_index Index of extts to enable
 * @return int 0 on success, -1 on failure
 */
static int enable_extts(int fd, unsigned int extts_index)
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

/**
 * @brief Disable an external timestamp
 *
 * @param fd PHC handler
 * @param extts_index Index of extts to enable
 * @return int 0 on success, -1 on failure
 */
static int disable_extts(int fd, unsigned int extts_index)
{
	struct ptp_extts_request extts_request = {
		.index = extts_index,
		.flags = 0
	};

	if (ioctl(fd, PTP_EXTTS_REQUEST, &extts_request) < 0) {
		log_error("PTP_EXTTS_REQUEST disable");
		return -1;
	}

	return 0;
}

/**
 * @brief Phasemeter thread routine
 *
 * @param p_data
 * @return void*
 */
static void* phasemeter_thread(void *p_data)
{
	int ret;
	bool stop;
	struct phasemeter *phasemeter = (struct phasemeter *) p_data;
	struct external_timestamp ts1;
	struct external_timestamp ts2;

	stop = phasemeter->stop;

	ret = enable_extts(phasemeter->fd, EXTTS_INDEX_ART_INTERNAL_PPS);
	if (ret != 0) {
		log_error("Could not enable ART internal pps external events");
		return NULL;
	}
	ret = enable_extts(phasemeter->fd, EXTTS_INDEX_GNSS_PPS);
	if (ret != 0) {
		log_error("Could not enable GNSS pps external events");
		return NULL;
	}

	/* Get first timestamp */
	do {
		ts1.index = read_extts(phasemeter->fd, &ts1.timestamp);
		if (ts1.index < 0) {
			log_warn("Could not read ptp clock external timestamp for phasemeter");
		}
	} while (ts1.index != EXTTS_INDEX_ART_INTERNAL_PPS && ts1.index != EXTTS_INDEX_GNSS_PPS);

	while(!stop) {
		/* Get Second timestamp */
		do {
			ts2.index = read_extts(phasemeter->fd, &ts2.timestamp);
			if (ts2.index < 0) {
				log_warn("Could not read ptp clock external timestamp for phasemeter");
			}
		} while (ts2.index != EXTTS_INDEX_ART_INTERNAL_PPS && ts2.index != EXTTS_INDEX_GNSS_PPS);
		log_debug("Phasemeter: %s, ts %" PRIi64 , (ts1.index == EXTTS_INDEX_GNSS_PPS)? "GNSS" : "INT ", ts1.timestamp);
		log_debug("Phasemeter: %s, ts %" PRIi64 , (ts2.index == EXTTS_INDEX_GNSS_PPS)? "GNSS" : "INT ", ts2.timestamp);

		/*
		 * Did not received GNSS PPS external event
		 * GNSS receiver PPS output can be deactivated if GNSS is not locked
		 */
		if (ts1.index == EXTTS_INDEX_ART_INTERNAL_PPS && ts1.index == ts2.index) {
			log_warn("Phasemeter: Did not receive GNSS pps event");
			pthread_mutex_lock(&phasemeter->mutex);
			phasemeter->status = PHASEMETER_NO_GNSS_TIMESTAMPS;
			stop = phasemeter->stop;
			pthread_cond_signal(&phasemeter->cond);
			pthread_mutex_unlock(&phasemeter->mutex);
			/* Second timestamp become next first one */
			memcpy(&ts1, &ts2, sizeof(struct external_timestamp));

		/*
		 * Did not received ART Internal PPS event
		 * This case should not happen
		 */
		} else if (ts1.index == EXTTS_INDEX_GNSS_PPS && ts1.index == ts2.index) {
			log_warn("Phasemeter: Did not receive ART internal pps event");
			pthread_mutex_lock(&phasemeter->mutex);
			phasemeter->status = PHASEMETER_NO_ART_INTERNAL_TIMESTAMPS;
			stop = phasemeter->stop;
			pthread_cond_signal(&phasemeter->cond);
			pthread_mutex_unlock(&phasemeter->mutex);
			/* Second timestamp become next first one */
			memcpy(&ts1, &ts2, sizeof(struct external_timestamp));

		/*
		 * One timestamp comes from GNSS receiver and the one comes from ART Internal PPS
		 */
		} else {
			int64_t timestamp_diff = ts2.timestamp - ts1.timestamp;
			timestamp_diff = (ts1.index == EXTTS_INDEX_GNSS_PPS) ? -timestamp_diff : timestamp_diff;
			/*
			 * Phase error is superior to 500ms
			 * Wait next timestamp
			 */
			if (timestamp_diff > MILLISECONDS_500 || timestamp_diff < -MILLISECONDS_500) {
				/* Second timestamp become next first one */
				memcpy(&ts1, &ts2, sizeof(struct external_timestamp));
				continue;
			}
			log_debug("Phasemeter: phase_error: %" PRIi64 "ns", timestamp_diff);
			pthread_mutex_lock(&phasemeter->mutex);
			phasemeter->status = PHASEMETER_BOTH_TIMESTAMPS;
			phasemeter->phase_error = timestamp_diff;
			stop = phasemeter->stop;
			pthread_cond_signal(&phasemeter->cond);
			pthread_mutex_unlock(&phasemeter->mutex);
			/* Get first timestamp */
			do {
				ts1.index = read_extts(phasemeter->fd, &ts1.timestamp);
				if (ts1.index < 0) {
					log_warn("Could not read ptp clock external timestamp for phasemeter");
				}
			} while (ts1.index != EXTTS_INDEX_ART_INTERNAL_PPS && ts1.index != EXTTS_INDEX_GNSS_PPS);
		}
	}

	log_info("Closing phasemeter thread");
	ret = disable_extts(phasemeter->fd, EXTTS_INDEX_ART_INTERNAL_PPS);
	if (ret != 0) {
		log_error("Could not disable ART internal pps external events");
	}
	ret = disable_extts(phasemeter->fd, EXTTS_INDEX_GNSS_PPS);
	if (ret != 0) {
		log_error("Could not disable GNSS pps external events");
	}
	return NULL;
}

/**
 * @brief Create phasemeter structure from PHC handler
 *
 * @param fd PHC handler
 * @return struct phasemeter*
 */
struct phasemeter* phasemeter_init(int fd)
{
	int ret;

	struct phasemeter *phasemeter = malloc(sizeof(struct phasemeter));
	if (phasemeter == NULL) {
		log_error("Could not allocate memory for phasemeter thread");
		return NULL;
	}
	phasemeter->fd = fd;
	phasemeter->stop = false;
	phasemeter->status = PHASEMETER_INIT;

	if (pthread_mutex_init(&phasemeter->mutex, NULL) != 0) {
		printf("\n mutex init failed\n");
		free(phasemeter);
		return NULL;
	}
	if (pthread_cond_init(&phasemeter->cond, NULL)) {
		printf("\n Cond var init failed\n");
		free(phasemeter);
		return NULL;
	}

	ret = pthread_create(
		&phasemeter->thread,
		NULL,
		phasemeter_thread,
		phasemeter
	);
	if (ret != 0) {
		log_error("Could not create phasemeter thread");
		free(phasemeter);
		return NULL;
	}

	return phasemeter;
}

/**
 * @brief Stop phasemeter thread
 *
 * @param phasemeter
 */
void phasemeter_stop(struct phasemeter *phasemeter)
{
	if (phasemeter == NULL)
		return;
	pthread_mutex_lock(&phasemeter->mutex);
	phasemeter->stop = true;
	pthread_mutex_unlock(&phasemeter->mutex);
	pthread_join(phasemeter->thread, NULL);
	free(phasemeter);
	phasemeter = NULL;
	return;
}

/**
 * @brief Get phase error from the thread
 *
 * @param phasemeter thread structure data
 * @param phase_error pointer where phase error will be stored
 * @return int phasemeter status
 */
int get_phase_error(struct phasemeter *phasemeter, int64_t *phase_error)
{
	int status;
	pthread_mutex_lock(&phasemeter->mutex);
	pthread_cond_wait(&phasemeter->cond, &phasemeter->mutex);
	*phase_error = phasemeter->phase_error;
	status = phasemeter->status;
	pthread_mutex_unlock(&phasemeter->mutex);
	
	return status;
}
