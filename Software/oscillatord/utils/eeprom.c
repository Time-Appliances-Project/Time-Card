#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "eeprom.h"

#define min(a,b) a < b ? a : b

static uint8_t gencrc(uint8_t *data, size_t len)
{
    uint8_t crc = 0xff;
    size_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if ((crc & 0x80) != 0)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc <<= 1;
        }
    }
    return crc;
}

int init_eeprom_manufacturing_pcba(const char *path, struct eeprom_manufacturing_data *data)
{
    strncpy(data->od_pcba_part_number, "1003066C00", min(sizeof("1003066C00") , 12));
    return(0);
}

int write_eeprom_manufacturing_data(const char *path, struct eeprom_manufacturing_data *data)
{
    int err = 0;
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        log_error("ERROR Opening file");
        return -1;
    }
    int ret = fseek(fp, 2 * 256, SEEK_SET);
    if (ret != 0) {
        log_error("Error moving pointer across file");
        err = -1;
    } else {

        size_t written = fwrite(data, 1, sizeof(*data), fp);
        if (written != sizeof(*data)) {
            log_error("Did not write all bytes in eeprom data");
            ret = -1;
        }
    }
    fclose(fp);
    return err;
}

int read_eeprom_manufacturing_data(const char *path, struct eeprom_manufacturing_data *data)
{
    FILE *fp;
    int ret;

    if (!data) {
        return -EFAULT;
    }

    fp = fopen(path,"rb");
    if(!fp) {
        log_error("Could not open file at %s", path);
    }

    ret = fseek(fp, 2 * 256, SEEK_SET);
    if (ret != 0)
        log_error("Error moving pointer across file");
    else {
        ret = fread(data, sizeof(struct eeprom_manufacturing_data), 1, fp);
        if(ret != 1)
            log_error("Could not read eeprom data");
        else
            print_eeprom_manufacturing_data(data);
    }
    fclose(fp);
    return ret;
}

void init_manufacturing_eeprom_data(struct eeprom_manufacturing_data *data, char *serial_number)
{
    time_t s;
    struct tm* current_time;

    data->magic = 0xFBFB;
    data->format_version = 3;
    data->product_production_state = MP;
    data->product_version = 5;
    data->product_sub_version = 0;

    strncpy(data->product_name, "TIME CARD", min(sizeof("TIME CARD"), 20));

    strncpy(data->system_assembly_part_number, "19002225", min(sizeof("19002225") , 12));
    strncpy(data->fb_pcba_part_number, "13200014402", min(sizeof("13200014402"), 12));
    strncpy(data->fb_pcb_part_number, "13100010902", min(sizeof("13100010902") , 12));
    strncpy(data->od_pcba_part_number, "1003066C00", min(sizeof("1003066C00") , 12));

    strncpy(data->od_pcba_serial_number, serial_number, min(strlen(serial_number) , 12));
    strncpy(data->product_serial_number, serial_number, min(strlen(serial_number), 13));

    strncpy(data->system_manufacturer, "OROLIA", min(sizeof("OROLIA"), 8));
    strncpy(data->assembled_at, "ASTEEL", min(sizeof("ASTEEL"), 7));

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncpy(data->product_part_number, "00000000", min(sizeof("00000000"), 8));
    strncpy(data->product_asset_tag, "000000000000", min(sizeof("000000000000"), 10));
    strncpy(data->local_mac_address, "000000000000", min(sizeof("000000000000"),12));
    strncpy(data->extended_mac_address_base, "000000000000", min(sizeof("000000000000"),12));
    data->extended_mac_address_size = 0;
    strncpy(data->eeprom_location_on_fabric, "TIME CARD", min(sizeof("TIME CARD"), 9));
    strncpy(data->pcb_manufacturer, "JOVE", min(sizeof("JOVE"), 4));
#pragma GCC diagnostic pop

    /*Fill in date with local time */
    s = time(NULL);
    current_time = localtime(&s);

    data->system_manufacturing_date_day = current_time->tm_mday;
    data->system_manufacturing_date_month = current_time->tm_mon + 1;
    data->system_manufacturing_date_year = current_time->tm_year + 1900;

    data->crc8 = gencrc((uint8_t *) data, sizeof(struct eeprom_manufacturing_data) - 1);
}

int read_disciplining_parameters(const char*path, struct disciplining_parameters *dsc_parameters)
{
    FILE *fp;
    int ret;

    if (!dsc_parameters) {
        return -EFAULT;
    }
    fp = fopen(path, "rb");
    if (!fp) {
        log_error("could not open file at %s", path);
    }

    ret = fread(dsc_parameters, sizeof(struct disciplining_parameters), 1, fp);
    if ( ret != 1)
        log_error("Could no read calibration parameters from file %s", path);
    else
        print_disciplining_parameters(dsc_parameters, LOG_DEBUG);
    fclose(fp);
    return ret;
}
