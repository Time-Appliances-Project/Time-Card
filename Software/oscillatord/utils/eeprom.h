/**
 * @file eeprom.h
 * @author Charles Parent (charles.parent@orolia2s.com)
 * @brief Header files for functions handling manufacturing data in ART card's EEPROM
 * Manufacturing data are located on RO part of the card, a jumper must be used to be
 * able to write this part
 * @version 0.1
 * @date 2022-07-08
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

#include <oscillator-disciplining/oscillator-disciplining.h>

#include "log.h"

enum PRODUCT_PRODUCTION_STATE {
    EVT,
    DVT,
    PVT,
    MP
};

/**
 * @brief ART card EEPROM format data
 */
struct __attribute__((__packed__)) eeprom_manufacturing_data {
    /** Magic word, fixed always "FBFB" */
    uint16_t magic; // 0x00
    /** Binary as unsigned integer, small endian */
    uint8_t format_version;// 0x02
    /** String */
    char product_name[20];//0x03
    /** XX-XXXXXX, dash not in the field */
    char product_part_number[8]; //0x17
    /** Will report to top level and consists of full system assembly */
    char system_assembly_part_number[12]; //0x1F
    char fb_pcba_part_number[12];// 0x2B
    char fb_pcb_part_number[12];// 0x37
    char od_pcba_part_number[13];// 0x43
    char od_pcba_serial_number[13];// 0x50
    /** 1=EVT, 2=DVT, 3=PVT, 4=MP */
    uint8_t product_production_state;// 0x5D
    /** Revision of the ART Card board */
    uint8_t product_version;// 0x5E
    /** Sub version of the revision */
    uint8_t product_sub_version;// 0x5F
    char product_serial_number[13];// 0x60
    char product_asset_tag[12];// 0x6D
    /** ODM name */
    char system_manufacturer[8];// 0x79
    /** mm-dd-yyyy, dash will not appear in field */
    uint16_t system_manufacturing_date_year;// 0x81
    uint8_t system_manufacturing_date_month;// 0x83
    uint8_t system_manufacturing_date_day;// 0x84
    char pcb_manufacturer[8];// 0x85
    char assembled_at[8];// 0x8D
    /** No MAC address on ART card, equals 00:00:00:00:00:00 */
    char local_mac_address[12];// 0x95
    /** No MAC address on ART card, equals 00:00:00:00:00:00 */
    char extended_mac_address_base[12];// 0xA1
    /** No MAC address, equal 0 */
    uint16_t extended_mac_address_size;// 0xAD
    char eeprom_location_on_fabric[20];// 0xAF
    /** Checksum CRC8 */
    uint8_t crc8;// 0xC3
};

static inline void print_eeprom_manufacturing_data(struct eeprom_manufacturing_data *data)
{
    log_debug("EEPROM data is:");
    log_debug("Magic: 0x%x", data->magic);
    log_debug("Format version: %d", data->format_version);
    log_debug("Product Name: %s", data->product_name);
    log_debug("Product PN: %s", data->product_part_number);
    log_debug("System assembly PN: %s", data->system_assembly_part_number);
    log_debug("FB PCBA PN: %s", data->fb_pcba_part_number);
    log_debug("FB PCB PN: %s", data->fb_pcb_part_number);
    log_debug("OD PCBA PN: %s", data->od_pcba_part_number);
    log_debug("OD PCVA SN: %s", data->od_pcba_serial_number);
    log_debug("Product Production state: %d", data->product_production_state);
    log_debug("Product version: %d", data->product_version);
    log_debug("Product subversion: %d", data->product_sub_version);
    log_debug("Product SN: %s", data->product_serial_number);
    log_debug("Product asset tag: %s", data->product_asset_tag);
    log_debug("System manufacturer: %s", data->system_manufacturer);
    log_debug("System manufacturer date: %u-%u-%u",
        data->system_manufacturing_date_day,
        data->system_manufacturing_date_month,
        data->system_manufacturing_date_year
    );
    log_debug("PCB Manufacturer: %s", data->pcb_manufacturer);
    log_debug("Assembled at: %s", data->assembled_at);
    log_debug("Local MAC address: %s", data->local_mac_address);
    log_debug("Extended MAC address: %s", data->extended_mac_address_base);
    log_debug("Extended MAC address size: %d", data->extended_mac_address_size);
    log_debug("EEPROM Location on fabric: %s", data->eeprom_location_on_fabric);
    log_debug("CRC8: 0x%x", data->crc8);
    return;
}

int write_eeprom_manufacturing_data(const char *path, struct eeprom_manufacturing_data *data);
int read_eeprom_manufacturing_data(const char *path, struct eeprom_manufacturing_data *data);
void init_manufacturing_eeprom_data(struct eeprom_manufacturing_data *data, char *serial_number);
int read_disciplining_parameters(const char*path, struct disciplining_parameters *dsc_parameters);
int init_eeprom_manufacturing_pcba(const char *path, struct eeprom_manufacturing_data *data);

#endif /* EEPROM_H */
