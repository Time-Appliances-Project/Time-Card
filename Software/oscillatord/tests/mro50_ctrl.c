/*
 * mRO50 oscillator control program.
 * Allows to read / write commands to the mRO50 oscillator
 */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <getopt.h>
#include <poll.h>
#include <sys/fcntl.h>
#include <sys/types.h>

#include "log.h"
#include "mRo50.h"
#include "utils.h"

enum {
    COMMAND_READ,
    COMMAND_WRITE
};

enum {
    TYPE_FINE,
    TYPE_COARSE,
    TYPE_TEMP,
    TYPE_LOCK,
    TYPE_STATUS
};

static void print_help(void) {
    printf("usage: mro50_ctrl [-h] -d DEVICE -c COMMAND -t TYPE [WRITE_VALUE]\n");
    printf("- DEVICE: mrO50 device's path\n");
    printf("- COMMAND: 'read' or 'write'\n");
    printf("- TYPE: 'fine', 'coarse', 'temp', 'lock_flag' or 'status' (temp, lock_flag "
           "and status are read only)\n");
    printf("- WRITE_VALUE: mandatory if command is write.\n");
    printf("- -h: prints help\n");
    return;
}

static int check_device(char* device) {
    if (device == NULL) {
        log_error("Device path not provided!\n");
        return -1;
    }

    if (access(device, F_OK) != -1)
        return 0;
    else {
        log_error("Device path does not exist\n");
        return -1;
    }
}

static int check_command(char* command) {
    if (command == NULL) {
        log_error("Command not specified!\n");
        return -1;
    }

    if (strcmp(command, "read") == 0)
        return COMMAND_READ;
    else if (strcmp(command, "write") == 0)
        return COMMAND_WRITE;
    else {
        log_error("Unknown command %s\n", command);
        return -1;
    }
}

static int check_type(char* type) {
    if (type == NULL) {
        log_error("Type not specified!\n");
        return -1;
    }

    if (strcmp(type, "fine") == 0)
        return TYPE_FINE;
    else if (strcmp(type, "coarse") == 0)
        return TYPE_COARSE;
    else if (strcmp(type, "temp") == 0)
        return TYPE_TEMP;
    else if (strcmp(type, "lock_flag") == 0)
        return TYPE_LOCK;
    else if (strcmp(type, "status") == 0)
        return TYPE_STATUS;
    else {
        log_error("Unknown type %s\n", type);
        return -1;
    }
}

int main(int argc, char** argv) {
    char*    device;
    char*    command;
    int      command_int;
    char*    type = NULL;
    int      type_int;
    long int cmdl_value;
    uint32_t write_value;
    int      index = 0;
    int      c;
    int      err, res;

    log_set_level(LOG_INFO);

    while ((c = getopt(argc, argv, "d:c:t:h")) != -1)
        switch (c) {
        case 'd':
            device = optarg;
            break;
        case 'c':
            command = optarg;
            break;
        case 't':
            type = optarg;
            break;
        case 'h':
            print_help();
            return 0;
        case '?':
            if (optopt == 'd')
                fprintf(stderr, "Option -%c requires mRO50 serial device's path.\n", optopt);
            if (optopt == 'c')
                fprintf(stderr, "Option -%c requires an command type: either read or write.\n", optopt);
            if (optopt == 't')
                fprintf(stderr,
                        "Option -%c requires an control values to command: either fine, "
                        "coarse or serial_activate.\n",
                        optopt);
            else if (isprint(optopt))
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
            return 1;
        default:
            abort();
        }

    err = check_device(device);
    if (err != 0)
        return -1;

    err = check_command(command);
    if (err == -1)
        return err;
    command_int = err;

    err         = check_type(type);
    if (err == -1)
        return err;
    type_int = err;

    int fd   = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        log_error("Could not open mRo50 device\n");
        return -1;
    }
    if (set_serial_attributes(fd) != 0) {
        close(fd);
        return -1;
    }

    // Check for additional parameter if WRITE command is specified
    if (command_int == COMMAND_WRITE) {
        bool write_value_present = false;
        for (index = optind; index < argc; index++) {
            cmdl_value = atol(argv[index]);
            if (cmdl_value < 0) {
                printf("Value to write must be positive");
                return -1;
            }
            write_value         = (unsigned int)cmdl_value;
            write_value_present = true;
            break;
        }

        if (!write_value_present) {
            log_error("Write value not specified!");
            return -1;
        }
    }

    log_info("device = %s, command = %s, type = %s", device, command, type);

    if (command_int == COMMAND_READ) {
        uint32_t value;
        switch (type_int) {
        case TYPE_FINE:
            err = mRo50_oscillator_cmd(fd, CMD_READ_FINE, sizeof(CMD_READ_FINE) - 1);
            if (err > 0) {
                res = sscanf(answer_str, "%x\r\n", &value);
                memset(answer_str, 0, err);
                if (res > 0) {
                    log_info("Fine value: %u", value);
                } else {
                    log_error("Could not parse fine parameter");
                    return -1;
                }
            } else {
                log_error("Fail reading Fine Parameters, err %d, errno %d", err, errno);
                return -1;
            }
            break;
        case TYPE_COARSE:
            err = mRo50_oscillator_cmd(fd, CMD_READ_COARSE, sizeof(CMD_READ_COARSE) - 1);
            if (err > 0) {
                res = sscanf(answer_str, "%x\r\n", &value);
                memset(answer_str, 0, err);
                if (res > 0) {
                    log_info("Coarse value: %u", value);
                } else {
                    log_error("Could not parse coarse parameter");
                    return -1;
                }
            } else {
                log_error("Fail reading Coarse Parameters, err %d, errno %d", err, errno);
                return -1;
            }
            break;
        case TYPE_TEMP:
            err = mRo50_oscillator_cmd(fd, CMD_READ_STATUS, sizeof(CMD_READ_STATUS) - 1);
            if (err == STATUS_ANSWER_SIZE) {
                char EP_temperature[4];
                log_debug("MONITOR1 from mro50 gives %s", answer_str);
                /* Parse mRo50 EP temperature */
                strncpy(EP_temperature, &answer_str[STATUS_EP_TEMPERATURE_INDEX], STATUS_ANSWER_FIELD_SIZE);
                value              = strtoul(EP_temperature, NULL, 16);
                double temperature = compute_temp(value);
                if (temperature == DUMMY_TEMPERATURE_VALUE)
                    return -1;
                log_info("Temperature read: %.2f", temperature);
            } else {
                log_warn("Fail reading attributes, err %d, errno %d", err, errno);
                return -1;
            }
            break;
        case TYPE_LOCK:
            err = mRo50_oscillator_cmd(fd, CMD_READ_STATUS, sizeof(CMD_READ_STATUS) - 1);
            if (err == STATUS_ANSWER_SIZE) {
                /* Parse mRO50 clock lock flag */
                uint8_t lock = answer_str[STATUS_CLOCK_LOCKED_INDEX] & (1 << STATUS_CLOCK_LOCKED_BIT);
                log_info("Lock flag: %s", lock >> STATUS_CLOCK_LOCKED_BIT ? "true" : "false");
                memset(answer_str, 0, STATUS_ANSWER_SIZE);
            } else {
                log_warn("Fail reading attributes, err %d, errno %d", err, errno);
                return -1;
            }
            break;
        case TYPE_STATUS:
            err = mRo50_oscillator_cmd(fd, CMD_READ_STATUS, sizeof(CMD_READ_STATUS) - 1);
            if (err == STATUS_ANSWER_SIZE) {
                log_info("Status: %s", answer_str);
            } else {
                log_warn("Fail reading attributes, err %d, errno %d", err, errno);
                return -1;
            }
            break;
        default:
            log_error("Unhandled type %d", type);
            break;
        }
        return 0;

    } else if (command_int == COMMAND_WRITE) {
        char command[128];
        memset(command, '\0', 128);
        switch (type_int) {
        case TYPE_FINE:
            if (write_value < FINE_RANGE_MIN || write_value > FINE_RANGE_MAX) {
                log_error("value is out of range for fine control !");
                return -1;
            }
            sprintf(command, "MON_tpcb PIL_cfield C %04X\r", write_value);
            err = mRo50_oscillator_cmd(fd, command, strlen(command));
            if (err != 2) {
                log_error("Could not prepare command request to adjust fine frequency, "
                          "error %d, errno %d",
                          err,
                          errno);
                return -1;
            }
            memset(answer_str, 0, mro_answer_len);
            log_info("Wrote %d to fine value", write_value);
            break;
        case TYPE_COARSE:
            if (write_value > COARSE_RANGE_MAX) {
                log_error("value is out of range for coarse control !");
                return -1;
            }
            sprintf(command, "FD %08X\r", write_value);
            err = mRo50_oscillator_cmd(fd, command, strlen(command));
            if (err != 2) {
                log_error("Could not prepare command request to adjust coarse value, "
                          "error %d, errno %d",
                          err,
                          errno);
                return -1;
            }
            memset(answer_str, 0, mro_answer_len);
            log_info("Wrote %d to coarse value", write_value);
            break;
        case TYPE_TEMP:
        case TYPE_LOCK:
        case TYPE_STATUS:
            log_warn("Cannot write %d", type_int);
            break;
        default:
            log_error("Unhandled type %d", type);
            break;
        }
        return 0;
    }

    close(fd);

    return 0;
}
