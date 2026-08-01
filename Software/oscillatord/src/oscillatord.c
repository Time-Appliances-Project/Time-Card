/**
 * @file oscillatord.c
 * @brief Main file of the program
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2022
 *
 * Oscillatord aims at disciplining an oscillator to an external reference.
 * It is responsible for fetching oscillator and reference data and pass them
 * to a disciplining algorithm, and apply the decision of the algorithm regarding the oscillator.
 */
#include <sys/select.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <math.h>

#include <error.h>

#include <oscillator-disciplining/oscillator-disciplining.h>
#include <linux/ptp_clock.h>

#include "config.h"
#include "eeprom_config.h"
#include "gnss.h"
#include "log.h"
#include "monitoring.h"
#include "ntpshm/ntpshm.h"
#include "ntpshm/ppsthread.h"
#include "oscillator.h"
#include "oscillator_factory.h"
#include "phasemeter.h"
#include "utils.h"

#define UPDATE_DISCIPLINING_PARAMETERS_SEC 3600

static struct gps_context_t context;
struct od *od = NULL;
struct oscillator *oscillator = NULL;
struct devices_path devices_path = { 0 };
pthread_t save_dsc_params_thread;
static bool save_dsc_params_thread_active;

/**
 * @brief Signal Handler to kill program gracefully
 *
 * @param signum
 * @return * Signal
 */
static void signal_handler(int signum)
{
	log_info("Caught signal %s.", strsignal(signum));
	if (!loop) {
		log_error("Signalled twice, brutal exit.");
		exit(EXIT_FAILURE);
	}
	loop = false;
}

static void save_disciplining_parameters(struct od *od) {
	log_info("Saving disciplining parameters in EEPROM");
	struct disciplining_parameters dsc_params;
	int ret = od_get_disciplining_parameters(od, &dsc_params);
	if (ret != 0) {
		log_error("Could not get discipling parameters from disciplining algorithm");
	} else {
		ret = write_disciplining_parameters_in_eeprom(
			devices_path.disciplining_config_path,
			devices_path.temperature_table_path,
			&dsc_params
		);
		if (ret < 0)
			log_error("Error updating disciplining parameters !");
		else {
			log_info("Saved calibration parameters into EEPROM");
		}
	}
}

static void * save_disciplining_parameters_thread(void *p_data) {
	struct od *od = (struct od*) p_data;
	save_disciplining_parameters(od);
	return NULL;
}

static int schedule_disciplining_parameters_save(struct od *od)
{
	int ret;

	if (od == NULL)
		return -EINVAL;
	if (save_dsc_params_thread_active) {
		ret = pthread_join(save_dsc_params_thread, NULL);
		if (ret != 0)
			return -ret;
		save_dsc_params_thread_active = false;
	}
	ret = pthread_create(&save_dsc_params_thread, NULL,
		save_disciplining_parameters_thread, od);
	if (ret == 0)
		save_dsc_params_thread_active = true;
	return -ret;
}

static void finish_disciplining_parameters_save(void)
{
	if (!save_dsc_params_thread_active)
		return;
	if (pthread_join(save_dsc_params_thread, NULL) != 0)
		log_warn("Could not join the EEPROM save thread");
	save_dsc_params_thread_active = false;
}

/**
 * @brief Phase jump: Apply a phase offset to the PHC
 *
 * @param fd_clock handler of PHC
 * @param device_name device name for logs
 * @param phase_error phase offset to apply
 * @return int 0 on success
 */
static int apply_phase_offset(int fd_clock, const char *device_name,
	int64_t phase_error)
{
	int ret = 0;
	clockid_t clkid;
	clkid = FD_TO_CLOCKID(fd_clock);

	struct timex timex = {
		.modes = ADJ_SETOFFSET | ADJ_NANO,
		.offset = 0,
		.time.tv_sec = phase_error > 0 || (phase_error % NS_IN_SECOND == 0.0) ?
			(long long) floor(phase_error / NS_IN_SECOND):
			(long long) floor(phase_error / NS_IN_SECOND) - 1,
		.time.tv_usec = phase_error > 0 || (phase_error % NS_IN_SECOND == 0.0) ?
			phase_error % NS_IN_SECOND:
			phase_error % NS_IN_SECOND + NS_IN_SECOND,
	};

	log_info("%s: applying phase offset correction of %"PRIi64"ns",
		device_name, phase_error);
	ret = clock_adjtime(clkid, &timex);
	return ret;
}

/**
 * @brief Enable/disable PPS output of PHC
 *
 * @param fd handler of PHC
 * @param enable boolean to enable/disable
 * @return int 0 on success, -1 on failure
 */
static int enable_pps(int fd, bool enable)
{
	if (ioctl(fd, PTP_ENABLE_PPS, enable ? 1 : 0) < 0) {
		log_error("PTP_ENABLE_PPS failed");
		return -1;
	}
	return 0;
}

static void prepare_minipod_config(struct minipod_config* minipod_config, struct config * config)
{
	minipod_config->calibrate_first = config_get_bool_default(config, "calibrate_first", false);
	minipod_config->debug = config_get_unsigned_number(config, "debug");
	minipod_config->fine_stop_tolerance = config_get_unsigned_number(config, "fine_stop_tolerance");
	minipod_config->max_allowed_coarse = config_get_unsigned_number(config, "max_allowed_coarse");
	minipod_config->nb_calibration = config_get_unsigned_number(config, "nb_calibration");
	minipod_config->phase_jump_threshold_ns = config_get_unsigned_number(config, "phase_jump_threshold_ns");
	minipod_config->phase_resolution_ns = config_get_unsigned_number(config, "phase_resolution_ns");
	minipod_config->reactivity_max = config_get_unsigned_number(config, "reactivity_max");
	minipod_config->reactivity_min = config_get_unsigned_number(config, "reactivity_min");
	minipod_config->reactivity_power = config_get_unsigned_number(config, "reactivity_power");
	minipod_config->ref_fluctuations_ns = config_get_unsigned_number(config, "ref_fluctuations_ns");
	minipod_config->oscillator_factory_settings = config_get_bool_default(config, "oscillator_factory_settings", true);
	minipod_config->learn_temperature_table = config_get_bool_default(config, "learn_temperature_table", false);
	minipod_config->use_temperature_table = config_get_bool_default(config, "use_temperature_table", false);
	minipod_config->fine_table_output_path = config_get_default(config, "fine_table_output_path", "/tmp/");
}

/**
 * @brief Main program function
 *
 * @param argc
 * @param argv used to ge config file path
 */
int main(int argc, char *argv[])
{
	struct config config;
	struct gps_device_t session = {};
	struct phasemeter *phasemeter = NULL;
	struct oscillator_ctrl ctrl_values;
	struct gnss *gnss;
	struct monitoring *monitoring = NULL;
	struct od_input input = {0};
	struct od_output output = {0};
	struct minipod_config minipod_config = {0};
	struct disciplining_parameters dsc_params = {0};
	const char *path;
	char err_msg[OD_ERR_MSG_LEN];
	struct oscillator_attributes osc_attr = { 0 };
	int64_t phase_error;
	int phasemeter_status;
	int ret;
	int sign = 0;
	int log_level;
	bool disciplining_mode;
	bool monitoring_mode;
	bool opposite_phase_error;
	bool phase_error_supported = false;
	bool ignore_next_irq = false;
	bool fake_holdover_activated = false;
	__attribute__((cleanup(fd_cleanup))) int fd_clock = -1;
	volatile struct pps_thread_t * pps_thread = NULL;
	time_t start_save_epprom_parameters, end_save_eeprom_parameters;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGPIPE, SIG_IGN);

	if (argc != 2)
		error(EXIT_FAILURE, 0, "usage: %s config_file_path", argv[0]);
	path = argv[1];

	/* Read Config file */
	ret = config_init(&config, path);
	if (ret != 0) {
		error(EXIT_FAILURE, -ret, "config_init(%s)", path);
		return -EINVAL;
	}

	/* Get disciplining and monitoring values from config
	 * to know how oscillatord should behave
	 */
	disciplining_mode = config_get_bool_default(&config, "disciplining", false);
	monitoring_mode = config_get_bool_default(&config, "monitoring", false);
	if (!disciplining_mode && !monitoring_mode) {
		log_error("No disciplining and no monitoring requested, Exiting.");
		return -EINVAL;
	}

	/* Get devices' path from sysfs directory */
	ret = config_discover_devices(&config, &devices_path);
	if (ret != 0) {
		error(EXIT_FAILURE, -ret, "get_devices_path_from_sysfs");
		return ret;
	}

	/* Set log level according to configuration */
	log_level = config_get_unsigned_number(&config, "debug");
	log_set_level(log_level >= 0 ? log_level : 0);
	log_info("Starting Oscillatord v%s", PACKAGE_VERSION);

	/* Start Monitoring Thread */
	if (monitoring_mode) {
		monitoring = monitoring_init(&config, &devices_path);
		if (monitoring == NULL) {
			log_error("Error creating monitoring socket thread");
			return -EINVAL;
		}
		log_info("Starting monitoring socket");
	}

	/* Create oscillator object */
	oscillator = oscillator_factory_new(&config, &devices_path);
	if (oscillator == NULL) {
		error(EXIT_FAILURE, errno, "oscillator_factory_new");
		return -EINVAL;
	}
	log_info("oscillator model %s", oscillator->class->name);

	/* Handle phase error */
	if (monitoring_mode) {
		phase_error_supported = (oscillator_get_phase_error(oscillator, &phase_error) != -ENOSYS);
		if (phase_error_supported)
			sign = 1;
		pthread_mutex_lock(&monitoring->mutex);
		monitoring->phase_error_supported = phase_error_supported;
		pthread_mutex_unlock(&monitoring->mutex);
	}

	/* Open PTP clock file descriptor */
	fd_clock = open(devices_path.ptp_path, O_RDWR);
	if (fd_clock == -1 && disciplining_mode) {
		log_error("Could not open ptp clock device while disciplining_mode is activated !");
		error(EXIT_FAILURE, errno, "open(%s)", devices_path.ptp_path);
		return -EINVAL;
	}

	/* Init GPS session and context */
	session.context = &context;
	(void)memset(&context, '\0', sizeof(struct gps_context_t));
	context.leap_notify = LEAP_NOWARNING;
	session.sourcetype = source_pps;
	pps_thread = &(session.pps_thread);
	pps_thread->context = &session;

	/* Start GNSS Thread */
	char flip_flip_path[5000];
	snprintf(flip_flip_path, sizeof(flip_flip_path) - 1, "%s@115200", devices_path.gnss_path);
	gnss = gnss_init(&config, flip_flip_path, &session, fd_clock);
	if (gnss == NULL) {
		error(EXIT_FAILURE, errno, "Failed to listen to the receiver");
		return -EINVAL;
	}
	if (monitoring) {
		gnss->gnss_info = &monitoring->gnss_info;
	}

	if (disciplining_mode) {
		/* Get disciplining parameters files exposed by driver */
		ret = read_disciplining_parameters_from_eeprom(
			devices_path.disciplining_config_path,
			devices_path.temperature_table_path,
			&dsc_params
		);
		if (ret != 0) {
			log_error("Failed to read disciplining_parameters from EEPROM");
			return -EINVAL;
		}
		opposite_phase_error = config_get_bool_default(&config,
				"opposite-phase-error", false);
		sign = opposite_phase_error ? -1 : 1;

		prepare_minipod_config(&minipod_config, &config);

		/* Create shared library oscillator object */
		od = od_new_from_config(&minipod_config, &dsc_params, err_msg);
		if (od == NULL) {
			error(EXIT_FAILURE, errno, "od_new %s", err_msg);
			return -EINVAL;
		}
		/* Get time to know when to save disciplining parameters */
		time(&start_save_epprom_parameters);

		/* Start Phasemeter Thread */
		phasemeter = phasemeter_init(fd_clock);
		if (phasemeter == NULL) {
			return -EINVAL;
		}
		/* Wait for all thread to get at least one piece of data */
		sleep(2);

		/* Check that program should still be running before setting PTP time */
		if (loop) {
			/* Init PTP clock time */
			log_info("Initialize time of ptp clock %s", devices_path.ptp_path);
			ret = gnss_set_ptp_clock_time(gnss);
			if (ret != 0) {
				log_error("Could not set ptp clock time: err %d", ret);
				return -EINVAL;
			}
		}
		phase_error_supported = true;

		/* Check if program is still supposed to be running or has been requested to terminate */
		if(loop) {
			/* Apply initial phase jump before setting PTP clock time */
			do {
				phasemeter_status = get_phase_error(phasemeter, &phase_error);
			} while (phasemeter_status != PHASEMETER_BOTH_TIMESTAMPS);
			log_debug("Initial phase error to apply is %" PRIi64, phase_error);
			log_info("Applying initial phase jump before setting PTP clock time");
			ret = apply_phase_offset(
				fd_clock,
				devices_path.ptp_path,
				-phase_error * sign
			);
			if (ret < 0)
				error(EXIT_FAILURE, -ret, "apply_phase_offset");
			sleep(SETTLING_TIME);

			/* Check PTP Clock time is properly set */
			log_info("Reset PTP Clock time after rough alignment to GNSS");
			ret = gnss_set_ptp_clock_time(gnss);
			if (ret != 0) {
				log_error("Could not set ptp clock time");
				return -EINVAL;
			}
		}
	}

	/* Check if program is still intend to run before continuing */
	if (loop) {
		/* Start NTP SHM session */
		enable_pps(fd_clock, true);
		(void)ntpshm_context_init(&context);

		/* Start PPS Thread that triggers writes in NTP SHM */
		if (strlen(devices_path.pps_path) != 0) {
			pps_thread->devicename = (char *)&devices_path.pps_path;
			pps_thread->log_hook = ppsthread_log;
			log_info("Init NTP SHM session");
			ntpshm_session_init(&session);
			ntpshm_link_activate(&session);
		} else {
			log_warn("No pps-device found in sysfs, NTPSHM will no be filled");
		}
	}

	/* Main Loop */
	while(loop) {
		if (disciplining_mode) {
			/* Get Phase error and status*/
			phasemeter_status = get_phase_error(phasemeter, &osc_attr.phase_error);

			if (gnss_get_epoch_data(gnss, &input.valid, &input.survey_completed, &input.qErr) != 0) {
				log_error("Error getting GNSS data, exiting");
				break;
			}
			/* Wait for phase error before getting oscillator control values */
			/* This prevents control values to be read right after writing them */

			/* Oscillator control values and temperature are needed for
			* the disciplining algorithm and monitoring, get both of them
			*/
			ret = oscillator_parse_attributes(oscillator, &osc_attr);
			if (ret == -ENOSYS) {
				osc_attr.temperature = 0.0;
				osc_attr.locked = false;
			} else if (ret < 0) {
				log_warn("Coud not get temperature of oscillator");
				continue;
			}

			ret = oscillator_get_ctrl(oscillator, &ctrl_values);
			if (ret != 0) {
				log_warn("Could not get control values of oscillator");
				continue;
			}

			if (ignore_next_irq) {
				log_debug("ignoring 1 input due to phase jump");
				ignore_next_irq = false;
				continue;
			}

			/* Fills in input structure with current phasemeter status */
			input.phasemeter_status = phasemeter_status;

			if (output.action == ADJUST_FINE && output.setpoint != ctrl_values.fine_ctrl) {
				log_error("Could not apply output to mro50");
				log_error("Requested value was %u, control value read is %u", output.setpoint, ctrl_values.fine_ctrl);
				//error(EXIT_FAILURE, -EIO, "apply_output");
			}

			/* Fills in input structure for disciplining algorithm */
			input.coarse_setpoint = ctrl_values.coarse_ctrl;
			input.fine_setpoint = ctrl_values.fine_ctrl;
			input.temperature = osc_attr.temperature;
			input.lock = osc_attr.locked;
			input.phase_error = (struct timespec) {
				.tv_sec = sign * osc_attr.phase_error / NS_IN_SECOND,
				.tv_nsec = sign * osc_attr.phase_error % NS_IN_SECOND,
			};

			if (fake_holdover_activated) {
				log_warn("Fake Holdover activated: make minipod think gnss is not valid");
				input.valid = false;
			}

			log_info("input: phase_error = (%lds, %09ldns), "
				"valid = %s, survey = %s, qErr = %d,lock = %s, fine = %d, "
				"coarse = %d, temp = %.2f°C, calibration requested: %s",
				input.phase_error.tv_sec,
				input.phase_error.tv_nsec,
				input.valid ? "true" : "false",
				input.survey_completed ? "true" : "false",
				input.qErr,
				input.lock ? "true" : "false",
				input.fine_setpoint,
				input.coarse_setpoint,
				input.temperature,
				input.calibration_requested ? "true" : "false");

			/* Call disciplining algorithm process loop */
			ret = od_process(od, &input, &output);
			if (ret < 0)
				error(EXIT_FAILURE, -ret, "od_process");
			/* Resets input structure to empty values */
			input = (struct od_input) {0};

			/* Process output result of the algorithm */
			if (output.action == PHASE_JUMP) {
				log_info("Phase jump requested");
				ret = apply_phase_offset(
					fd_clock,
					devices_path.ptp_path,
					-output.value_phase_ctrl
				);

				if (ret < 0)
					error(EXIT_FAILURE, -ret, "apply_phase_offset");
				ignore_next_irq = true;

			} else if (output.action == CALIBRATE) {
				log_info("Calibration requested");
				if (monitoring_mode) {
					pthread_mutex_lock(&monitoring->mutex);
					od_get_monitoring_data(od, &monitoring->disciplining);
					pthread_mutex_unlock(&monitoring->mutex);
				}
				struct calibration_parameters * calib_params = od_get_calibration_parameters(od);
				if (calib_params == NULL)
					error(EXIT_FAILURE, -ENOMEM, "od_get_calibration_parameters");

				struct calibration_results *results = oscillator_calibrate(oscillator, phasemeter, gnss, calib_params, sign);
				if (results != NULL)
					od_calibrate(od, calib_params, results);
				else {
					if (!loop)
						break;
					else
						error(EXIT_FAILURE, -ENOMEM, "oscillator_calibrate");
				}

			} else if (output.action == SAVE_DISCIPLINING_PARAMETERS) {
					ret = od_get_disciplining_parameters(od, &dsc_params);
					if (ret != 0)
						log_error("Could not get discipling parameters from disciplining algorithm");
					dsc_params.dsc_config.calibration_date = time(NULL);

					ret = write_disciplining_parameters_in_eeprom(
						devices_path.disciplining_config_path,
						devices_path.temperature_table_path,
						&dsc_params
					);
					if (ret < 0) {
						log_error("Error saving data to EEPROM");
					} else {
						log_info("Saved disciplining parameters into EEPROM");
					}

					/* Disable calibrate first to prevent a new calibration when rebooting */
					config_set(&config, "calibrate_first", "false");
					if (config_save(&config, path) != 0) {
						log_warn("Could not disable calibration at boot in config at %s", path);
						log_warn("If you restart oscillatord calibration will be done again !");
					}
			} else if (output.action != NO_OP) {
				ret = oscillator_apply_output(oscillator, &output);
				if (ret < 0) {
					log_error("Could not apply output on oscillator !");
				}
			}
			/* Check if time elapsed is superior to periodic time to save EEPROM data */
			time(&end_save_eeprom_parameters);
			if (difftime(end_save_eeprom_parameters, start_save_epprom_parameters) >= (double) UPDATE_DISCIPLINING_PARAMETERS_SEC) {
				log_info("Periodically saving EEPROM data");
				if (schedule_disciplining_parameters_save(od) != 0)
					log_error("Could not start the periodic EEPROM save");
				/* Reset time to save eeprom data*/
				time(&start_save_epprom_parameters);
			}
		} else {
			/* Used for monitoring only */
			/* Oscillator control values and temperature are needed for
			 * the disciplining algorithm and monitoring, get both of them.
			 * We don't really want to poll atomic clock instantly, so let's
			 * sleep for a second.
			 */
			usleep(1000);
			ret = oscillator_parse_attributes(oscillator, &osc_attr);
			if (ret == -ENOSYS) {
				osc_attr.temperature = 0.0;
				osc_attr.locked = false;
			} else if (ret < 0)
				error(EXIT_FAILURE, -ret, "oscillator_get_temp");
			if (phase_error_supported) {
				bool fixOk = false;
				struct timespec lastFix = {};
				gnss_get_fix_info(gnss, &fixOk, &lastFix);
				oscillator_push_gnss_info(oscillator, fixOk, &lastFix);
			}
			ret = oscillator_get_ctrl(oscillator, &ctrl_values);
			if (ret != 0) {
				log_warn("Could not get control values of oscillator");
				continue;
			}
		}
		if (monitoring_mode) {
			/* Check for monitoring requests */
			enum monitoring_request request;
			struct od_monitoring disciplining = {
				.clock_class = CLOCK_CLASS_UNCALIBRATED,
				.status = WARMUP,
				.current_phase_convergence_count = -1,
				.valid_phase_convergence_threshold = -1,
				.convergence_progress = 0.00,
				.ready_for_holdover = false,
			};
			if (disciplining_mode) {
				if(od_get_monitoring_data(od, &disciplining) != 0) {
					log_warn("Could not get disciplining data");
					disciplining.clock_class = CLOCK_CLASS_UNCALIBRATED;
					disciplining.status = INIT;
					disciplining.current_phase_convergence_count = -1;
					disciplining.valid_phase_convergence_threshold = -1;
					disciplining.convergence_progress = 0.0;
				}
				osc_attr.phase_error *= sign;
			} else if (phase_error_supported) {
				/* this actually means that oscillator has it's own hardware disciplining
				 * algorithm and we are able to monitor it
				 */
				oscillator_get_phase_error(oscillator, &osc_attr.phase_error);
				oscillator_get_disciplining_status(oscillator, &disciplining);
			}

			pthread_mutex_lock(&monitoring->mutex);
			monitoring->osc_attributes = osc_attr;
			monitoring->ctrl_values = ctrl_values;
			monitoring->disciplining = disciplining;
			request = monitoring->request;
			monitoring->request = REQUEST_NONE;
			pthread_mutex_unlock(&monitoring->mutex);

			switch(request) {
			case REQUEST_CALIBRATION:
				log_info("Monitoring: Calibration requested");
				input.calibration_requested = true;
				break;
			case REQUEST_GNSS_START:
				log_info("Monitoring: GNSS Start requested");
				gnss_set_action(gnss, GNSS_ACTION_START);
				break;
			case REQUEST_GNSS_STOP:
				log_info("Monitoring: GNSS Stop requested");
				gnss_set_action(gnss, GNSS_ACTION_STOP);
				break;
			case REQUEST_GNSS_SOFT:
				log_info("Monitoring: GNSS Soft requested");
				gnss_set_action(gnss, GNSS_ACTION_SOFT);
				break;
			case REQUEST_GNSS_HARD:
				log_info("Monitoring: GNSS Hard requested");
				gnss_set_action(gnss, GNSS_ACTION_HARD);
				break;
			case REQUEST_GNSS_COLD:
				log_info("Monitoring: GNSS Cold requested");
				gnss_set_action(gnss, GNSS_ACTION_COLD);
				break;
			case REQUEST_SAVE_EEPROM:
				log_info("Monitoring: Saving EEPROM data");
				if (schedule_disciplining_parameters_save(od) != 0)
					log_error("Could not start the requested EEPROM save");
				break;
			case REQUEST_FAKE_HOLDOVER_START:
				fake_holdover_activated = true;
				break;
			case REQUEST_FAKE_HOLDOVER_STOP:
				fake_holdover_activated = false;
				break;
			case REQUEST_RESET_UBLOX_SERIAL:
				log_info("Monitoring: Ublox serial reset requested");
				gnss_set_action(gnss, GNSS_ACTION_RESET_SERIAL);
				break;
			case REQUEST_READ_EEPROM:
				log_warn("Read EEPROM: not implemented");
				break;
			case REQUEST_MRO_COARSE_INC:
				log_info("Monitoring: MRO INC requested");
				struct od_output adj_coarse_inc_output = { .action = ADJUST_COARSE, .setpoint = ctrl_values.coarse_ctrl + 1, };
				ret = oscillator_apply_output(oscillator, &adj_coarse_inc_output);
				if (ret < 0) {
					log_error("Could not apply output on oscillator !");
				}
				break;
			case REQUEST_MRO_COARSE_DEC:
				log_info("Monitoring: MRO DEC requested");
				struct od_output adj_coarse_dec_output = { .action = ADJUST_COARSE, .setpoint = ctrl_values.coarse_ctrl - 1, };
				ret = oscillator_apply_output(oscillator, &adj_coarse_dec_output);
				if (ret < 0) {
					log_error("Could not apply output on oscillator !");
				}
				break;
			case REQUEST_NONE:
			default:
				break;
			}

		}
	}

	enable_pps(fd_clock, false);
	if (pps_thread != NULL && pps_thread->devicename != NULL)
		ntpshm_link_deactivate(&session);

	gnss_stop(gnss);

	if (disciplining_mode) {
		finish_disciplining_parameters_save();
		phasemeter_stop(phasemeter);
		ret = od_get_disciplining_parameters(od, &dsc_params);
		if (ret != 0) {
			log_error("Could not get discipling parameters from disciplining algorithm");
		} else {
			log_debug("Printing disciplining_parameters");
			print_disciplining_parameters(&dsc_params, LOG_INFO);
			ret = write_disciplining_parameters_in_eeprom(
				devices_path.disciplining_config_path,
				devices_path.temperature_table_path,
				&dsc_params
			);
			if (ret < 0)
				log_error("Error saving data to EEPROM");
			else
				log_info("Saved calibration parameters into EEPROM");
		}
		od_destroy(&od);
	}
	if (monitoring_mode)
		monitoring_stop(monitoring);
	if (fd_clock != -1)
		close(fd_clock);
	if (oscillator != NULL) {
		oscillator_factory_destroy(&oscillator);
	}

	config_cleanup(&config);

	return EXIT_SUCCESS;
}
