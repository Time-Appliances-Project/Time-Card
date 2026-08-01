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
    char ocp_path[256] = "";
    bool ocp_path_valid = false;
    bool coarse_value_valid = false;
    bool serial_value_valid = false;
    bool found_eeprom = false;
    bool disciplining_config_found = false;
    bool temperature_table_found = false;
    int mro50_coarse_value;
    bool passed = true;
    char serial[256] = "";
    char eeprom_path[256] = "";
    char disciplining_config_path[256] = "";
    char temperature_table_path[256] = "";
	/* Set log level */
	log_set_level(1);

    snprintf(ocp_path, sizeof(ocp_path), "/sys/class/timecard/%s", argv[1]);
	log_info("\t-ocp path is: \"%s\", checking...",ocp_path);
	if (access(ocp_path, F_OK) != -1) 
	{
		ocp_path_valid = true;
        log_info("\t\tocp dir path exists !");
        
        DIR * ocp_dir;
        log_info("\t-sysfs path %s", ocp_path);

        ocp_dir = opendir(ocp_path);
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
            else if (strcmp(entry->d_name, "disciplining_config") == 0)
            {
                find_file((char *) ocp_path, "disciplining_config", disciplining_config_path);
                log_info("\t-disciplining_config detected: %s", disciplining_config_path);
                disciplining_config_found = true;
            }
            else if (strcmp(entry->d_name, "temperature_table") == 0)
            {
                find_file((char *) ocp_path, "temperature_table", temperature_table_path);
                log_info("\t-temperature_table detected: %s", temperature_table_path);
                temperature_table_found = true;
            }
            entry = readdir(ocp_dir);
        }
    } 
	else 
	{
		ocp_path_valid = false;
        log_info("\t\tocp path doesn't exists !");
    }
    if (argc > 2) 
    { 
        mro50_coarse_value = atoi(argv[2]); // get the string to check
        coarse_value_valid = true;
    }
    if (!coarse_value_valid)
    {
       log_info("no coarse value provided");
    }
    if (argc > 3) 
    {   
        if (strlen(argv[3]) == 9 && *argv[3] == 'F')
        {
            strncpy(serial, argv[3], sizeof(serial));
            serial_value_valid = true;
        }
    }
    if (!serial_value_valid)
    {
       log_info("no serial value provided");
    }
    if (ocp_path_valid && coarse_value_valid && serial_value_valid)
    {
        if (found_eeprom)
        {
            log_info("Writing EEPROM manufacturing data");
            char command[4141];
            sprintf(command, "art_eeprom_format -p %s -s %s", eeprom_path, serial);
            if (system(command) != 0)
            {
                log_warn("Could not write EEPROM data in %s", eeprom_path);
                passed = false;
            }
            else
            {
                memset(command, 0, 4141);
                if (disciplining_config_found) 
                {
                    log_info("Writing Disciplining config in EEPROM");
                    sprintf(command, "art_disciplining_manager -p %s -f -c %u", disciplining_config_path, mro50_coarse_value);
                    if (system(command) != 0)
                    {
                        log_warn("Could not write factory disciplining parameters in %s", disciplining_config_path);
                        passed = false;
                    }
                    memset(command, 0, 4141);
                }
                else 
                {
                    log_warn("Disciplining config file not found!");
                    passed = false;
                }

                if (temperature_table_found) 
                {
                    log_info("Writing temperature table in EEPROM");
                    sprintf(command, "art_temperature_table_manager -p %s -f", temperature_table_path);
                    if (system(command) != 0) 
                    {
                        log_warn("Could not write factory temperature table in %s",temperature_table_path);
                        passed = false;
                    }
                    memset(command, 0, 4141);
                }
                else 
                {
                    log_warn("Temperature table file not found!");
                    passed = false;
                }
            }
        }
    }
    else
    {
        log_warn("EEPROM writting Aborted");
        passed = false;
    }
    if (passed)
    {
        log_info("EEPROM writting succeeded");
    }
    else
    {
        log_info("EEPROM writting failed");
    }
}