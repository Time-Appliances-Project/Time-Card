/**
 * @file art_temperature_table_manager.c
 * @brief Read and Write temperature table from/to ART card's EEPROM
 * @version 0.1
 * @date 2022-06-13
 *
 * @copyright Copyright (c) 2022
 *
 */
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <oscillator-disciplining/oscillator-disciplining.h>

#include "eeprom.h"
#include "eeprom_config.h"
#include "log.h"

enum Mode {
    ART_TEMPERATURE_TABLE_NONE,
    ART_TEMPERATURE_TABLE_READ,
    ART_TEMPERATURE_TABLE_WRITE,
    ART_TEMPERATURE_TABLE_RESET
};

static void print_help(void)
{
    log_info("art_temperature_table_manager -p temperature_table_path [-w input_table.txt | -r -o output_table.txt | -f] -h");
    log_info("\t-p temperature_table_path: Path to temperature_table file exposed by the driver");
    log_info("\t-w input_table.txt: Path to input temperature table to write to temperature_table_path");
    log_info("\t-r: Read temperature table from temperature_table_path");
    log_info("\t-f: Reset temperature table in temperature_table_path");
    log_info("\t-o: output_file_path: write temperature table read in output_file_path");
    log_info("\t-h: print help");
}

static int float_array_parser(const char* value, float **result) {
    char *endptr;
    char *ptr;
    const char *delim = ",";
    float buffer[2];
    int parsed = 0;
    float value_float;

    errno = 0;

    ptr = strtok((char *) value, delim);
    while (ptr != NULL)
    {
        if (parsed >= 2) {
            return -ERANGE;
        }
        value_float = strtold(ptr, &endptr);
        if (value_float == HUGE_VAL ||
            (value_float == 0 && errno == ERANGE))
            return -ERANGE;
        buffer[parsed] = value_float;
        parsed++;
        ptr = strtok(NULL, delim);
    }

    float *values = malloc(parsed * sizeof(float));
    if (values == NULL) {
        return -ENOMEM;
    }
    for (int i = 0; i < parsed; i++) {
        values[i] = buffer[i];
    }

    *result = values;

    return parsed;
}

static int read_temperature_table_from_file(char * path, struct temperature_table *temp_table) {
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read = 0;

    fp = fopen(path, "r");
    if (!fp) {
        log_error("cannot open file at %s", path);
        return -1;
    }
    while((read = getline(&line, &len, fp)) != -1) {
        log_debug("%s", line);
        char tmp[256];
        float *temp_value_tuple = NULL;
        strcpy(tmp, line);
        int num_floats = float_array_parser(tmp, &temp_value_tuple);
        if (num_floats == 2) {
            int temperature_index;
            if (temp_value_tuple[0] < MIN_TEMPERATURE || temp_value_tuple[0] >= MAX_TEMPERATURE) {
                log_error("Temperature %.2f is out of range", temp_value_tuple[0]);
                return -1;
            }
            temperature_index = (int) floor(STEPS_BY_DEGREE * (temp_value_tuple[0] - MIN_TEMPERATURE));
            log_info("writing %.2f to range [%.2f, %.2f[",
                (float) round(temp_value_tuple[1] * 10) / 10,
                (temperature_index + STEPS_BY_DEGREE * MIN_TEMPERATURE) / STEPS_BY_DEGREE,
                (temperature_index + 1 + STEPS_BY_DEGREE * MIN_TEMPERATURE) / STEPS_BY_DEGREE
            );
            temp_table->mean_fine_over_temperature[temperature_index] = round(temp_value_tuple[1] * 10);
        }
        free(temp_value_tuple);
    }
    free(line);
    return 0;
}

static int write_temperature_table_to_file(char *path, struct temperature_table *temp_table)
{
    FILE *fp;
    fp = fopen(path, "w");
    if (!fp) {
        log_error("cannot open %s", path);
        return -1;
    }
    for (int i = 0; i < MEAN_TEMPERATURE_ARRAY_MAX  ; i++) {
        char line[256];
        sprintf(line, "%.2f,%.2f\n", 
            MIN_TEMPERATURE + (float) i / STEPS_BY_DEGREE,
            (float) temp_table->mean_fine_over_temperature[i] / 10.0
        );
        fputs(line, fp);
    }
    fclose(fp);
    return 0;
}

int main(int argc, char *argv[])
{
    struct temperature_table temp_table;
    enum Mode mode = ART_TEMPERATURE_TABLE_NONE;
    char *input_file = NULL;
    char *output_file = NULL;
    char *path;
    int option, ret;
    log_set_level(LOG_INFO);

    while ((option = getopt(argc, argv, "p:w:ho:rf")) != -1) {
        switch (option)
        {
        case 'p':
            path = optarg;
            break;
        case 'r':
            mode = ART_TEMPERATURE_TABLE_READ;
            break;
        case 'w':
            mode = ART_TEMPERATURE_TABLE_WRITE;
            input_file = optarg;
            break;
        case 'f':
            mode = ART_TEMPERATURE_TABLE_RESET;
            break;
        case 'o':
            output_file = optarg;
            break;
        case '?':
            if (optopt == 'm')
                log_error("Option -%c requires path to temperature_table file exposed by the driver.\n", optopt);
            if (optopt == 'w')
                log_error("Option -%c path to input temperature table file.\n", optopt);
            if (optopt == 'o')
                log_error("Option -%c path to output temperature table file.\n", optopt);
            return -1;
            break;
        case 'h':
        default:
            print_help();
            return 0;
            break;
        }
    }

    if (path == NULL) {
        log_error("No mro50 path provided!");
        print_help();
        return -1;
    }

    memset(&temp_table, 0, sizeof(struct temperature_table));

    switch (mode) {
        case ART_TEMPERATURE_TABLE_READ:
            ret = read_temperature_table_from_temperature_table_file(path, &temp_table);
            if (ret != 0) {
                log_error("read_temperature_table_from_temperature_table_file: %d");
                return -1;
            }
            print_temperature_table(temp_table.mean_fine_over_temperature, LOG_INFO);
            if (output_file != NULL) {
                log_info("writing value in %s", output_file);
                ret = write_temperature_table_to_file(output_file, &temp_table);
                if (ret != 0) {
                    log_error("write_temperature_table_to_file: %d");
                    return -1;
                }
            }
            break;
        case ART_TEMPERATURE_TABLE_WRITE:
            ret = read_temperature_table_from_file(input_file, &temp_table);
            if (ret != 0) {
                log_error("read_temperature_table_from_file: %d");
                return -1;
            }
            temp_table.header = HEADER_MAGIC;
            temp_table.version = 1;
            ret = write_temperature_table_to_temperature_table_file(path, &temp_table);
            if (ret != 0) {
                log_error("write_temperature_table_to_temperature_table_file: %d");
                return -1;
            }
            break;
        case ART_TEMPERATURE_TABLE_RESET:
            temp_table.header = HEADER_MAGIC;
            temp_table.version = 1;
            log_info("Resetting temperature table in %s", path);
            ret = write_temperature_table_to_temperature_table_file(path, &temp_table);
            if (ret != 0) {
                log_error("write_temperature_table_to_temperature_table_file: %d");
                return -1;
            }
            log_info("Temperature table reset");
            break;
        default:
            break;
    }
    return 0;
}
