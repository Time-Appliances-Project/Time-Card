/**
 * @file art_integration_test_suite.c
 * @brief Integration test suite program for art card.
 * @version 0.1
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2022
 * Check wether art card handled by ptp_ocp driver works
 * by interacting will all its devices
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dirent.h>
#include <getopt.h>
#include <linux/limits.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include "../../common/mRO50_ioctl.h"
#include "config.h"
#include "configurable_io_test.h"
#include "gnss_serial_test.h"
#include "log.h"
#include "mro_device_test.h"
#include "phase_error_tracking_test.h"
#include "ptp_device_test.h"
#include "utils.h"

#define READ        0
#define WRITE       1

#define SOCKET_PORT 2970

static void print_help(void) {
    printf("usage: art_integration_test_suite [-h] -p PATH -s SERIAL_NUMBER\n");
    printf("Parameters:\n");
    printf("- -p PATH: path of the file/EEPROM data should be written from\n");
    printf("- -s SERIAL_NUMBER: Serial number that should be written within data."
           "Serial must start with an F followed by 8 numerical characters\n");
    printf("- -h: prints help\n");
    return;
}

static bool test_ocp_directory(char* ocp_path, char* serial_number, struct devices_path* devices_path) {
    DIR*     ocp_dir = opendir(ocp_path);
    uint32_t mro50_coarse_value;
    bool     disciplining_config_found = false;
    bool     temperature_table_found   = false;
    bool     gnss_receiver_passed      = false;
    bool     config_io_passed          = false;
    bool     mro50_passed              = false;
    bool     found_eeprom              = false;
    bool     ptp_passed                = false;

    if (ocp_dir == NULL) {
        log_error("Directory %s does not exists\n", ocp_path);
        return false;
    }

    struct dirent* entry = readdir(ocp_dir);
    while (entry != NULL) {
        /* I2C TEST: Find EEPROM File
         * EEPROM file will be written if test is successful
         */
        if (strncmp(entry->d_name, "i2c", 4) == 0) {
            log_info("I2C device detected");
            char pathname[PATH_MAX]; /* should always be big enough */
            sprintf(pathname, "%s/%s", ocp_path, entry->d_name);
            found_eeprom = find_file(realpath(pathname, NULL), "eeprom", devices_path->eeprom_path);
            if (found_eeprom) {
                log_info("\t- Found EEPROM file: %s\n", devices_path->eeprom_path);
            } else {
                log_warn("\t- Could not find EEPROM file\n");
            }

            /* MRO50 TEST: Perform R/W operations using ioctls
             * Also read factory coarse which needs to be written in EEPROM
             */
        } else if (strncmp(entry->d_name, "ttyMAC", 6) == 0) {
            log_info("mro50 device detected");
            int fd = open("/dev/mro50.0", O_RDWR);
            if (fd < 0) {
                log_error("Could not open mRo50 device\n");
            }

            /* Activate serial in order to use mro50-serial device */
            uint32_t serial_activate = 1;
            int      ret = ioctl(fd, MRO50_BOARD_CONFIG_WRITE, &serial_activate);
            if (ret != 0) {
                log_error("Could not activate mro50 serial");
            }
            find_dev_path(ocp_path, entry, devices_path->mro_path);
            int mro50 = open(devices_path->mro_path, O_RDWR | O_NONBLOCK);
            if (mro50 > 0) {
                mro50_passed = test_mro50_device(mro50);
                if (mro50_passed) {
                    /* Read factory coarse of the mRO50 which needs to be stored in EEPROM */
                    if (mro50_read_coarse(mro50, &mro50_coarse_value) != 0) {
                        log_error("Could not read factory coarse value of mRO50");
                        mro50_passed = false;
                    }
                }
                close(mro50);
            } else {
                log_error("\t- Error opening mro50 device");
            }

            /* PTP CLOCK TEST: Set clock time */
        } else if (strncmp(entry->d_name, "ptp", 4) == 0) {
            log_info("ptp clock device detected");
            find_dev_path(ocp_path, entry, devices_path->ptp_path);
            int ptp_clock = open(devices_path->ptp_path, O_RDWR);
            if (ptp_clock > 0) {
                ptp_passed = test_ptp_device(ptp_clock);
                close(ptp_passed);
            } else {
                log_error("\t- Error opening ptp device");
            }

            /* SERIAL GNSS TEST:
             * Check serial can be opened
             * Reconfigure GNSS to default configuration
             * check it can receive a fix and UBX-MON-RF message
             */
        } else if (strncmp(entry->d_name, "ttyGNSS", 7) == 0) {
            log_info("ttyGPS detected");
            find_dev_path(ocp_path, entry, devices_path->gnss_path);
            gnss_receiver_passed = test_gnss_serial(devices_path->gnss_path);
        } else if (strncmp(entry->d_name, "disciplining_config", 19) == 0) {
            log_info("disciplining_config file found");
            sprintf(devices_path->disciplining_config_path, "%s/%s", ocp_path, entry->d_name);
            disciplining_config_found = true;
        } else if (strncmp(entry->d_name, "temperature_table", 17) == 0) {
            log_info("temperature_table file found");
            sprintf(devices_path->temperature_table_path, "%s/%s", ocp_path, entry->d_name);
            temperature_table_found = true;
        }

        entry = readdir(ocp_dir);
    }

    /* Test IO */
    config_io_passed = test_configurable_io(ocp_path, devices_path->ptp_path);

    if (!(mro50_passed && ptp_passed && gnss_receiver_passed && found_eeprom && config_io_passed)) {
        log_error("At least one test failed");
        return false;
    } else {
        log_info("All tests passed\n");
        log_info("Writing EEPROM manufacturing data");
        char command[4141];
        sprintf(command, "art_eeprom_format -p %s -s %s", devices_path->eeprom_path, serial_number);
        if (system(command) != 0) {
            log_error("Could not write EEPROM data in %s", devices_path->eeprom_path);
            return false;
        }
        memset(command, 0, 4141);

        if (disciplining_config_found) {
            log_info("Writing Disciplining config in EEPROM");
            sprintf(command,
                    "art_disciplining_manager -p %s -f -c %u",
                    devices_path->disciplining_config_path,
                    mro50_coarse_value);
            if (system(command) != 0) {
                log_error("Could not write factory disciplining parameters in %s",
                          devices_path->disciplining_config_path);
                return false;
            }
            memset(command, 0, 4141);
        } else {
            log_error("Disciplining config file not found!");
            return false;
        }

        if (temperature_table_found) {
            log_info("Writing temperature table in EEPROM");
            sprintf(command, "art_temperature_table_manager -p %s -f", devices_path->temperature_table_path);
            if (system(command) != 0) {
                log_error("Could not write factory temperature table in %s",
                          devices_path->temperature_table_path);
                return false;
            }
            memset(command, 0, 4141);
        } else {
            log_error("Temperature table file not found!");
            return false;
        }
    }
    return true;
}

static void
prepare_config_file_for_oscillatord(char* sysfs_path, int socket_port_offset, struct config* config) {
    char socket_port_number[16];
    /* Define socket port offset */
    snprintf(socket_port_number, sizeof(socket_port_number) - 1, "%d", SOCKET_PORT + socket_port_offset);

    memset(config, 0, sizeof(struct config));

    config_set(config, "disciplining", "true");
    config_set(config, "monitoring", "true");
    config_set(config, "socket-address", "0.0.0.0");
    config_set(config, "socket-port", socket_port_number);
    config_set(config, "oscillator", "mRO50");
    config_set(config, "sysfs-path", sysfs_path);
    config_set(config, "gnss-receiver-reconfigure", "false");
    config_set(config, "opposite-phase-error", "false");
    config_set(config, "debug", "1");
    config_set(config, "calibrate_first", "false");
    config_set(config, "phase_resolution_ns", "5");
    config_set(config, "ref_fluctuations_ns", "30");
    config_set(config, "phase_jump_threshold_ns", "300");
    config_set(config, "reactivity_min", "10");
    config_set(config, "reactivity_max", "30");
    config_set(config, "reactivity_power", "2");
    config_set(config, "fine_stop_tolerance", "200");
    config_set(config, "max_allowed_coarse", "30");
    config_set(config, "nb_calibration", "10");
    config_set(config, "oscillator_factory_settings", "true");
    return;
}

int main(int argc, char* argv[]) {
    struct devices_path devices_path;
    struct config       config;
    char*               serial_number = NULL;
    char*               sysfs_path    = NULL;
    char                ocp_name[100];
    char                temp[1024];
    int                 ocp_number;
    int                 c;

    log_set_level(LOG_DEBUG);
    log_info("ART Program Test Suite");

    while ((c = getopt(argc, argv, "p:s:h")) != -1) {
        switch (c) {
        case 'p':
            sysfs_path = optarg;
            break;
        case 's':
            serial_number = optarg;
            break;
        case 'h':
            print_help();
            return 0;
            break;
        case '?':
            if (optopt == 'p')
                fprintf(stderr, "Option -%c requires path to ART card sysfs.\n", optopt);
            return -1;
            break;
        }
    }

    if (!sysfs_path) {
        printf("Please provide path to ART card sysfs\n");
        return -1;
    }

    log_info("Testing ART card which sysfs is %s", sysfs_path);
    if (test_ocp_directory(sysfs_path, serial_number, &devices_path)) {
        /* Extract ocpX from sysfs */
        sprintf(temp, "%s", sysfs_path);
        char* token = strtok(realpath(temp, NULL), "/");
        while (token != NULL) {
            strncpy(ocp_name, token, sizeof(ocp_name));
            token = strtok(NULL, "/");
        }
        log_debug("OCP name is %s", ocp_name);
        if (1 == sscanf(ocp_name, "%*[^0123456789]%d", &ocp_number)) {
            log_debug("ocp number is %d", ocp_number);
            /* Prepare config file to be used by oscillatord for tests */
            prepare_config_file_for_oscillatord(sysfs_path, ocp_number, &config);

            /* Test card by checking phase error stays in limits defined in phase error tracking test */
            switch (test_phase_error_tracking(ocp_name, &config)) {
            case TEST_PHASE_ERROR_TRACKING_OK:
                /* Test passed without calibration, card is ready */
                break;
            case TEST_PHASE_ERROR_TRACKING_KO:
                /* Test did not pass, card is must not be shipped */
                return -1;
                break;
            default:
                log_error("This test result is not supported !");
                return -1;
            }
        }

    } else {
        log_error("ART Card in %s did not pass all tests", sysfs_path);
        return -1;
    }

    return 0;
}
