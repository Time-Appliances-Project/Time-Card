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
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include "../common/mRO50_ioctl.h"
#include "art_integration_testsuite/gnss_serial_test.h"
#include "art_integration_testsuite/mro_device_test.h"
#include "art_integration_testsuite/phase_error_tracking_test.h"
#include "art_integration_testsuite/ptp_device_test.h"
#include "config.h"
#include "log.h"
#include "utils.h"

#define READ        0
#define WRITE       1

#define SOCKET_PORT 2958

static void print_help(void) {
    printf("usage: art_integration_test_suite [-h] -p SYSFS_PATH\n");
    printf("Parameters:\n");
    printf("- -p SYSFS_PATH: path to ART card sysfs (ex: /sys/class/timecard/ocp0)\n");
    printf("- -h: prints help\n");
    return;
}

static bool test_ocp_directory(char* ocp_path, struct devices_path* devices_path) {
    DIR*     ocp_dir              = opendir(ocp_path);
    bool     gnss_receiver_passed = false;
    uint32_t mro50_coarse_value;
    bool     mro50_passed = false;
    bool     found_eeprom = false;
    bool     ptp_passed   = false;

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
            char pathname[1280]; /* should always be big enough */
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
        } else if (strncmp(entry->d_name, "mro50", 6) == 0) {
            log_info("mro50 device detected");
            find_dev_path(ocp_path, entry, devices_path->mro_path);
            log_info("mro50 device path: %s", devices_path->mro_path);
            int fd = open(devices_path->mro_path, O_RDWR);
            if (fd < 0) {
                log_error("Could not open mRo50 device\n");
            }
            uint32_t serial_activate = 1;
            int      ret = ioctl(fd, MRO50_BOARD_CONFIG_WRITE, &serial_activate);
            if (ret != 0) {
                log_error("Could not activate mro50 serial");
            } else {
                log_info("mro50 serial successfully activated");
            }
        } else if (strncmp(entry->d_name, "ttyMAC", 6) == 0) {
            log_info("mro50 Serial detected");
            find_dev_path(ocp_path, entry, devices_path->mac_path);
            log_info("mro50 Serial path: %s", devices_path->mac_path);

            int mro50 = open(devices_path->mac_path, O_RDWR | O_NONBLOCK);
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
        }

        entry = readdir(ocp_dir);
    }

    /* Test IO */
    if (!(mro50_passed && ptp_passed && gnss_receiver_passed && found_eeprom)) {
        log_error("At least one test failed");
        return false;
    }
    return true;
}

static void prepare_config_file_for_oscillatord(struct devices_path* devices_path,
                                                char*                ocp_name,
                                                int                  socket_port_offset,
                                                struct config*       config,
                                                char*                sysfspath) {
    char socket_port_number[16];
    /* Define socket port offset */
    snprintf(socket_port_number, sizeof(socket_port_number) - 1, "%d", SOCKET_PORT + socket_port_offset);

    memset(config, 0, sizeof(struct config));

    config_set(config, "disciplining", "true");
    config_set(config, "monitoring", "true");
    config_set(config, "socket-address", "0.0.0.0");
    config_set(config, "socket-port", socket_port_number);
    config_set(config, "oscillator", "mRO50");
    config_set(config, "sysfs-path", sysfspath);
    config_set(config, "ptp-clock", devices_path->ptp_path);
    config_set(config, "mro50-device", devices_path->mro_path);
    config_set(config, "gnss-device-tty", devices_path->gnss_path);
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
    config_set(config, "learn_temperature_table", "false");
    config_set(config, "use_temperature_table", "false");
    config_set(config, "oscillator_factory_settings", "true");
    return;
}

int main(int argc, char* argv[]) {
    struct devices_path devices_path = {0};
    struct config       config;
    char*               sysfs_path = NULL;
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
    if (test_ocp_directory(sysfs_path, &devices_path)) {
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
            prepare_config_file_for_oscillatord(&devices_path, ocp_name, ocp_number, &config, sysfs_path);

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
