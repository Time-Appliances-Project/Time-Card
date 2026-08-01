/**
 * @file utils.h
 * @brief Utility function to cleanup structures and compute temperature
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef UTILS_H_
#define UTILS_H_

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include <dirent.h>

#define NS_IN_SECOND            1000000000l
#define DUMMY_TEMPERATURE_VALUE -3000.0

#ifndef container_of
# define container_of(ptr, type, member)                        \
    ({                                                          \
        const typeof(((type*)0)->member)* __mptr = (ptr);       \
        (type*)((uintptr_t)__mptr - offsetof(type, member));    \
    })
#endif /* ut_container_of */

void   file_cleanup(FILE** f);
void   string_cleanup(char** s);
void   fd_cleanup(int* fd);
// Formula to compute mRO50 temperature
double compute_temp(uint32_t reg);
void   find_dev_path(const char* dirname, struct dirent* dir, char* dev_path);
bool   find_file(char* path, char* name, char* file_path);
bool   parse_receiver_version(char* textToCheck, int* major, int* minor);

#endif /* UTILS_H_ */
