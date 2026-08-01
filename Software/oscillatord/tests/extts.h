#ifndef EXTTS
#define EXTTS

#include <stdlib.h>

enum {
    EXTTS_INDEX_TS_GNSS,
    EXTTS_INDEX_TS_1,
    EXTTS_INDEX_TS_2,
    EXTTS_INDEX_TS_3,
    EXTTS_INDEX_TS_4,
    EXTTS_INDEX_TS_INTERNAL,
    NUM_EXTTS
};

int enable_extts(int fd, unsigned int extts_index);
int disable_extts(int fd, unsigned int extts_index);
int read_extts(int fd, int64_t* nsec);

#endif /* EXTTS */