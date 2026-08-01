/**
 * @file art_eeprom_format.c
 * @author Charles PARENT (charles.parent@orolia2s.com)
 * @brief Program to write factory data on EEPROM of ART cards
 * @version 0.1
 * @date 2022-03-04
 *
 * @copyright Copyright (c) 2022
 *
 */
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <time.h>
#include <unistd.h>

#include "eeprom.h"
#include "log.h"

#include <dirent.h>

static bool detect_eeprom_path(const char* ocp_path, char* eeprom_path){
    char dirpath[256];
    sprintf(dirpath, "%s/i2c", ocp_path);
    DIR* dir = opendir(dirpath);
    bool found = false;

    if (dir == NULL) {
        log_error("Unable to open the directory: %s\n", dirpath);
        return false;
    }

	struct dirent * entry = readdir(dir);
	while (entry != NULL) {
        if (strcmp(entry->d_name + 1, "-0050") == 0)
        {
            found = true;
            sprintf(eeprom_path, "%s/%s/eeprom", dirpath, entry->d_name);
            log_info("\t-eeprom path found at: \'%s\'", eeprom_path);
        }
        if (strcmp(entry->d_name + 2, "-0050") == 0)
        {
            found = true;
            sprintf(eeprom_path, "%s/%s/eeprom", dirpath, entry->d_name);
            log_info("\t-eeprom path found at: \'%s\'", eeprom_path);
        }
		entry = readdir(dir);
    }

    closedir(dir);
    return found;
}

static void removeChar(char * str, char charToRemove){
    int i, j;
    int len = strlen(str);
    for(i=0; i<len; i++)
        if(str[i] == charToRemove) {
            for(j=i; j<len; j++)
                str[j] = str[j+1];
            len--;
            i--;
        }
}

static void print_help(void)
{
    printf("art-eeprom-format: Format Manufacturing data in ART Card's EEPROM");
}

int main(int argc, char *argv[])
{
    int ret = 0;
    char ocp_path[256] = {0};
    struct eeprom_manufacturing_data data_read;

	log_set_level(2);

    if (argc != 2)
    {
        log_error("Wrong input, please provide an valid ocp !");
        return(0);
    }

	log_info("Checking input:");
    snprintf(ocp_path, sizeof(ocp_path) - 1, "/sys/class/timecard/%s", argv[1]);

	log_info("\t-ocp path is: \"%s\", checking...", ocp_path);
	if (access(ocp_path, F_OK) != -1)
	{
        log_info("\t-ocp path exists !");
    }
	else
	{
        log_error("\t-ocp path doesn't exists !");
        return(0);
    }

    log_info("Checking eeprom:");
    char eeprom_path[256];
    if (!detect_eeprom_path(ocp_path, eeprom_path))
    {
        log_error("\t-eeprom_path not found !");
        return(0);
    }

    log_info("Reading current eeprom data ...");

    read_eeprom_manufacturing_data(eeprom_path, &data_read);
    init_eeprom_manufacturing_pcba(eeprom_path, &data_read);

    log_info("Writing manufacturing data to %s...", eeprom_path);
    ret = write_eeprom_manufacturing_data(eeprom_path, &data_read);
    if (ret != 0) {
        log_error("Error writing eeprom data");
        goto end;
    }

    log_info("Reading eeprom data after write...");
    read_eeprom_manufacturing_data(eeprom_path, &data_read);

    if (!strcmp(data_read.od_pcba_part_number, "1003066C00") == 0)
    {
        log_error("Invalid write, Please make sure to have write access on factory eeprom !");
        goto end;
    }
    else
    {
        log_info("EEPROM successfully reformated !");
    }

end:
    return ret;
}
