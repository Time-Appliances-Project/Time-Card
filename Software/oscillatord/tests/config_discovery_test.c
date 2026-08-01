#include <assert.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"

static void make_path(char output[PATH_MAX], const char *directory,
	const char *name)
{
	int length = snprintf(output, PATH_MAX, "%s/%s", directory, name);
	assert(length > 0 && length < PATH_MAX);
}

static void make_file(const char *path)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fclose(file) == 0);
}

static void make_device_link(const char *directory, const char *name)
{
	char path[PATH_MAX];
	make_path(path, directory, name);
	assert(symlink("/dev/null", path) == 0);
}

int main(void)
{
	char temporary[] = "/tmp/oscillatord-config-test-XXXXXX";
	char config_path[PATH_MAX];
	char path[PATH_MAX];
	char config_text[PATH_MAX + 128];
	struct config config;
	struct devices_path devices;
	FILE *file;

	assert(mkdtemp(temporary) != NULL);
	make_device_link(temporary, "ptp");
	make_device_link(temporary, "pps");
	make_device_link(temporary, "ttyGNSS");
	make_device_link(temporary, "ttyMAC");
	make_device_link(temporary, "mro50");
	make_path(path, temporary, "disciplining_config");
	make_file(path);
	make_path(path, temporary, "temperature_table");
	make_file(path);
	make_path(config_path, temporary, "oscillatord.conf");
	snprintf(config_text, sizeof(config_text),
		"disciplining=true\nmonitoring=true\noscillator=mRO50\n"
		"sysfs-path=%s\ndebug=2\n", temporary);
	file = fopen(config_path, "wb");
	assert(file != NULL);
	assert(fwrite(config_text, 1, strlen(config_text), file) ==
		strlen(config_text));
	assert(fclose(file) == 0);

	assert(config_init(&config, config_path) == 0);
	assert(strcmp(config_get(&config, "oscillator"), "mRO50") == 0);
	assert(config_discover_devices(&config, &devices) == 0);
	assert(strcmp(devices.ptp_path, "/dev/null") == 0);
	assert(strcmp(devices.pps_path, "/dev/null") == 0);
	assert(strcmp(devices.gnss_path, "/dev/null") == 0);
	assert(strcmp(devices.mac_path, "/dev/null") == 0);
	assert(strcmp(devices.mro_path, "/dev/null") == 0);
	assert(strstr(devices.disciplining_config_path,
		"disciplining_config") != NULL);
	assert(strstr(devices.temperature_table_path,
		"temperature_table") != NULL);
	config_cleanup(&config);

	make_path(path, temporary, "ptp"); unlink(path);
	make_path(path, temporary, "pps"); unlink(path);
	make_path(path, temporary, "ttyGNSS"); unlink(path);
	make_path(path, temporary, "ttyMAC"); unlink(path);
	make_path(path, temporary, "mro50"); unlink(path);
	make_path(path, temporary, "disciplining_config"); unlink(path);
	make_path(path, temporary, "temperature_table"); unlink(path);
	unlink(config_path);
	assert(rmdir(temporary) == 0);
	return 0;
}

