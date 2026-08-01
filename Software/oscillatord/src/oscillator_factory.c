#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>

#include "oscillator_factory.h"
#include "config.h"
#include "log.h"

#ifndef MAX_OSCILLATOR_FACTORIES
#define MAX_OSCILLATOR_FACTORIES 5
#endif

static const struct oscillator_factory *factories[MAX_OSCILLATOR_FACTORIES];
static unsigned int factories_nb;

static const struct oscillator_factory *oscillator_factory_get_by_name(
		const char *name)
{
	unsigned int i;

	for (i = 0; i < factories_nb; i++)
		if (strcmp(name, factories[i]->class.name) == 0)
			return factories[i];

	errno = ENOENT;

	return NULL;
}

struct oscillator *oscillator_factory_new(struct config *config, struct devices_path *devices_path)
{
	int ret;
	const char *name;
	const struct oscillator_factory *factory;

	name = config_get(config, "oscillator");
	if (name == NULL) {
		ret = errno;
		log_error("Configuration \"%s\" doesn't have an oscillator entry.",
				config->path);
		errno = ret;
		return NULL;
	}

	factory = oscillator_factory_get_by_name(name);
	if (factory == NULL) {
		ret = errno;
		log_error("Display type \"%s\" unknown, check configuration %s",
				name, config->path);
		errno = ret;
		return NULL;
	}

	return factory->new(devices_path);
}

static bool oscillator_factory_is_valid
	(const struct oscillator_factory *factory)
{
	return factory != NULL && factory->destroy != NULL &&
			factory->class.name != NULL &&
			*factory->class.name != '\0' &&
			factory->new != NULL;
}

void oscillator_factory_init(const char *factory_name,
		struct oscillator *oscillator, const char *fmt, ...)
{
	va_list args;
	const struct oscillator_factory *factory;

	factory = oscillator_factory_get_by_name(factory_name);
	oscillator->class = &factory->class;
	va_start(args, fmt);
	vsnprintf(oscillator->name, OSCILLATOR_NAME_LENGTH, fmt, args);
	va_end(args);
	oscillator->dac_max = 0;
	oscillator->dac_max = UINT32_MAX;
}

int oscillator_factory_register(const struct oscillator_factory *factory)
{
	if (!oscillator_factory_is_valid(factory))
		return -EINVAL;

	if (factories_nb == MAX_OSCILLATOR_FACTORIES) {
		log_error("no room left for factories, see "
				"MAX_OSCILLATOR_FACTORIES");
		return -ENOMEM;
	}
	factories[factories_nb] = factory;
	factories_nb++;

	return 0;
}

void oscillator_factory_destroy(struct oscillator **oscillator)
{
	const struct oscillator_factory *factory;

	if (oscillator == NULL || *oscillator == NULL)
		return;
	factory = oscillator_factory_get_by_name((*oscillator)->class->name);
	if (factory == NULL)
		return;

	factory->destroy(oscillator);
	*oscillator = NULL;
}
