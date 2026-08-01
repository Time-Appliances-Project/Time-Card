#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>

#include "config.h"
#include "log.h"
#include "utils.h"

#include "../oscillator.h"
#include "../oscillator_factory.h"

#define FACTORY_NAME "sa3x"

struct sa3x_attributes {
	char bite;           // BITE
	char version[5];     // Version
	char serial[12];     // SerialNumber
	int  teccontrol;     // TEC Control (mDegC)
	int  rfcontrol;      // RF Control (0.1mv)
	int  ddscurrent;     // DDS Frequency Center Current (0.01Hz)
	int  cellcurrent;    // CellHeaterCurrent (ma)
	int  dcsignal;       // DCSignal (mv)
	int  temperature;    // Temperature (mDegC)
	int  digitaltuning;  // Digital Tuning (0.01Hz)
	char analogtuningon; // Analog Tuning On/Off
	int  analogtuning;   // Analog Tuning (mv)
};

struct sa3x_oscillator {
	struct oscillator oscillator;
	int osc_fd;
	struct sa3x_attributes attributes;
	struct timespec attr_time;
};

static unsigned int sa3x_oscillator_index;

// we need only seconds to account difference
static inline time_t sa3x_timediff(const struct timespec ts0, const struct timespec ts1)
{
	return ts0.tv_sec > ts1.tv_sec ? (ts0.tv_sec - ts1.tv_sec) : (ts1.tv_sec - ts0.tv_sec);
}

static void sa3x_oscillator_destroy(struct oscillator **oscillator)
{
	struct oscillator *o;
	struct sa3x_oscillator *r;

	if (oscillator == NULL || *oscillator == NULL)
		return;

	o = *oscillator;
	r = container_of(o, struct sa3x_oscillator, oscillator);
	if (r->osc_fd != -1) {
		close(r->osc_fd);
		log_info("Closed oscillator's serial port");
	}
	memset(o, 0, sizeof(*o));
	free(o);
	*oscillator = NULL;
}

static int set_serial_attributes(int fd)
{
	struct termios tty;

	if (tcgetattr(fd, &tty) != 0){
		log_error("error from tcgetattr\n");
		return -1;
	}

	cfsetospeed(&tty, B57600);
	cfsetispeed(&tty, B57600);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty.c_iflag &= ~IGNBRK;         // disable break processing
	tty.c_lflag = 0;                // no signaling chars, no echo,
	                                // no canonical processing
	tty.c_oflag = 0;                // no remapping, no delays

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_cflag |= (CLOCAL | CREAD);   // ignore modem controls,
	                                   // enable reading
	tty.c_cflag &= ~(PARENB | PARODD); // shut off parity
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr(fd, TCSANOW, &tty) != 0){
		log_error("error from tcsetattr\n");
		return -1;
	}

	return 0;
}

static struct oscillator *sa3x_oscillator_new(struct devices_path *devices_path)
{
	struct sa3x_oscillator *sa3x;
	int fd;
	struct oscillator *oscillator;

	sa3x = calloc(1, sizeof(*sa3x));
	if (sa3x == NULL)
		return NULL;
	oscillator = &sa3x->oscillator;
	sa3x->osc_fd = -1;
	// calloc sets memory to 0, we don't need to init structures

	fd = open(devices_path->mac_path, O_RDWR|O_NONBLOCK);
	if (fd == -1) {
		log_error("Could not open sa3x device\n");
		goto error;
	}

	sa3x->osc_fd = fd;
	if (set_serial_attributes(fd) != 0)
		goto error;

	oscillator_factory_init(FACTORY_NAME, oscillator, FACTORY_NAME "-%d",
			sa3x_oscillator_index);
	sa3x_oscillator_index++;

	log_debug("instantiated " FACTORY_NAME " oscillator");

	return oscillator;
error:
	if (sa3x->osc_fd != -1)
		close(sa3x->osc_fd);
	sa3x_oscillator_destroy(&oscillator);
	return NULL;
}

static struct sa3x_attributes *sa3x_oscillator_get_attributes(struct oscillator *oscillator)
{
	struct sa3x_oscillator *sa3x;
	sa3x = container_of(oscillator, struct sa3x_oscillator, oscillator);
	struct sa3x_attributes *a = &sa3x->attributes;
	struct pollfd pfd = {};
	struct timespec ts;
	int err;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	if (sa3x_timediff(ts, sa3x->attr_time) < SETTLING_TIME) {
		return a;
	}

	char *command = "^";
	if (write(sa3x->osc_fd, command, 1) != 1) {
		log_error("oscillator_get_attributes send command error: %d (%s)", errno, strerror(errno));
		return NULL;
	}

	pfd.fd = sa3x->osc_fd;
	pfd.events = POLLIN;
	// give MAC 100ms to respond with telemetry
	err = poll(&pfd, 1, 100);
	if (!err) {
		// poll call timed out
		log_error("oscillator_get_attributes timed out");
		return NULL;

	}
	if (err == -1) {
		log_error("oscillator_get_attributes poll error: %d (%s)", errno, strerror(errno));
		return NULL;
	}
	// Datasheet states that answer will be no more than 128 characters
	char line[128] = {0};
	size_t n = 128;

	if ((err = read(sa3x->osc_fd, line, n)) > 0) {

		err = sscanf(line, "%c,%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%c,%d",
			&a->bite, a->version, a->serial, &a->teccontrol, &a->rfcontrol, &a->ddscurrent,
			&a->cellcurrent, &a->dcsignal, &a->temperature, &a->digitaltuning, &a->analogtuningon,
			&a->analogtuning);
		if (err < 9) {
			log_error("oscillator_get_attributes parse telemetry error: only %d attributes read", err);
			return NULL;
		}
	} else {
		log_error("oscillator_get_attributes read telemetry error: %d (%s)", errno, strerror(errno));
		return NULL;
	}
	clock_gettime(CLOCK_MONOTONIC, &sa3x->attr_time);
	return a;
}

static int sa3x_oscillator_get_ctrl(struct oscillator *oscillator, struct oscillator_ctrl *ctrl)
{
	// Used for mRO50
	ctrl->fine_ctrl = 0;
	ctrl->coarse_ctrl = 0;
	return 0;
}

static int sa3x_oscillator_parse_attributes(struct oscillator *oscillator, struct oscillator_attributes *attributes)
{
	struct sa3x_attributes *a = sa3x_oscillator_get_attributes(oscillator);

	if (a) {
		if (a->bite == 0) {
			attributes->locked = 1;
		} else {
			attributes->locked = 0;
		}
		// mDegC to DegC
		attributes->temperature = a->temperature / 1000.0;
	} else {
		attributes->temperature = -400.0;
		attributes->locked = 0;
	}
	// we cannot propagate error further
	return 0;
}

static const struct oscillator_factory sa3x_oscillator_factory = {
	.class = {
			.name = FACTORY_NAME,
			.get_ctrl = sa3x_oscillator_get_ctrl,
			.parse_attributes = sa3x_oscillator_parse_attributes,
	},
	.new = sa3x_oscillator_new,
	.destroy = sa3x_oscillator_destroy,
};

static void __attribute__((constructor)) sa3x_oscillator_constructor(void)
{
	int ret;

	ret = oscillator_factory_register(&sa3x_oscillator_factory);
	if (ret < 0)
		log_error("oscillator_factory_register", ret);
}
