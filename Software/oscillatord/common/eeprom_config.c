#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eeprom_config.h"
#include "log.h"

int read_file(char path[PATH_MAX], char *data, size_t size)
{
    FILE *fp = fopen(path,"rb");
    if (fp != NULL) {
        int ret = fread(data, size, 1, fp);
        if (ret != 1) {
            log_error("Error reading file %s", path);
            return -1;
        }
        fclose(fp);
    } else {
        log_error("Could not open file at %s", path);
        return -1;
    }
    return 0;
}

int write_file(char path[PATH_MAX], char *data, size_t size)
{
    FILE *fp = fopen(path,"wb");
    if(fp != NULL) {
        size_t written = fwrite(data, 1, size, fp);
        fclose(fp);
        if (written != size) {
            log_error("Could not write all data: %d written, %d asked", written, size);
            return -1;
        }
        log_debug("Bytes written: %d", written);
    } else {
        log_error("Could not open file at %s", path);
        return -1;
    }
    return 0;

}

int write_disciplining_parameters_to_disciplining_config_file(const char *path, struct disciplining_config *config)
{
    char buffer[DISCIPLINING_CONFIG_FILE_SIZE];
    int ret;

    if (config == NULL) {
        log_error("config is NULL");
        return -EINVAL;
    }

    memset(buffer, 0, DISCIPLINING_CONFIG_FILE_SIZE * sizeof(char));
    memcpy(buffer, config, sizeof(struct disciplining_config_V_1));

    ret = write_file((char *) path, buffer, DISCIPLINING_CONFIG_FILE_SIZE);
    if (ret != 0) {
        log_error("Could not write data in %s", path);
        return -1;
    }

    return 0;
}

int read_disciplining_parameters_from_disciplining_config_file(const char *path, struct disciplining_config *config)
{
    char buffer[DISCIPLINING_CONFIG_FILE_SIZE];
    int dsc_config_version = 0;
    int ret;

    ret = read_file((char *) path, (char *) buffer, DISCIPLINING_CONFIG_FILE_SIZE);
    if (ret != 0) {
        log_error("Could not read disciplining config at %s", path);
        return -1;
    }
    if (check_header_valid(buffer[0])) {
        dsc_config_version = buffer[1];
        log_info("Version of disciplining_config file: %d", dsc_config_version);
        if (dsc_config_version == 1) {
            /*
             * Data in files is stored in format version 1
             * fill struct disciplining_parameters
             */
            memcpy(config, buffer, sizeof(struct disciplining_config_V_1));
            return 0;
        } else {
            log_error("Unknown version %d", dsc_config_version);
            return -1;
        }
    } else {
        log_error("Header in %s is not valid !", path);
        log_error("Please upgrade disciplining_config and temperature table using art_eeprom_data_updater !");
        return -1;
    }
}

/**
 * @brief Read temperature table using file exposed by the driver
 *
 * @param path
 * @param temp_table
 * @return int
 */
int read_temperature_table_from_temperature_table_file(char *path, struct temperature_table *temp_table)
{
    char buffer[TEMPERATURE_TABLE_FILE_SIZE];
    uint8_t temp_table_version;
    int ret;

    if (temp_table == NULL) {
        log_error("temp_table is NULL");
        return -EINVAL;
    }

    ret = read_file(path, (char *) buffer, TEMPERATURE_TABLE_FILE_SIZE);
    if (ret != 0) {
        log_error("Could not read temperature table at %s", path);
    }

    /* Check presence of header in both file */
    if (check_header_valid(buffer[0])) {
        temp_table_version = buffer[1];
        log_info("Version of temperature_table file: %d", temp_table_version);
        if (temp_table_version == 1) {
            /*
             * Data in files is stored in format version 1
             * fill struct disciplining_parameters
             */
            memcpy(temp_table, buffer, sizeof(struct temperature_table_V_1));
            return 0;
        } else {
            log_error("Unknown version %d", temp_table_version);
            return -1;
        }
    } else {
        log_error("Header in %s is not valid !", path);
        log_error("Please upgrade disciplining_config and temperature table using art_eeprom_data_updater !");
        return -1;
    }

    return 0;
}

int write_temperature_table_to_temperature_table_file(char *path, struct temperature_table *temp_table)
{
    char buffer[TEMPERATURE_TABLE_FILE_SIZE];
    int ret;

    if (temp_table == NULL) {
        log_error("dsc_params is NULL");
        return -EINVAL;
    }

    memset(buffer, 0, TEMPERATURE_TABLE_FILE_SIZE * sizeof(char));

    memcpy(buffer, temp_table, sizeof(struct temperature_table_V_1));

    ret = write_file(path, buffer, TEMPERATURE_TABLE_FILE_SIZE);
    if (ret != 0) {
        log_error("Could not write data in %s", path);
    }

    return 0;
}

/**
 * @brief Fill struct dsc_params_V1 with data from struct dsc_params_V0
 *
 * @param dsc_params_V0
 * @param dsc_params_V1
 */
static void convert_disciplining_parameters_V0_to_V1(struct disciplining_parameters_V_0 *dsc_params_V0, struct disciplining_parameters_V_1 *dsc_params_V1) {
    dsc_params_V1->dsc_config.header = HEADER_MAGIC;
    dsc_params_V1->dsc_config.version = 1;
    dsc_params_V1->dsc_config.ctrl_nodes_length = dsc_params_V0->ctrl_nodes_length;
    dsc_params_V1->dsc_config.ctrl_nodes_length_factory = dsc_params_V0->ctrl_nodes_length_factory;
    dsc_params_V1->dsc_config.coarse_equilibrium = dsc_params_V0->coarse_equilibrium;
    dsc_params_V1->dsc_config.coarse_equilibrium_factory = dsc_params_V0->coarse_equilibrium_factory;
    dsc_params_V1->dsc_config.calibration_date = dsc_params_V0->calibration_date;
    dsc_params_V1->dsc_config.calibration_valid = dsc_params_V0->calibration_valid;
    dsc_params_V1->dsc_config.estimated_equilibrium_ES = dsc_params_V0->estimated_equilibrium_ES;

    for (int i = 0; i < dsc_params_V1->dsc_config.ctrl_nodes_length; i++) {
        dsc_params_V1->dsc_config.ctrl_load_nodes[i] = dsc_params_V0->ctrl_load_nodes[i];
        dsc_params_V1->dsc_config.ctrl_drift_coeffs[i] = dsc_params_V0->ctrl_drift_coeffs[i];
    }

    for (int i = 0; i < dsc_params_V1->dsc_config.ctrl_nodes_length_factory; i++) {
        dsc_params_V1->dsc_config.ctrl_load_nodes_factory[i] = dsc_params_V0->ctrl_load_nodes_factory[i];
        dsc_params_V1->dsc_config.ctrl_drift_coeffs_factory[i] = dsc_params_V0->ctrl_drift_coeffs_factory[i];
    }

    dsc_params_V1->temp_table.header = HEADER_MAGIC;
    dsc_params_V1->temp_table.version = 1;
    for (int i = 0; i < MEAN_TEMPERATURE_ARRAY_MAX; i++) {
        dsc_params_V1->temp_table.mean_fine_over_temperature[i] = dsc_params_V0->mean_fine_over_temperature[i];
    }
    return;
}

/**
 * @brief Read data from eeprom from two files and fill struct disciplining_parameters
 *
 * @param disciplining_config_path
 * @param temperature_table_path
 * @param dsc_params
 * @return int
 */
int read_disciplining_parameters_from_eeprom(
    char disciplining_config_path[PATH_MAX],
    char temperature_table_path[PATH_MAX],
    struct disciplining_parameters *dsc_params
) {
    char dsc_config_data[DISCIPLINING_CONFIG_FILE_SIZE];
    char temp_table[TEMPERATURE_TABLE_FILE_SIZE];
    uint8_t dsc_config_version;
    uint8_t temp_table_version;
    int ret;

    if (dsc_params == NULL) {
        log_error("dsc_params is NULL");
        return -EINVAL;
    }

    ret = read_file(disciplining_config_path, (char *) dsc_config_data, DISCIPLINING_CONFIG_FILE_SIZE);
    if (ret != 0) {
        log_error("Could not read disciplining config at %s", disciplining_config_path);
    }

    ret = read_file(temperature_table_path, (char *) temp_table, TEMPERATURE_TABLE_FILE_SIZE);
    if (ret != 0) {
        log_error("Could not read temperature table at %s", temperature_table_path);
    }

    /* Check presence of header in both file */
    if (check_header_valid(dsc_config_data[0]) && check_header_valid(temp_table[0])) {
        dsc_config_version = dsc_config_data[1];
        temp_table_version = temp_table[1];
        log_info("Version of disciplining_config file: %d", dsc_config_version);
        log_info("Version of temperature_table file: %d", temp_table_version);
        if (dsc_config_version != temp_table_version) {
            log_warn("Version mismatch between files !");
            return -1;
        } else if (dsc_config_version == 1) {
            /*
             * Data in files is stored in format version 1
             * fill struct disciplining_parameters
             */
            memcpy(&dsc_params->dsc_config, dsc_config_data, sizeof(struct disciplining_config_V_1));
            memcpy(&dsc_params->temp_table, temp_table, sizeof(struct temperature_table_V_1));
            return 0;
        } else {
            log_error("Unknown version %d", dsc_config_version);
            return -1;
        }
    } else if (!check_header_valid(dsc_config_data[0]) && !check_header_valid(temp_table[0])){
        /*
         * Header is not present, so we assume file mapping corresponds to struct disciplining_parameters_V_0
         * which is split between both file
         * fill struct disciplining_parameters_V_0 and convert it to  struct disciplining_parameters
         */
        log_warn("Header not found in both disciplining_config or temperature table!");
        log_info("Assuming data stored in files is of version 0 of struct disciplining_parameters");
        struct disciplining_parameters_V_0 dsc_params_V0;
        memcpy(&dsc_params_V0, dsc_config_data, DISCIPLINING_CONFIG_FILE_SIZE);
        memcpy(((uint8_t *) &dsc_params_V0) + DISCIPLINING_CONFIG_FILE_SIZE * sizeof(char), temp_table, sizeof(struct disciplining_parameters_V_0) - DISCIPLINING_CONFIG_FILE_SIZE);
        convert_disciplining_parameters_V0_to_V1(&dsc_params_V0, dsc_params);
        return 0;
    } else {
        if (!check_header_valid(dsc_config_data[0])) {
            log_error("Header not found in %s file but found in %s", disciplining_config_path, temperature_table_path);
        } else {
            log_error("Header not found in %s file but found in %s", temperature_table_path, disciplining_config_path);
        }
        log_error("Please reconfigure both file to latest version (%d)", DISCIPLINING_CONFIG_VERSION);
        return -1;
    }

    return 0;
}

int write_disciplining_parameters_in_eeprom(
    char disciplining_config_path[PATH_MAX],
    char temperature_table_path[PATH_MAX],
    struct disciplining_parameters *dsc_params
) {
    char dsc_config_data[DISCIPLINING_CONFIG_FILE_SIZE];
    char temp_table[TEMPERATURE_TABLE_FILE_SIZE];
    int ret;

    if (dsc_params == NULL) {
        log_error("dsc_params is NULL");
        return -EINVAL;
    }

    memset(dsc_config_data, 0, DISCIPLINING_CONFIG_FILE_SIZE * sizeof(char));
    memset(temp_table, 0, TEMPERATURE_TABLE_FILE_SIZE * sizeof(char));

    memcpy(dsc_config_data, &dsc_params->dsc_config, sizeof(struct disciplining_config_V_1));
    memcpy(temp_table, &dsc_params->temp_table, sizeof(struct temperature_table_V_1));

    ret = write_file(disciplining_config_path, dsc_config_data, DISCIPLINING_CONFIG_FILE_SIZE);
    if (ret != 0) {
        log_error("Could not write data in %s", disciplining_config_path);
    }

    ret = write_file(temperature_table_path, temp_table, TEMPERATURE_TABLE_FILE_SIZE);
    if (ret != 0) {
        log_error("Could not write data in %s", temperature_table_path);
    }

    return 0;
}
