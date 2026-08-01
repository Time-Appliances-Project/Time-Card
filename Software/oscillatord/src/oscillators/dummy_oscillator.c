#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include "config.h"
#include "log.h"

#include "../oscillator.h"
#include "../oscillator_factory.h"

#define FACTORY_NAME "dummy"

#define DUMMY_SETPOINT_MIN 31500
#define DUMMY_SETPOINT_MAX 1016052

static unsigned int dummy_oscillator_index;

static int dummy_oscillator_set_dac(struct oscillator *oscillator,
		uint32_t value)
{
	log_info("%s(%p, %" PRIu32 ")", __func__, oscillator, value);

	return 0;
}

static int dummy_oscillator_get_dac(struct oscillator *oscillator,
		uint32_t *value)
{
	*value = (rand() % (DUMMY_SETPOINT_MAX - DUMMY_SETPOINT_MIN)) +
			DUMMY_SETPOINT_MIN;

	log_info("%s(%p, %" PRIu32 ")", __func__, oscillator, *value);

	return 0;
}

static int dummy_oscillator_get_ctrl(struct oscillator *oscillator,
		struct oscillator_ctrl *ctrl)
{
	return dummy_oscillator_get_dac(oscillator, &ctrl->dac);
}

static int dummy_oscillator_save(struct oscillator *oscillator)
{
	log_info("%s(%p)", __func__, oscillator);

	return 0;
}

static int dummy_oscillator_parse_attributes(struct oscillator *oscillator, struct oscillator_attributes *attributes)
{
	attributes->temperature = (rand() % (55 - 10)) + 10;
	attributes->locked = false;

	log_info("%s(%p, %g)", __func__, oscillator, attributes->temperature);

	return 0;
}

static int dummy_oscillator_apply_output(struct oscillator *oscillator, struct od_output *output) {
	return dummy_oscillator_set_dac(oscillator, output->setpoint);
}

static struct oscillator *dummy_oscillator_new(struct devices_path *devices_path)
{
	struct oscillator *oscillator;

	oscillator = calloc(1, sizeof(*oscillator));
	if (oscillator == NULL)
		return NULL;

	oscillator_factory_init(FACTORY_NAME, oscillator, FACTORY_NAME "-%d",
			dummy_oscillator_index);
	dummy_oscillator_index++;

	return oscillator;
}

static void dummy_oscillator_destroy(struct oscillator **oscillator)
{
	memset(*oscillator, 0, sizeof(**oscillator));
	free(*oscillator);
	*oscillator = NULL;
}

static const struct oscillator_factory dummy_oscillator_factory = {
	.class = {
			.name = FACTORY_NAME,
			.get_ctrl = dummy_oscillator_get_ctrl,
			.save = dummy_oscillator_save,
			.parse_attributes = dummy_oscillator_parse_attributes,
			.apply_output = dummy_oscillator_apply_output,
			.dac_min = DUMMY_SETPOINT_MIN,
			.dac_max = DUMMY_SETPOINT_MAX,
	},
	.new = dummy_oscillator_new,
	.destroy = dummy_oscillator_destroy,
};

static void __attribute__((constructor)) dummy_oscillator_constructor(void)
{
	int ret;

	ret = oscillator_factory_register(&dummy_oscillator_factory);
	if (ret < 0)
		log_error("oscillator_factory_register", ret);
}
