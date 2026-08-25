#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <pthread.h>

#include "config.h"
#include "oscillator_factory.h"

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            exit(EXIT_FAILURE);                                                             \
        }                                                                                   \
    } while (0)

struct command_response {
    const char* command;
    const char* response;
};

struct responder {
    int                            fd;
    const struct command_response* sequence;
    size_t                         count;
};

static void* respond_to_commands(void* argument) {
    struct responder* responder = argument;
    size_t            index;

    for (index = 0; index < responder->count; index++) {
        const struct command_response* expected     = &responder->sequence[index];
        char                           command[128] = {0};
        ssize_t                        length;

        do {
            length = read(responder->fd, command, sizeof(command) - 1);
        } while (length < 0 && errno == EINTR);
        CHECK(length > 0);
        while (length > 0 && command[length - 1] == '\0')
            length--;
        command[length] = '\0';
        CHECK(strcmp(command, expected->command) == 0);
        CHECK(write(responder->fd, expected->response, strlen(expected->response))
              == (ssize_t)strlen(expected->response));
    }

    return NULL;
}

static void test_pps_width(bool expect_update) {
    const struct command_response width_update_sequence[] = {
        {             "\\{swrev?}",   "[=V1.1,test]\r\n"},
        {              "{serial?}", "[=12345678901]\r\n"},
        {       "{get,PhaseLimit}",      "[=100000]\r\n"},
        {         "{get,PpsWidth}",    "[=40000000]\r\n"},
        {"{set,PpsWidth,80000000}",    "[=80000000]\r\n"},
        {       "{set,TauPps0,50}",          "[=50]\r\n"},
    };
    const struct command_response width_already_set_sequence[] = {
        {      "\\{swrev?}",   "[=V1.1,test]\r\n"},
        {       "{serial?}", "[=12345678901]\r\n"},
        {"{get,PhaseLimit}",      "[=100000]\r\n"},
        {  "{get,PpsWidth}",    "[=80000000]\r\n"},
        {"{set,TauPps0,50}",          "[=50]\r\n"},
    };
    const struct command_response* sequence  = expect_update ? width_update_sequence :
                                                               width_already_set_sequence;
    struct responder               responder = {
                      .sequence = sequence,
                      .count    = expect_update ?
                                      sizeof(width_update_sequence) / sizeof(width_update_sequence[0]) :
                                      sizeof(width_already_set_sequence) / sizeof(width_already_set_sequence[0]),
    };
    struct devices_path devices = {0};
    struct config       config  = {0};
    struct oscillator*  oscillator;
    char                slave_path[128];
    pthread_t           thread;
    int                 master_fd, slave_fd;

    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master_fd >= 0);
    CHECK(grantpt(master_fd) == 0);
    CHECK(unlockpt(master_fd) == 0);
    CHECK(ptsname_r(master_fd, slave_path, sizeof(slave_path)) == 0);
    slave_fd = open(slave_path, O_RDWR | O_NOCTTY);
    CHECK(slave_fd >= 0);

    responder.fd = master_fd;
    CHECK(pthread_create(&thread, NULL, respond_to_commands, &responder) == 0);
    CHECK(config_set(&config, "oscillator", "sa5x") == 0);
    CHECK(snprintf(devices.mac_path, sizeof(devices.mac_path), "%s", slave_path) > 0);

    oscillator = oscillator_factory_new(&config, &devices);
    CHECK(oscillator != NULL);
    oscillator_factory_destroy(&oscillator);
    CHECK(pthread_join(thread, NULL) == 0);

    config_cleanup(&config);
    close(slave_fd);
    close(master_fd);
}

int main(void) {
    test_pps_width(true);
    test_pps_width(false);
    return 0;
}
