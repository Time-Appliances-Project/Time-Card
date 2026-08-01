/**
 * @file art_eeprom_files_updater.c
 * @author Charles Parent (charles.parent@orolia2s.com)
 * @brief Program to update disciplining_config and temperature table file exposed by ptp_ocp driver
 * @version 0.1
 * @date 2022-07-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <oscillator-disciplining/oscillator-disciplining.h>

#include "log.h"
#include "eeprom_config.h"

static void print_help(void)
{
    log_info("art_eeprom_files_updater: Update disciplining_config and temperature table exposed by ptp_ocp driver");
    log_info("Usage: art_eeprom_files_updater -p disciplining_config_file_path -t temperature_table_path [-h]");
    log_info("\t-p disciplining_config_file_path: Path to the disciplining_config file exposed by the driver");
    log_info("\t-t temperature_table_path: Path to the temperature_table file exposed by the driver");
    log_info("\t-h: print help");
}

int main(int argc, char *argv[])
{
    char *disciplining_config_path;
    char *temperature_table_path;
    int option;
    int ret = 0;
    log_set_level(LOG_INFO);

    while ((option = getopt(argc, argv, "p:t:h")) != -1) {
        switch(option) {
        case 'p':
            disciplining_config_path = optarg;
            break;
        case 't':
            temperature_table_path = optarg;
            break;
        case 'h':
        default:
            print_help();
            return 0;

        }
    }

    if (disciplining_config_path == NULL) {
        log_error("No path to disciplining_config file provided!");
        print_help();
    }
    if (temperature_table_path == NULL) {
        log_error("No path to temperature_table file provided!");
        print_help();
    }

    log_info("Reading disciplining parameters from both files");
    struct disciplining_parameters dsc_params;
    ret = read_disciplining_parameters_from_eeprom(
        disciplining_config_path,
        temperature_table_path,
        &dsc_params
    );
    if (ret != 0) {
        log_error("An error occurred when reading disciplining parameters of the card");
        return -1;
    }

    log_info("Writing back disciplining parameters to version %d", dsc_params.dsc_config.version);
    ret = write_disciplining_parameters_in_eeprom(
        disciplining_config_path,
        temperature_table_path,
        &dsc_params
    );
    if (ret != 0) {
        log_error("An error occurred when writing disciplining parameters on the card");
        return -1;
    }
    log_info("Disciplining parameters updated !");

    return 0;
}