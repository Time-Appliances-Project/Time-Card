#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>
#include <fcntl.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>

#include "sim_oscillator.h"

#include "config.h"
#include "log.h"
#include "utils.h"

#include "../oscillator.h"
#include "../oscillator_factory.h"

#define FACTORY_NAME "sim"
#define SIM_SETPOINT_MIN 0
#define SIM_SETPOINT_MAX 1000000
#define SIM_MAX_PTS_PATH_LEN 0x400

struct sim_oscillator {
	struct oscillator oscillator;
	FILE *simulator_process;
	int control_fifo;
	char pps_pts[SIM_MAX_PTS_PATH_LEN];
	uint32_t value;
};

static unsigned int sim_oscillator_index;

static int sim_oscillator_set_dac(struct oscillator *oscillator,
		uint32_t value)
{
	struct sim_oscillator *sim;
	ssize_t sret;

	sim = container_of(oscillator, struct sim_oscillator, oscillator);

	log_debug("%s(%s, %" PRIu32 ")", __func__, oscillator->name, value);

	sret = write(sim->control_fifo, &value, sizeof(value));
	if (sret == -1)
		return -errno;
	sim->value = value;

	return 0;
}

static int sim_oscillator_get_dac(struct oscillator *oscillator,
		uint32_t *value)
{
	struct sim_oscillator *sim;

	sim = container_of(oscillator, struct sim_oscillator, oscillator);

	*value = sim->value;
	log_debug("%s(%s) = %" PRIu32, __func__, oscillator->name, *value);

	return 0;
}

static int sim_oscillator_get_ctrl(struct oscillator *oscillator,
		struct oscillator_ctrl *ctrl)
{
	return sim_oscillator_get_dac(oscillator, &ctrl->dac);
}

static int sim_oscillator_save(struct oscillator *oscillator)
{
	log_debug("%s(%s)", __func__, oscillator->name);

	return -ENOSYS;
}

static int sim_oscillator_parse_attributes(struct oscillator *oscillator, struct oscillator_attributes *attributes)
{
	attributes->temperature = (rand() % (55 - 10)) + 10;
	attributes->locked = false;

	log_info("%s(%p, %g)", __func__, oscillator, attributes->temperature);

	return 0;
}

static int sim_oscillator_apply_output(struct oscillator *oscillator, struct od_output *output) {
		return sim_oscillator_set_dac(oscillator, output->setpoint);
}


static void sim_oscillator_destroy(struct oscillator **oscillator)
{
	struct oscillator *o;
	struct sim_oscillator *s;

	if (oscillator == NULL || *oscillator == NULL)
		return;

	o = *oscillator;
	s = container_of(o, struct sim_oscillator, oscillator);
	fd_cleanup(&s->control_fifo);
	if (s->simulator_process != NULL) {
		pclose(s->simulator_process);
		s->simulator_process = NULL;
	}
	memset(o, 0, sizeof(*o));
	free(o);
	*oscillator = NULL;
}

static struct oscillator *sim_oscillator_new(struct devices_path *devices_path)
{
	struct sim_oscillator *sim;
	int ret;
	struct oscillator *oscillator;
	const char *cret;

	__attribute__((cleanup(string_cleanup))) char *simulator_command = NULL;

	ret = asprintf(&simulator_command, "oscillator_sim");
	if (ret < 0) {
		log_error("asprintf error");
		errno = ENOMEM;
		return NULL;
	}

	sim = calloc(1, sizeof(*sim));
	if (sim == NULL)
		return NULL;
	oscillator = &sim->oscillator;
	sim->control_fifo = -1;

	log_info("launching the simulator process");
	unlink(CONTROL_FIFO_PATH);
	sim->simulator_process = popen(simulator_command, "re");
	if (sim->simulator_process == NULL) {
		ret = -errno;
		log_error("popen: %m");
		goto error;
	}
	log_info("opening fifo");
	do {
		sim->control_fifo = open(CONTROL_FIFO_PATH, O_WRONLY);
	} while (sim->control_fifo == -1 && errno == ENOENT);
	if (sim->control_fifo == -1) {
		ret = -errno;
		log_error("open(" CONTROL_FIFO_PATH "): %m");
		goto error;
	}
	oscillator_factory_init(FACTORY_NAME, oscillator, FACTORY_NAME "-%d",
			sim_oscillator_index);


	log_debug("reading pts name");
	cret = fgets(sim->pps_pts, SIM_MAX_PTS_PATH_LEN,
			sim->simulator_process);
	if (cret == NULL) {
		ret = -EIO;
		log_error("fgets error");
		goto error;
	}
	log_debug("pts name is %s", sim->pps_pts);

	log_info("instantiated " FACTORY_NAME " oscillator");

	return oscillator;
error:
	sim_oscillator_destroy(&oscillator);
	errno = -ret;
	return NULL;
}

static const struct oscillator_factory sim_oscillator_factory = {
	.class = {
			.name = FACTORY_NAME,
			.get_ctrl = sim_oscillator_get_ctrl,
			.save = sim_oscillator_save,
			.parse_attributes = sim_oscillator_parse_attributes,
			.apply_output = sim_oscillator_apply_output,
			.dac_min = SIM_SETPOINT_MIN,
			.dac_max = SIM_SETPOINT_MAX,
	},
	.new = sim_oscillator_new,
	.destroy = sim_oscillator_destroy,
};

static void __attribute__((constructor)) sim_oscillator_constructor(void)
{
	int ret;

	ret = oscillator_factory_register(&sim_oscillator_factory);
	if (ret < 0)
		log_error("oscillator_factory_register", ret);
}
