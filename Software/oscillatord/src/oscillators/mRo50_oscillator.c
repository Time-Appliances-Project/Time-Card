#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/limits.h>
#include <math.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <error.h>
#include <termios.h>
#include <poll.h>

#include "config.h"
#include "log.h"
#include "mRO50_ioctl.h"
#include "utils.h"

#include "../oscillator.h"
#include "../oscillator_factory.h"

#define FACTORY_NAME "mRO50"
#define MRO50_CMD_READ_TEMP 0x3e
#define MRO50_CMD_GET_DAC 0x41
#define MRO50_CMD_PROD_ID 0x50
#define MRO50_CMD_READ_FW_REV 0x51
#define MRO50_CMD_SET_DAC 0xA0
#define MRO50_SETPOINT_MIN 0
#define MRO50_SETPOINT_MAX 1000000
#define READ_MAX_TRY 400

#define CMD_WRITE_COARSE "FD %08X\r"
#define CMD_WRITE_FINE   "MON_tpcb PIL_cfield C %04X\r"
#define CMD_READ_COARSE "FD\r"
#define CMD_READ_FINE   "MON_tpcb PIL_cfield C\r"
#define CMD_READ_STATUS "MONITOR1\r"
#define CMD_READ_TEMP_PARAM_A "MON_tpcb PIL_cfield A\r"
#define CMD_READ_TEMP_PARAM_B "MON_tpcb PIL_cfield B\r"
#define CMD_RESET "reset\r"

#define STATUS_ANSWER_SIZE 62
#define STATUS_EP_TEMPERATURE_INDEX 52
#define STATUS_CLOCK_LOCKED_INDEX 56
#define STATUS_CLOCK_LOCKED_BIT 2
#define STATUS_ANSWER_FIELD_SIZE 4
#define MRO50_BRIDGE_LOCKED (1U << 1)

#define RESET_TIMEOUT 300

typedef u_int32_t uint32_t;
typedef u_int32_t u32;

struct mRo50_oscillator {
	struct oscillator oscillator;
	char serial_path[PATH_MAX];
	int serial_fd;
	int bridge_fd;
	bool direct_bridge;
};

struct mRo50_attributes {
	double EP_temperature;	// Main electronique board temperature (DegC)
	uint8_t locked:1;		//Locked
};

// From datasheet we assume answers cannot be larger than 128 characters
static char answer_str[129] = {0};
const size_t mro_answer_len = 128;

static unsigned int mRo50_oscillator_index;


static void mRo50_oscillator_destroy(struct oscillator **oscillator)
{
	struct oscillator *o;
	struct mRo50_oscillator *r;

	if (oscillator == NULL || *oscillator == NULL)
		return;

	o = *oscillator;
	r = container_of(o, struct mRo50_oscillator, oscillator);
	if (r->serial_fd >= 0) {
		close(r->serial_fd);
		log_info("Closed oscillator's serial port");
	}
	if (r->bridge_fd >= 0) {
		close(r->bridge_fd);
		log_info("Closed oscillator's direct driver bridge");
	}
	memset(o, 0, sizeof(*o));
	free(o);
	*oscillator = NULL;
}

static int set_serial_attributes(int fd)
{
	struct termios tty;
	int err = tcgetattr(fd, &tty);
	if (err != 0){
		log_error("error from tcgetattr: %d\n", errno);
		return -1;
	}

	cfsetospeed(&tty, B9600);
	cfsetispeed(&tty, B9600);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty.c_iflag &= ~IGNBRK;			// disable break processing
	tty.c_lflag = 0;			// no signaling chars, no echo,
						// no canonical processing
	tty.c_oflag = 0;			// no remapping, no delays

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
	tty.c_iflag &= ~(INLCR | IGNCR); // no CR/LF translation on input
	tty.c_iflag |= ICRNL;

	tty.c_cflag |= (CLOCAL | CREAD);	// ignore modem controls,
						// enable reading
	tty.c_cflag &= ~(PARENB | PARODD);	// shut off parity
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr(fd, TCSANOW, &tty) != 0){
		log_error("error from tcsetattr\n");
		return -1;
	}

	return 0;
}

static int mRo50_oscillator_cmd(struct mRo50_oscillator *mRo50, const char *cmd, int cmd_len)
{
	struct pollfd pfd = {};
	int err, rbytes = 0;
	if (write(mRo50->serial_fd, cmd, cmd_len) != cmd_len) {
		log_error("mRo50_oscillator_cmd send command error: %d (%s)", errno, strerror(errno));
		return -1;
	}
	pfd.fd = mRo50->serial_fd;
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
		err = read(mRo50->serial_fd, answer_str + rbytes, mro_answer_len - rbytes);
		if (err < 0) {
			log_error("mRo50_oscillator_cmd rbyteserror: %d (%s)", errno, strerror(errno));
			memset(answer_str, 0, rbytes);
			return -1;
		}
		rbytes += err;
	}
	if (rbytes == 0) {
		log_warn("mRo50_oscillator_cmd didn't get answer, zero length");
		return -1;
	}
	// Verify that first caracter of the answer is not equal to '?'
	if (answer_str[0] == '?') {
		// answer format doesn't fit protocol
		log_warn("mRo50_oscillator_cmd answer protocol error: %s", answer_str);
		memset(answer_str, 0, rbytes);
		return -1;
	}
	if (rbytes < 2 || answer_str[rbytes -1] != '\n' || answer_str[rbytes - 2] != '\n') {
		log_warn("mRo50_oscillator_cmd answer does not contain LFLF: %s", answer_str);
		memset(answer_str, 0, rbytes);
		return -1;
	}
	return rbytes;
}

static int mRo50_clean_serial(struct mRo50_oscillator *mRo50)
{
	int serial_fd;

	close(mRo50->serial_fd);

	log_info("Resetting mRo50 serial");
	serial_fd = open(mRo50->serial_path, O_RDWR|O_NONBLOCK);
	if (serial_fd < 0) {
		log_error("Could not reopen mRo50 device\n");
		return -1;
	}
	mRo50->serial_fd = serial_fd;
	if (set_serial_attributes(serial_fd) != 0)
		return -1;

	mRo50_oscillator_cmd(mRo50, "\r\n", strlen("\r\n"));
	memset(answer_str, 0, mro_answer_len);
	log_info("mRo50 serial reset");
	return 0;
}

static bool mRo50_reset(struct mRo50_oscillator *mRo50)
{
	time_t start_reset, current_time;
	struct pollfd pfd = {};
	size_t rbytes = 0;
	bool mRo_reset = false;
	int err;

	log_info("Resetting mRO50...");
	time(&start_reset);
	if (write(mRo50->serial_fd, CMD_RESET, strlen(CMD_RESET)) != strlen(CMD_RESET)) {
		log_error("mRo50_oscillator_cmd send command error: %d (%s)", errno, strerror(errno));
		return false;
	}
	pfd.fd = mRo50->serial_fd;
	pfd.events = POLLIN;

	while (1) {
		/* Exit if reset takes longer than */
		time(&current_time);
		if (difftime(current_time, start_reset) >= (double) RESET_TIMEOUT) {
			log_error("Reset Timeout !");
			break;
		}

		err = poll(&pfd, 1, 50);
		if (err == -1) {
			log_warn("mRo50_oscillator_cmd poll error: %d (%s)", errno, strerror(errno));
			continue;
		}
		if (!err) // poll timeout
			continue;
		err = read(mRo50->serial_fd, answer_str + rbytes, mro_answer_len - rbytes);
		if (err < 0) {
			log_error("mRo50_oscillator_cmd rbyteserror: %d (%s)", errno, strerror(errno));
			continue;
		}
		rbytes += err;
		if (rbytes < 1)
			continue;
		answer_str[rbytes] = 0;
		if (strstr(answer_str, "Start done>") != NULL) {
			answer_str[rbytes - 1] = '\0';
			log_debug("%s", answer_str);
			log_info("mRO successfully reset !");
			mRo_reset = true;
			break;
		}
		if (answer_str[rbytes - 1] == '\n') {
			if (rbytes > 1) {
				answer_str[rbytes - 1] = '\0';
				log_debug("%s", answer_str);
				if (answer_str[0] == '?') {
					log_warn("Reset command not understood by mRO50, retrying...");
					if (write(mRo50->serial_fd, CMD_RESET, strlen(CMD_RESET)) != strlen(CMD_RESET)) {
						log_error("mRo50_oscillator_cmd send command error: %d (%s)", errno, strerror(errno));
						return false;
					}
				}
			}
			rbytes = 0;
		}
		if (rbytes == mro_answer_len) {
			log_error("Buffer full !");
			rbytes = 0;
		}
	}
	return mRo_reset;
}

static void read_temperature_compensation_parameters(struct mRo50_oscillator *mRo50)
{
	int ret, res;
	uint32_t a,b;

	log_info("Reading A & B parameters");
	ret = mRo50_oscillator_cmd(mRo50, CMD_READ_TEMP_PARAM_A, sizeof(CMD_READ_TEMP_PARAM_A) - 1);
	if (ret > 0) {
		res = sscanf(answer_str, "%x\r\n", &a);
		memset(answer_str, 0, ret);
		if (res > 0) {

		} else {
			log_error("Could not read temperature compensation parameter A");
			return;
		}
	} else {
		log_error("Fail reading temperature compensation parameter A, err %d, errno %d", ret, errno);
		mRo50_clean_serial(mRo50);
		if (ret != 0) {
			log_error("Could not reset mRo50 serial");
		}
		return;
	}

	ret = mRo50_oscillator_cmd(mRo50, CMD_READ_TEMP_PARAM_B, sizeof(CMD_READ_TEMP_PARAM_B) - 1);
	if (ret > 0) {
		res = sscanf(answer_str, "%x\r\n", &b);
		memset(answer_str, 0, ret);
		if (res > 0) {

		} else {
			log_error("Could not read temperature compensation parameter B");
			return;
		}
	} else {
		log_error("Fail reading temperature compensation parameter B, err %d, errno %d", ret, errno);
		mRo50_clean_serial(mRo50);
		if (ret != 0) {
			log_error("Could not reset mRo50 serial");
		}
		return;
	}
	log_info("Internal temperature compensation: A = %f, B = %f", *((float*)&a), *((float*)&b));
}

static struct oscillator *mRo50_oscillator_new(struct devices_path *devices_path)
{
	struct mRo50_oscillator *mRo50;
	int fd;
	int serial_fd;
	struct oscillator *oscillator;

	mRo50 = calloc(1, sizeof(*mRo50));
	if (mRo50 == NULL)
		return NULL;
	mRo50->serial_fd = -1;
	mRo50->bridge_fd = -1;
	oscillator = &mRo50->oscillator;

	if (devices_path->mro_path[0] != '\0') {
		fd = open(devices_path->mro_path, O_RDWR);
		if (fd >= 0) {
			mRo50->bridge_fd = fd;
			mRo50->direct_bridge = true;
			log_info("Using mRO50 direct driver bridge %s",
				devices_path->mro_path);
			goto initialized;
		}
		log_warn("Could not open mRO50 direct bridge %s: %s; trying serial",
			devices_path->mro_path, strerror(errno));
	}
	snprintf(mRo50->serial_path, sizeof(mRo50->serial_path), "%s",
		devices_path->mac_path);

	serial_fd = open(mRo50->serial_path, O_RDWR|O_NONBLOCK);
	if (serial_fd < 0) {
		log_error("Could not open mRo50 device: %s\n", mRo50->serial_path);
		goto error;
	}
	mRo50->serial_fd = serial_fd;
	if (set_serial_attributes(serial_fd) != 0)
		goto error_openedfd;

	/* Reset mRo50 */
	if (!mRo50_reset(mRo50))
		goto error_openedfd;
	read_temperature_compensation_parameters(mRo50);

initialized:
	oscillator_factory_init(FACTORY_NAME, oscillator, FACTORY_NAME "-%d",
			mRo50_oscillator_index);
	mRo50_oscillator_index++;
	log_debug("instantiated " FACTORY_NAME " oscillator");
	return oscillator;
error_openedfd:
	close(serial_fd);
	mRo50->serial_fd = -1;
error:
	mRo50_oscillator_destroy(&oscillator);
	return NULL;
}

static int mRo50_oscillatord_get_attributes(struct oscillator *oscillator, struct mRo50_attributes *a)
{
	struct mRo50_oscillator *mRo50;
	mRo50 = container_of(oscillator, struct mRo50_oscillator, oscillator);
	if (mRo50->direct_bridge) {
		uint32_t temperature;
		uint32_t control;
		if (ioctl(mRo50->bridge_fd, MRO50_READ_TEMP, &temperature) != 0 ||
			ioctl(mRo50->bridge_fd, MRO50_READ_CTRL, &control) != 0) {
			log_error("Could not read mRO50 direct-bridge attributes: %s",
				strerror(errno));
			return -1;
		}
		a->EP_temperature = compute_temp(temperature);
		if (a->EP_temperature == DUMMY_TEMPERATURE_VALUE)
			return -1;
		a->locked = (control & MRO50_BRIDGE_LOCKED) != 0;
		return 0;
	}
	char EP_temperature[4];
	uint32_t read_value;
	int err;

	err = mRo50_oscillator_cmd(mRo50, CMD_READ_STATUS, sizeof(CMD_READ_STATUS) - 1);
	if (err == STATUS_ANSWER_SIZE) {
		answer_str[err - 2] = '\0';
		log_debug("MONITOR1 from mro50 gives %s", answer_str);
		/* Parse mRo50 EP temperature */
		strncpy(EP_temperature, &answer_str[STATUS_EP_TEMPERATURE_INDEX], STATUS_ANSWER_FIELD_SIZE);
		read_value = strtoul(EP_temperature, NULL, 16);
		double temperature = compute_temp(read_value);
		if (temperature == DUMMY_TEMPERATURE_VALUE)
			return -1;
		a->EP_temperature = temperature;

		/* Parse mRO50 clock lock flag */
		uint8_t lock = answer_str[STATUS_CLOCK_LOCKED_INDEX] & (1 << STATUS_CLOCK_LOCKED_BIT);
		a->locked = lock >> STATUS_CLOCK_LOCKED_BIT;
		memset(answer_str, 0, STATUS_ANSWER_SIZE);
	} else {
		log_warn("Fail reading attributes, err %d, errno %d", err, errno);
		err = mRo50_clean_serial(mRo50);
		if (err != 0) {
			log_error("Could not reset mRo50 serial");
		}
		return -1;
	}

	return 0;
}

static int mRo50_oscillator_get_ctrl(struct oscillator *oscillator, struct oscillator_ctrl *ctrl)
{
	struct mRo50_oscillator *mRo50;
	uint32_t coarse, fine;
	int ret, res;

	mRo50 = container_of(oscillator, struct mRo50_oscillator, oscillator);
	if (mRo50->direct_bridge) {
		if (ioctl(mRo50->bridge_fd, MRO50_READ_COARSE,
				&ctrl->coarse_ctrl) != 0 ||
			ioctl(mRo50->bridge_fd, MRO50_READ_FINE,
				&ctrl->fine_ctrl) != 0) {
			log_error("Could not read mRO50 direct-bridge controls: %s",
				strerror(errno));
			return -1;
		}
		return 0;
	}

	ret = mRo50_oscillator_cmd(mRo50, CMD_READ_COARSE, sizeof(CMD_READ_COARSE) - 1);
	if (ret > 0) {
		res = sscanf(answer_str, "%x\r\n", &coarse);
		memset(answer_str, 0, ret);
		if (res > 0) {
			ctrl->coarse_ctrl = coarse;
		} else {
			log_error("Could not parse coarse parameter");
			return -1;
		}
	} else {
		log_error("Fail reading Coarse Parameters, err %d, errno %d", ret, errno);
		mRo50_clean_serial(mRo50);
		if (ret != 0) {
			log_error("Could not reset mRo50 serial");
		}
		return -1;
	}


	ret = mRo50_oscillator_cmd(mRo50, CMD_READ_FINE, sizeof(CMD_READ_FINE) - 1);
	if (ret > 0) {
		res = sscanf(answer_str, "%x\r\n", &fine);
		memset(answer_str, 0, ret);
		if (res > 0) {
			ctrl->fine_ctrl = fine;
		} else {
			log_error("Could not parse fine parameter");
			return -1;
		}
	} else {
		log_error("Fail reading Fine Parameters, err %d, errno %d", ret, errno);
		mRo50_clean_serial(mRo50);
		if (ret != 0) {
			log_error("Could not reset mRo50 serial");
		}
		return -1;
	}


	return 0;
}

static int mRO50_oscillator_parse_attributes(struct oscillator *oscillator, struct oscillator_attributes *attributes)
{
	struct mRo50_attributes mRo50_attributes;
	int ret;
	ret = mRo50_oscillatord_get_attributes(oscillator, &mRo50_attributes);
	if (ret != 0) {
		return -1;
	}
	attributes->temperature = mRo50_attributes.EP_temperature;
	attributes->locked = mRo50_attributes.locked;
	return 0;
}

static int mRo50_oscillator_apply_output(struct oscillator *oscillator, struct od_output *output)
{
	struct mRo50_oscillator *mRo50;
	char command[128];
	int ret;

	memset(command, '\0', 128);
	mRo50 = container_of(oscillator, struct mRo50_oscillator, oscillator);
	if (mRo50->direct_bridge) {
		unsigned long command_code;
		if (output->action == ADJUST_FINE)
			command_code = MRO50_ADJUST_FINE;
		else if (output->action == ADJUST_COARSE)
			command_code = MRO50_ADJUST_COARSE;
		else
			return -EINVAL;
		if (ioctl(mRo50->bridge_fd, command_code, &output->setpoint) != 0) {
			log_error("Could not write mRO50 direct-bridge control: %s",
				strerror(errno));
			return -1;
		}
		return 0;
	}

	if (output->action == ADJUST_FINE) {
		log_trace("mRo50_oscillator_apply_output: Fine adjustment to value %lu requested", output->setpoint);
		sprintf(command, CMD_WRITE_FINE, output->setpoint);
	} else if (output->action == ADJUST_COARSE) {
		log_trace("mRo50_oscillator_apply_output: Coarse adjustment to value %lu requested", output->setpoint);
		sprintf(command, CMD_WRITE_COARSE, output->setpoint);
	} else {
		log_error("Calling mRo50_oscillator_apply_output with action different from ADJUST_COARSE or ADJUST_FINE");
		log_error("Action is %d", output->action);
		return 0;
	}
	ret = mRo50_oscillator_cmd(mRo50, command, strlen(command));
	if (ret != 2) {
		log_error("Could not prepare command request to adjust fine frequency, error %d, errno %d", ret, errno);
		return -1;
	}
	memset(answer_str, 0, mro_answer_len);
	return 0;
}

static struct calibration_results * mRo50_oscillator_calibrate(struct oscillator *oscillator,
		struct phasemeter *phasemeter, struct gnss *gnss, struct calibration_parameters *calib_params,
		int phase_sign)
{
	struct od_output adj_fine = { .action = ADJUST_FINE, .setpoint = 0, };
	int ret;
	int64_t phase_error;

	struct calibration_results *results = malloc(sizeof(struct calibration_results));
	if (results == NULL) {
		log_error("Could not allocate memory to create calibration_results");
		return NULL;
	}

	results->length = calib_params->length;
	results->nb_calibration = calib_params->nb_calibration;
	results->measures = malloc(results->length * results->nb_calibration * sizeof(struct timespec));
	if (results->measures == NULL) {
		log_error("Could not allocate memory to create calibration measures");
		free(results);
		results = NULL;
		return NULL;
	}
	log_info("Starting measure for calibration");
	for (int i = 0; i < results->length; i ++) {
		if (!loop)
			goto clean_calibration;
		adj_fine.setpoint = (uint32_t) calib_params->ctrl_points[i];
		log_info("Applying fine adjustment of %d", adj_fine.setpoint);
		ret = mRo50_oscillator_apply_output(oscillator, &adj_fine);
		if (ret < 0) {
			free(results->measures);
			log_error("Could not write to mRO50");
			results->measures = NULL;
			free(results);
			results = NULL;
			return NULL;
		}
		sleep(SETTLING_TIME);

		struct oscillator_ctrl ctrl;
		ret = mRo50_oscillator_get_ctrl(oscillator, &ctrl);
		if (ctrl.fine_ctrl != adj_fine.setpoint) {
			log_info("ctrl measured is %d and ctrl point is %d", ctrl.fine_ctrl, adj_fine.setpoint);
			log_error("CTRL POINTS HAS NOT BEEN SET !");
		}

		log_info("Starting phase error measures %d/%d", i+1, results->length);
		for (int j = 0; j < results->nb_calibration; j++) {
			if (!loop)
				goto clean_calibration;
			int phasemeter_status = get_phase_error(phasemeter, &phase_error);
			if (phasemeter_status != PHASEMETER_BOTH_TIMESTAMPS) {
				log_error("Could not get phase error during calibration, aborting");
				free(results->measures);
				results->measures = NULL;
				free(results);
				results = NULL;
				return NULL;
			}
			/* Get qErr in ps*/
			int32_t qErr;
			if (gnss_get_epoch_data(gnss, NULL, NULL, &qErr) != 0) {
				log_error("Could not get gnss data");
				free(results->measures);
				results->measures = NULL;
				free(results);
				results = NULL;
				return NULL;
			}

			*(results->measures + i * results->nb_calibration + j) = phase_error + (float) qErr / 1000;
			log_debug("ctrl_point %d measure[%d]: phase error = %lld, qErr = %d, result = %f",
				adj_fine.setpoint, j, phase_error, qErr, phase_error + (float) qErr / 1000);
			sleep(1);
		}
	}

	return results;

clean_calibration:
	free(results->measures);
	results->measures = NULL;
	free(results);
	results = NULL;
	return NULL;
}

static const struct oscillator_factory mRo50_oscillator_factory = {
	.class = {
			.name = FACTORY_NAME,
			.get_ctrl = mRo50_oscillator_get_ctrl,
			.save = NULL,
			.parse_attributes = mRO50_oscillator_parse_attributes,
			.apply_output = mRo50_oscillator_apply_output,
			.calibrate = mRo50_oscillator_calibrate,
			.dac_min = MRO50_SETPOINT_MIN,
			.dac_max = MRO50_SETPOINT_MAX,
	},
	.new = mRo50_oscillator_new,
	.destroy = mRo50_oscillator_destroy,
};

static void __attribute__((constructor)) mRo50_oscillator_constructor(void)
{
	int ret;

	ret = oscillator_factory_register(&mRo50_oscillator_factory);
	if (ret < 0)
		log_error("oscillator_factory_register", ret);
}
