#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <poll.h>
#include <termios.h>

#include "mRo50.h"

#include "log.h"

// From datasheet we assume answers cannot be larger than 60+2+2+2 characters
char         answer_str[66] = {0};
const size_t mro_answer_len = 66;

int          set_serial_attributes(int fd) {
             struct termios tty;
             int            err = tcgetattr(fd, &tty);
             if (err != 0) {
                 log_error("error from tcgetattr: %d\n", errno);
                 return -1;
    }

             cfsetospeed(&tty, B9600);
             cfsetispeed(&tty, B9600);

             tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
             // disable IGNBRK for mismatched speed tests; otherwise receive break
             // as \000 chars
             tty.c_iflag &= ~IGNBRK; // disable break processing
             tty.c_lflag = 0;        // no signaling chars, no echo,
                                     // no canonical processing
             tty.c_oflag = 0;        // no remapping, no delays

             tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

             tty.c_cflag |= (CLOCAL | CREAD);   // ignore modem controls,
                                                // enable reading
             tty.c_cflag &= ~(PARENB | PARODD); // shut off parity
             tty.c_cflag &= ~CSTOPB;
             tty.c_cflag &= ~CRTSCTS;

             if (tcsetattr(fd, TCSANOW, &tty) != 0) {
                 log_error("error from tcsetattr\n");
                 return -1;
    }

             return 0;
}

int mRo50_oscillator_cmd(int fd, const char* cmd, int cmd_len) {
    struct pollfd pfd = {};
    int           err, rbytes = 0;
    if (write(fd, cmd, cmd_len) != cmd_len) {
        log_error("mRo50_oscillator_cmd send command error: %d (%s)", errno, strerror(errno));
        return -1;
    }
    pfd.fd     = fd;
    pfd.events = POLLIN;
    while (1) {
        err = poll(&pfd, 1, 50);
        if (err == -1) {
            log_warn("mRo50_oscillator_cmd poll error: %d (%s)", errno, strerror(errno));
            memset(answer_str, 0, rbytes);
            return -1;
        }
        // poll call timed out - check the answer
        if (!err)
            break;
        err = read(fd, &answer_str[rbytes], mro_answer_len - rbytes);
        if (err < 0) {
            log_error("mRo50_oscillator_cmd rbyteserror: %d (%s)", errno, strerror(errno));
            memset(answer_str, 0, rbytes);
            return -1;
        }
        rbytes += err;
    }
    if (rbytes == 0) {
        log_error("mRo50_oscillator_cmd didn't get answer, zero length");
        return -1;
    }
    // Verify that first caracter of the answer is not equal to '?'
    if (answer_str[0] == '?') {
        // answer format doesn't fit protocol
        log_error("mRo50_oscillator_cmd answer protocol error: %s", answer_str);
        memset(answer_str, 0, rbytes);
        return -1;
    }
    if (answer_str[rbytes - 1] != '\n' || answer_str[rbytes - 2] != '\n') {
        log_error("mRo50_oscillator_cmd answer does not contain LFLF: %s", answer_str);
        memset(answer_str, 0, rbytes);
        return -1;
    }
    return rbytes;
}
