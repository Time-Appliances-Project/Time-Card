/**
 * @file config.h
 * @brief Header for function handling parsing of configuration file
 */
#ifndef CONFIG_H_
#define CONFIG_H_
#include <stddef.h>
#include <stdbool.h>
#include <linux/limits.h>

/**
 * @struct config
 * @brief structure holding config file values.
 */
struct config {
	char *argz;
	size_t len;
	char *argz_defconfig;
	size_t len_defconfig;
	const char *defconfig_key;
	char *path;
};

struct devices_path {
	char eeprom_path[PATH_MAX];
	char disciplining_config_path[PATH_MAX];
	char gnss_path[PATH_MAX];
	char mac_path[PATH_MAX];
	char mro_path[PATH_MAX];
	char pps_path[PATH_MAX];
	char ptp_path[PATH_MAX];
	char temperature_table_path[PATH_MAX];
};

extern volatile int loop;

int config_init(struct config *config, const char *path);
const char *config_get(const struct config *config, const char *key);
const char *config_get_default(const struct config *config, const char *key,
		const char *default_value);
bool config_get_bool_default(const struct config *config, const char *key,
		bool default_value);
int config_set(struct config *config, const char *key, const char *value);

/* returns a value in [0, LONG_MAX] on success, -errno on error */
long config_get_unsigned_number(const struct config *config, const char *key);
int config_get_int16_t(const struct config *config, const char *key, int16_t *val);

/* returns a value in [0, UINT8_MAX] on success, -errno on error */
int config_get_uint8_t(const struct config *config, const char *key);
void config_cleanup(struct config *config);

void config_dump(const struct config *config, char *buf, size_t buf_len);
int config_save(struct config *config, const char *path);

/* discovers devices from sysfs path */
int config_discover_devices(const struct config *config, struct devices_path *devices_path);
#endif /* CONFIG_H_ */
