#ifndef TESTS_MRO50
#define TESTS_MRO50

#include <stdlib.h>

/** Minimum possible value of coarse control */
#define COARSE_RANGE_MIN            0
/** Maximum possible value of coarse control */
#define COARSE_RANGE_MAX            4194303
/** Minimum possible value of fine control */
#define FINE_RANGE_MIN              0
/** Maximum possible value of fine control */
#define FINE_RANGE_MAX              4800

#define CMD_READ_COARSE             "FD\r"
#define CMD_READ_FINE               "MON_tpcb PIL_cfield C\r"
#define CMD_READ_STATUS             "MONITOR1\r"

#define STATUS_ANSWER_SIZE          62
#define STATUS_EP_TEMPERATURE_INDEX 52
#define STATUS_CLOCK_LOCKED_INDEX   56
#define STATUS_CLOCK_LOCKED_BIT     2
#define STATUS_ANSWER_FIELD_SIZE    4

extern char         answer_str[66];
extern const size_t mro_answer_len;

int                 set_serial_attributes(int fd);
int                 mRo50_oscillator_cmd(int fd, const char* cmd, int cmd_len);

#endif /* TESTS_MRO50 */