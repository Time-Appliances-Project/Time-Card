/**
 * @file phasemeter.h
 * @brief Header for part computing the phase error between the PHC and the GNSS receiver
 * @version 0.1
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2022
 *
 * A thread is created to listen to PHC's external timestamps events (One corresponds to the PPS of the PHC,
 * another one corresponds to the PPS of the GNSS receiver). It then computes the phase error between these two PPS.
 */
#ifndef OSCILLATORD_PHASEMETER_H
#define OSCILLATORD_PHASEMETER_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @struct phasemeter
 * @brief general structure for phasemeter thread
 *
 */
struct phasemeter {
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int32_t phase_error;
	int status;
	int fd;
	bool stop;
};

struct phasemeter* phasemeter_init(int fd);
void phasemeter_stop(struct phasemeter *phasemeter);
int get_phase_error(struct phasemeter *phasemeter, int64_t *phase_error);

#endif /* OSCILLATORD_PHASEMETER_H */
