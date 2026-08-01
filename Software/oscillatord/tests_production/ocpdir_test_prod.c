#include <dirent.h>
#include <getopt.h>
#include <linux/limits.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "log.h"
#include "utils.h"

int main(int argc, char *argv[])
{
	char ocp_path[256] = {0};
    bool found_eeprom = false;
    struct config config = {};
    struct devices_path devices_path = {};

	/* Set log level */
	log_set_level(1);

    snprintf(ocp_path, sizeof(ocp_path) - 1, "%s", argv[1]);

	log_info("\t-ocp path is: \"%s\", checking...",ocp_path);
	if (access(ocp_path, F_OK) == -1)
	{
        log_error("\t\tocp path doesn't exists !");
        return -1;
    }

    log_info("\t\tocp dir path exists !");

    config_set(&config, "sys_path", ocp_path);

    config_discover_devices(&config, &devices_path);

    if (strlen(devices_path.mro_path) > 0) {
        log_info("\t-mro50 device detected: %s", devices_path.mro_path);
    }
    if (strlen(devices_path.ptp_path) > 0)    {
        log_info("\t-ptp clock device detected: %s", devices_path.ptp_path);
    }
    if (strlen(devices_path.pps_path) > 0) {
        log_info("\t-pps device detected: %s", devices_path.pps_path);
    }
    if (strlen(devices_path.gnss_path) > 0) {
        log_info("\t-ttyGPS detected: %s", devices_path.gnss_path);
    }
    if (strlen(devices_path.mac_path) > 0) {
        log_info("\t-ttyMAC detected: %s", devices_path.mac_path);
    }
    if (strlen(devices_path.disciplining_config_path) > 0) {
        log_info("\t-disciplining_config detected: %s", devices_path.disciplining_config_path);
    }
    if (strlen(devices_path.temperature_table_path) > 0) {
        log_info("\t-temperature_table detected: %s", devices_path.temperature_table_path);
    }

    DIR * ocp_dir = opendir(ocp_path);

    if (ocp_dir == NULL)
    {
        log_error("Directory %s does not exists", ocp_path);
    }
    else
    {
        char eeprom_path[256];
        log_info("\t-sysfs path %s", ocp_path);

        struct dirent * entry = readdir(ocp_dir);
        while (entry != NULL)
        {
            if (strncmp(entry->d_name, "i2c", 4) == 0)
            {
                log_info("I2C device detected");
                char pathname[PATH_MAX];   /* should always be big enough */
                sprintf( pathname, "%s/%s", ocp_path, entry->d_name);
                found_eeprom = find_file(realpath(pathname, NULL), "eeprom", eeprom_path);
                if (found_eeprom)
                {
                    log_info("\t- Found EEPROM file: %s", eeprom_path);
                }
                else
                {
                    log_warn("\t- Could not find EEPROM file");
                }
            }

            entry = readdir(ocp_dir);
        }
    }
}
