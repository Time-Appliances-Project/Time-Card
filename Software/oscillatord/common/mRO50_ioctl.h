#ifndef MRO50_IOCTL_H
#define MRO50_IOCTL_H

#include <sys/ioctl.h>
#include <sys/types.h>

typedef u_int32_t u32;
typedef u_int8_t u8;

#define MRO50_READ_FINE		_IOR('M', 1, u32 *)
#define MRO50_READ_COARSE	_IOR('M', 2, u32 *)
#define MRO50_ADJUST_FINE	_IOW('M', 3, u32)
#define MRO50_ADJUST_COARSE	_IOW('M', 4, u32)
#define MRO50_READ_TEMP		_IOR('M', 5, u32 *)
#define MRO50_READ_CTRL		_IOR('M', 6, u32 *)
#define MRO50_SAVE_COARSE	_IO('M', 7)
#define MRO50_READ_EEPROM_BLOB _IOR('M', 8, u8*)
#define MRO50_WRITE_EEPROM_BLOB _IOW('M', 8, u8*)
#define MRO50_READ_EXTENDED_EEPROM_BLOB	_IOR('M', 9, u8 *)
#define MRO50_WRITE_EXTENDED_EEPROM_BLOB	_IOW('M', 9, u8 *)
#define MRO50_BOARD_CONFIG_READ _IOR('M', 10, u32 *)
#define MRO50_BOARD_CONFIG_WRITE _IOW('M', 10, u32 *)
#define MRO50_TEMP_FIELD_A_READ _IOR('M', 11, u32 *)
#define MRO50_TEMP_FIELD_B_READ _IOR('M', 12, u32 *)
#endif /* MRO50_IOCTL_H */
