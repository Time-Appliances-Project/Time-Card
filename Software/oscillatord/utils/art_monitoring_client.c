/**
 * @file art_monitoring_client.c
 * @author Charles Parent (charles.parent@orolia2s.com)
 * @brief Program providing a simple interface to interact wit oscillatord
 * through its monitoring socket
 * @date 2022-06-13
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "log.h"
#include "monitoring.h"

#include <getopt.h>
#include <json-c/json.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_help(void)
{
	printf("usage: art_monitoring_client [-h -r REQUEST_TYPE -a ADDRESS] -p PORT\n");
	printf("- -a ADDRESS: Address socket should bind to. Defaults to local address\n");
	printf("- -p PORT: Port socket should bind to\n");
	printf("- -r REQUEST_TYPE: send a request to oscillatord. Accepted values are:\n");
	printf("\t- calibration: request a calibration of the algorithm\n");
	printf("\t- gnss_start: start gnss receiver\n");
	printf("\t- gnss_stop: stop gnss receiver.\n");
	printf("\t- read_eeprom: read disciplining data from EEPROM.\n");
	printf("\t- save_eeprom: save minipod's disciplining data in EEPROM.\n");
	printf("\t- fake_holdover_start: start fake holdover\n");
	printf("\t- fake_holdover_stop: stop fake holdover.\n");
	printf("- -h: prints help\n");
	return;
}

/* Send json formatted request and returns json response */
static struct json_object *json_send_and_receive(int sockfd, int request)
{
	int ret;

	struct json_object *json_req = json_object_new_object();
	json_object_object_add(json_req, "request", json_object_new_int(request));

	const char *req = json_object_to_json_string(json_req);
	char buf[1024];
	strcpy(buf, req);
	ret = send(sockfd, req, strlen(buf), 0);
	if (ret == -1)
	{
		log_error("Error sending request: %d", ret);
		log_error("FAIL");
		return NULL;
	}

	char *resp = (char *)malloc(sizeof(char) * 2048);
	ret = recv(sockfd, resp, 2048, 0);
	if (-1 == ret)
	{
		log_error("Error receiving response: %d", ret);
		log_error("FAIL");
		return NULL;
	}

	return json_tokener_parse(resp);
}

int main(int argc, char *argv[]) {
	int c;
	int request = REQUEST_NONE;
	const char* socket_port = NULL;
	const char* socket_addr = NULL;

	while ((c = getopt(argc, argv, "a:p:r:h")) != -1)
	switch (c)
	{
		case 'a':
			socket_addr = optarg;
			break;
		case 'p':
			socket_port = optarg;
			break;
		case 'r':
		if (strcmp(optarg, "calibration") == 0)
			request = REQUEST_CALIBRATION;
		else if (strcmp(optarg, "gnss_start") == 0)
			request = REQUEST_GNSS_START;
		else if (strcmp(optarg, "gnss_stop") == 0)
			request = REQUEST_GNSS_STOP;
		else if (strcmp(optarg, "gnss_soft") == 0)
			request = REQUEST_GNSS_SOFT;
		else if (strcmp(optarg, "gnss_hard") == 0)
			request = REQUEST_GNSS_HARD;
		else if (strcmp(optarg, "gnss_cold") == 0)
			request = REQUEST_GNSS_COLD;
		else if (strcmp(optarg, "read_eeprom") == 0)
			request = REQUEST_READ_EEPROM;
		else if (strcmp(optarg, "save_eeprom") == 0)
			request = REQUEST_SAVE_EEPROM;
		else if (strcmp(optarg, "fake_holdover_start") == 0)
			request = REQUEST_FAKE_HOLDOVER_START;
		else if (strcmp(optarg, "fake_holdover_stop") == 0)
			request = REQUEST_FAKE_HOLDOVER_STOP;
		else if (strcmp(optarg, "mro_coarse_inc") == 0)
			request = REQUEST_MRO_COARSE_INC;
		else if (strcmp(optarg, "mro_coarse_dec") == 0)
			request = REQUEST_MRO_COARSE_DEC;
		else {
			log_error("Unknown request %s", optarg);
			return -1;
		}
		log_info("Action requested: %s", optarg);
		break;
	case 'h':
		print_help();
		return 0;
	case '?':
		if (optopt == 'r')
			fprintf (stderr, "Option -%c requires request type.\n", optopt);
		else
			fprintf (stderr,
					"Unknown option character `\\x%x'.\n",
					optopt);
		return EXIT_FAILURE;
	default:
		abort();
	}

	if (socket_port == NULL) {
		log_error("Bad port");
		print_help();
		return EXIT_FAILURE;
	}

	int              socket_fd;
	int              status;
	struct addrinfo* addresses;
	struct addrinfo* current;
	struct addrinfo  hint = {
		.ai_family = AF_UNSPEC,
		.ai_protocol = IPPROTO_TCP,
	};

	status = getaddrinfo(socket_addr, socket_port, &hint, &addresses);
	if (status == EAI_SYSTEM)
	{
		log_error("Unable to get an Internet address from '%s:%s': %s", socket_addr, socket_port, strerror(errno));
		return EXIT_FAILURE;
	}
	else if (status != 0)
	{
		log_error("Unable to get an Internet address from '%s:%s': %s", socket_addr, socket_port, gai_strerror(status));
		return EXIT_FAILURE;
	}

	for (current = addresses; current; current = current->ai_next)
	{
		socket_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
		if (socket_fd < 0)
		{
			log_warn("Couldn't open a socket for '%s:%s' (IPv%i): %s", socket_addr, socket_port, current->ai_family == AF_INET ? 4 : 6, strerror(errno));
			continue;
		}
		if (connect(socket_fd, current->ai_addr, current->ai_addrlen) == 0)
			break;
		log_warn("Couldn't connect to '%s:%s' (IPv%i) : %s", socket_addr, socket_port, current->ai_family == AF_INET ? 4 : 6, strerror(errno));

		close(socket_fd);
	}
	freeaddrinfo(addresses);

	if (current == NULL)
	{
		log_error("Could not connect to %s:%s", socket_addr, socket_port);
		return EXIT_FAILURE;
	}

	/* Request data through socket */
	struct json_object *obj = json_send_and_receive(socket_fd, request);
	struct json_object *layer_1;
	struct json_object *layer_2;
	struct json_object *layer_3;

	log_info(json_object_to_json_string(obj));

	/* Disciplining */
	json_object_object_get_ex(obj, "disciplining", &layer_1);
	if (layer_1 != NULL) {
		json_object_object_get_ex(layer_1, "status", &layer_2);
		const char *status = json_object_get_string(layer_2);
		json_object_object_get_ex(layer_1, "tracking_only", &layer_2);
		const char *tracking_only = json_object_get_string(layer_2);
		json_object_object_get_ex(layer_1, "current_phase_convergence_count", &layer_2);
		int current_phase_convergence_count = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "valid_phase_convergence_threshold", &layer_2);
		int valid_phase_convergence_threshold = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "convergence_progress", &layer_2);
		double convergence_progress = json_object_get_double(layer_2);
		json_object_object_get_ex(layer_1, "ready_for_holdover", &layer_2);
		const char *ready_for_holdover = json_object_get_string(layer_2);
		log_info("Disciplining detected");
		log_info("\t- Current status: %s", status);
		log_info("\t- tracking_only: %s", tracking_only);
		log_info("\t- ready_for_holdover: %s", ready_for_holdover);

		if (strcmp(status,"TRACKING") == 0) {
			log_info("\t- tracking convergence progress: %0.2f %% (%d/%d)",convergence_progress,current_phase_convergence_count,valid_phase_convergence_threshold);
		}
		else if (strcmp(status,"LOCK_LOW_RESOLUTION") == 0) {
			log_info("\t- lock low resolution convergence progress: %0.2f %% (%d/%d)",convergence_progress,current_phase_convergence_count,valid_phase_convergence_threshold);
		}
		else if (strcmp(status,"LOCK_HIGH_RESOLUTION") == 0) {
			log_info("\t- lock high resolution convergence progress: %0.2f %% (%d/%d)",convergence_progress,current_phase_convergence_count,valid_phase_convergence_threshold);
		}
	}

	/* Oscillator */
	json_object_object_get_ex(obj, "oscillator", &layer_1);
	if (layer_1 != NULL) {
		json_object_object_get_ex(layer_1, "model", &layer_2);
		const char * model = json_object_get_string(layer_2);
		json_object_object_get_ex(layer_1, "fine_ctrl", &layer_2);
		uint32_t fine_ctrl = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "coarse_ctrl", &layer_2);
		uint32_t coarse_ctrl = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "lock", &layer_2);
		bool lock = json_object_get_boolean(layer_2);
		json_object_object_get_ex(layer_1, "temperature", &layer_2);
		double temperature = json_object_get_double(layer_2);
		log_info("Oscillator detected");
		log_info("\t- model: %s", model);
		log_info("\t- fine_ctrl: %u", fine_ctrl);
		log_info("\t- coarse_ctrl: %u", coarse_ctrl);
		log_info("\t- lock: %s", lock ? "True" : "False");
		log_info("\t- temperature: %f", temperature);
	}

	/* Oscillator */
	json_object_object_get_ex(obj, "clock", &layer_1);
	if (layer_1 != NULL) {
		json_object_object_get_ex(layer_1, "class", &layer_2);
		const char * class = json_object_get_string(layer_2);
		json_object_object_get_ex(layer_1, "offset", &layer_2);
		int32_t offset = json_object_get_int(layer_2);
		log_info("Clock detected");
		log_info("\t- class: %s", class);
		log_info("\t- offset: %d", offset);
	}

	/* GNSS */
	json_object_object_get_ex(obj, "gnss", &layer_1);
	if (layer_1 != NULL) {
		json_object_object_get_ex(layer_1, "fix", &layer_2);
		int fix = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "antenna_status", &layer_2);
		int antenna_status = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "antenna_power", &layer_2);
		int antenna_power = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "lsChange", &layer_2);
		int lsChange = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "leap_seconds", &layer_2);
		int leap_seconds = json_object_get_int(layer_2);
		json_object_object_get_ex(layer_1, "fixOk", &layer_2);
		bool fixOk = json_object_get_boolean(layer_2);
		json_object_object_get_ex(layer_1, "survey_in_position_error", &layer_2);
		double survey_in_position_error = json_object_get_double(layer_2);
		log_info("GNSS detected");
		log_info("\t- fix: %u", fix);
		log_info("\t- fixOk: %s", fixOk ? "True" : "False");
		log_info("\t- antenna_status: %u", antenna_status);
		log_info("\t- antenna_power: %u", antenna_power);
		log_info("\t- survey_in_position_error: %0.2f m", survey_in_position_error);
		log_info("\t- lsChange: %u", lsChange);
		log_info("\t- leap_seconds: %u", leap_seconds);
	}

	/* Disciplining parameters */
	json_object_object_get_ex(obj, "disciplining_parameters", &layer_1);
	if (layer_1 != NULL) {
		log_info("Disciplining parameters detected");
		json_object_object_get_ex(layer_1, "calibration_parameters", &layer_2);
		if (layer_2 != NULL) {
			json_object_object_get_ex(layer_2, "ctrl_nodes_length", &layer_3);
			int ctrl_nodes_length = json_object_get_int(layer_3);
			json_object_object_get_ex(layer_2, "ctrl_load_nodes", &layer_3);
			const char * ctrl_load_nodes = json_object_get_string(layer_3);
			json_object_object_get_ex(layer_2, "ctrl_drift_coeffs", &layer_3);
			const char * ctrl_drift_coeffs = json_object_get_string(layer_3);
			json_object_object_get_ex(layer_2, "coarse_equilibrium", &layer_3);
			int coarse_equilibrium = json_object_get_int(layer_3);
			json_object_object_get_ex(layer_2, "calibration_date", &layer_3);
			int calibration_date = json_object_get_int(layer_3);
			json_object_object_get_ex(layer_2, "calibration_valid", &layer_3);
			const char * calibration_valid = json_object_get_string(layer_3);
			json_object_object_get_ex(layer_2, "ctrl_nodes_length_factory", &layer_3);
			int ctrl_nodes_length_factory = json_object_get_int(layer_3);
			json_object_object_get_ex(layer_2, "ctrl_load_nodes_factory", &layer_3);
			const char * ctrl_load_nodes_factory = json_object_get_string(layer_3);
			json_object_object_get_ex(layer_2, "ctrl_drift_coeffs_factory", &layer_3);
			const char * ctrl_drift_coeffs_factory = json_object_get_string(layer_3);
			json_object_object_get_ex(layer_2, "coarse_equilibrium_factory", &layer_3);
			int coarse_equilibrium_factory = json_object_get_int(layer_3);
			json_object_object_get_ex(layer_2, "estimated_equilibrium_ES", &layer_3);
			int estimated_equilibrium_ES = json_object_get_int(layer_3);
			log_info("\t- Calibration parameters");
			log_info("\t\t- ctrl_nodes_length: %d", ctrl_nodes_length);
			log_info("\t\t- ctrl_load_nodes: %s", ctrl_load_nodes);
			log_info("\t\t- ctrl_drift_coeffs: %s", ctrl_drift_coeffs);
			log_info("\t\t- coarse_equilibrium: %d", coarse_equilibrium);
			log_info("\t\t- calibration_date: %d", calibration_date);
			log_info("\t\t- calibration_valid: %s", calibration_valid);
			log_info("\t\t- ctrl_nodes_length_factory: %d", ctrl_nodes_length_factory);
			log_info("\t\t- ctrl_load_nodes_factory: %s", ctrl_load_nodes_factory);
			log_info("\t\t- ctrl_drift_coeffs_factory: %s", ctrl_drift_coeffs_factory);
			log_info("\t\t- coarse_equilibrium_factory: %d", coarse_equilibrium_factory);
			log_info("\t\t- estimated_equilibrium_ES: %d", estimated_equilibrium_ES);
		}

		json_object_object_get_ex(layer_1, "temperature_table", &layer_2);
		if (layer_2 != NULL) {
			log_info("\t- Temperature table");
			json_object_object_foreach(layer_2, temperature_range, mean_value) {
				log_info("\t\t- %s: %s", temperature_range, json_object_get_string(mean_value));
			}
		}

	}

	/* ACTION */
	json_object_object_get_ex(obj, "Action requested", &layer_1);
	if (layer_1 != NULL)
		log_info("Action requested: %s", json_object_get_string(layer_1));

	free(obj);

	close(socket_fd);
	log_info("PASSED !");


	return EXIT_SUCCESS;
}
