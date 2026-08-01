#include <errno.h>
#include <string.h>

#include <sys/ioctl.h>

#include "mro_device_test.h"

#include "../mRo50.h"
#include "log.h"
#include "utils.h"

/*
 * Performs read / write on mro50 using ioctl to check wether
 * coarse is working
 */
static bool mro50_check_coarse_read_write(int mro50_fd) {
    uint32_t original_value;
    char     command[128];
    int      err, res;
    /* Read current coarse value of mro50 */

    err = mRo50_oscillator_cmd(mro50_fd, CMD_READ_COARSE, sizeof(CMD_READ_COARSE) - 1);
    if (err > 0) {
        res = sscanf(answer_str, "%x\r\n", &original_value);
        memset(answer_str, 0, err);
        if (res > 0) {
            log_info("Coarse value read on mRO50 is %u", original_value);
        } else {
            log_debug("\t\t- Error %d", res);
            log_error("\t\t- Error reading value of mro50");
            return false;
        }
    } else {
        log_error("Fail reading Coarse Parameters, err %d, errno %d", err, errno);
        return false;
    }

    /* try writing value + 1 or value - 1 to check
     * write operation works
     */
    uint32_t new_value;
    if (original_value + 1 <= COARSE_RANGE_MAX) {
        new_value = original_value + 1;
    } else {
        new_value = original_value - 1;
    }
    sprintf(command, "FD %08X\r", new_value);
    err = mRo50_oscillator_cmd(mro50_fd, command, strlen(command));
    if (err != 2) {
        log_error("Could not prepare command request to adjust coarse value, error %d, "
                  "errno %d",
                  err,
                  errno);
        return false;
    }
    memset(answer_str, 0, mro_answer_len);
    log_info("Wrote %d to coarse value", new_value);

    /* Check value has been well written */
    uint32_t written_value;
    err = mRo50_oscillator_cmd(mro50_fd, CMD_READ_COARSE, sizeof(CMD_READ_COARSE) - 1);
    if (err > 0) {
        res = sscanf(answer_str, "%x\r\n", &written_value);
        memset(answer_str, 0, err);
        if (res > 0) {
            log_info("Coarse value: %u", written_value);
            if (written_value != new_value) {
                return false;
            }
        } else {
            log_debug("\t\t- Error %d", res);
            log_error("\t\t- Error reading value of mro50");
            return false;
        }
    } else {
        log_error("Fail reading Coarse Parameters, err %d, errno %d", err, errno);
        return false;
    }

    /* Write back previous value to preserve configuration */
    memset(command, 0, 128);
    sprintf(command, "FD %08X\r", original_value);
    err = mRo50_oscillator_cmd(mro50_fd, command, strlen(command));
    if (err != 2) {
        log_error("Could not prepare command request to adjust coarse value, error %d, "
                  "errno %d",
                  err,
                  errno);
        return false;
    }
    memset(answer_str, 0, mro_answer_len);

    return true;
}

/*
 * Performs read / write on mro50 using ioctl to check wether
 * fine is working
 */
static bool mro50_check_fine_read_write(int mro50_fd) {
    uint32_t original_value;
    char     command[128];
    int      err, res;
    /* Read current fine value of mro50 */

    err = mRo50_oscillator_cmd(mro50_fd, CMD_READ_FINE, sizeof(CMD_READ_FINE) - 1);
    if (err > 0) {
        res = sscanf(answer_str, "%x\r\n", &original_value);
        memset(answer_str, 0, err);
        if (res > 0) {
            log_info("Fine value: %u", original_value);
            log_info("Fine value read on mRO50 is %u", original_value);
        } else {
            log_debug("\t\t- Error %d", res);
            log_error("\t\t- Error reading value of mro50");
            return false;
        }
    } else {
        log_error("Fail reading Fine Parameters, err %d, errno %d", err, errno);
        return false;
    }

    /* try writing value + 1 or value - 1 to check
     * write operation works
     */
    uint32_t new_value;
    if (original_value + 1 <= FINE_RANGE_MAX) {
        new_value = original_value + 1;
    } else {
        new_value = original_value - 1;
    }
    sprintf(command, "MON_tpcb PIL_cfield C %04X\r", new_value);
    err = mRo50_oscillator_cmd(mro50_fd, command, strlen(command));
    if (err != 2) {
        log_error("Could not prepare command request to adjust fine value, error %d, "
                  "errno %d",
                  err,
                  errno);
        return false;
    }
    memset(answer_str, 0, mro_answer_len);
    log_info("Wrote %d to fine value", new_value);

    /* Check value has been well written */
    uint32_t written_value;
    err = mRo50_oscillator_cmd(mro50_fd, CMD_READ_FINE, sizeof(CMD_READ_FINE) - 1);
    if (err > 0) {
        res = sscanf(answer_str, "%x\r\n", &written_value);
        memset(answer_str, 0, err);
        if (res > 0) {
            log_info("Fine value: %u", written_value);
            if (written_value != new_value) {
                return false;
            }
        } else {
            log_debug("\t\t- Error %d", res);
            log_error("\t\t- Error reading value of mro50");
            return false;
        }
    } else {
        log_error("Fail reading Fine Parameters, err %d, errno %d", err, errno);
        return false;
    }

    /* Write back previous value to preserve configuration */
    memset(command, 0, 128);
    sprintf(command, "MON_tpcb PIL_cfield C %04X\r", original_value);
    err = mRo50_oscillator_cmd(mro50_fd, command, strlen(command));
    if (err != 2) {
        log_error("Could not prepare command request to adjust Fine value, error %d, "
                  "errno %d",
                  err,
                  errno);
        return false;
    }
    memset(answer_str, 0, mro_answer_len);

    return true;
}

static bool mro50_read_temperature(int mro50_fd) {
    uint32_t value;
    int      err;

    err = mRo50_oscillator_cmd(mro50_fd, CMD_READ_STATUS, sizeof(CMD_READ_STATUS) - 1);
    if (err == STATUS_ANSWER_SIZE) {
        char EP_temperature[4];
        /* Parse mRo50 EP temperature */
        strncpy(EP_temperature, &answer_str[STATUS_EP_TEMPERATURE_INDEX], STATUS_ANSWER_FIELD_SIZE);
        value              = strtoul(EP_temperature, NULL, 16);
        double temperature = compute_temp(value);
        if (temperature == DUMMY_TEMPERATURE_VALUE) {
            log_error("Could not compute temperature of mRo50");
            return false;
        }
        log_info("\t\t- Temperature read is %f °C", temperature);
    } else {
        log_warn("Fail reading attributes, err %d, errno %d", err, errno);
        return false;
    }
    return true;
}

int mro50_read_coarse(int mro50_fd, uint32_t* coarse) {
    int err, res;
    if (coarse == NULL)
        return -EFAULT;

    /* Read current coarse value of mro50 */
    err = mRo50_oscillator_cmd(mro50_fd, CMD_READ_COARSE, sizeof(CMD_READ_COARSE) - 1);
    if (err > 0) {
        res = sscanf(answer_str, "%x\r\n", coarse);
        memset(answer_str, 0, err);
        if (res > 0) {
            log_info("Coarse value read on mRO50 is %u", *coarse);
        } else {
            log_debug("\t\t- Error %d", err);
            log_error("\t\t- Error reading value of mro50");
            return -1;
        }
    } else {
        log_error("Fail reading Coarse Parameters, err %d, errno %d", err, errno);
        return -1;
    }
    return 0;
}

bool test_mro50_device(int mro50_fd) {
    log_info("\t- Testing mro50 device...");

    if (set_serial_attributes(mro50_fd) != 0) {
        log_error("Could not set serial attributes");
        return -1;
    }

    log_info("\t- Testing read / write operations on coarse value:");
    if (mro50_check_coarse_read_write(mro50_fd)) {
        log_info("\t\t- Coarse operations working !");
    } else {
        log_error("\t\t- Error reading / writing coarse value");
        return false;
    }
    log_info("\t\t- Passed\n");

    log_info("\t- Testing read / write operations on fine value:");
    if (mro50_check_fine_read_write(mro50_fd)) {
        log_info("\t\t- Fine operations working !");
    } else {
        log_error("\t\t- Error reading / writing fine value");
        return false;
    }
    log_info("\t\t- Passed\n");
    log_info("\t- Testing temperature read...");

    if (mro50_read_temperature(mro50_fd)) {
        log_info("Temperature read");
    } else {
        log_error("Could not read temperature");
        return false;
    }

    log_info("\t\t- Passed\n");

    return true;
}
