/**
 * @file monitoring.c
 * @brief monitoring handler
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2022-2023
 *
 * This program exposes a socket other processes can connect to and
 * request data from, as well as requesting a calibration.
 * Implementation is based on Eli's work: https://eli.thegreenplace.net/2017/concurrent-servers-part-3-event-driven/
 */
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "eeprom_config.h"
#include "monitoring.h"
#include "log.h"

/** The socket will not be polled for more than 2 seconds at a time */
#define SOCKET_TIMEOUT_MS 2000

/** Maximum number of pending connections queued up. */
#define N_BACKLOG 64

/**
 * Maximum file descriptor the socket can handle.
 *
 * One peer state will be allocated on the stack for each file descriptor (FD).
 * This number isn't exactily the maximum number of peers this program
 * will be able to handle, but this number minus 1 will be the highest
 * FD handled.
*/
#define MAXFDS 16 * 1024

/** Number of chars allocated on the stack for each peer */
#define SENDBUF_SIZE 1024

typedef enum { INITIAL_ACK, WAIT_FOR_MSG, IN_MSG } ProcessingState;

/** Data stored for each peer. */
typedef struct {
	ProcessingState state;
	char recv_buf[SENDBUF_SIZE];
	int buf_end;
	int buf_ptr;
} peer_state_t;

/**
 * Global table of peers, with file descriptors as keys and peers as values.
 *
 * Each peer is globally identified by the file descriptor (fd) it's connected
 * on. As long as the peer is connected, the fd is unique to it. When a peer
 * disconnects, a new peer may connect and get the same fd. on_peer_connected
 * should initialize the state properly to remove any trace of the old peer on
 * the same fd.
*/
peer_state_t global_state[MAXFDS];

/**
 * File descriptor status.
 *
 * Callbacks (on_XXX functions) return this status to the main loop; the status
 * instructs the loop about the next steps for the fd for which the callback was
 * invoked.
 * want_read=true means we want to keep monitoring this fd for reading.
 * want_write=true means we want to keep monitoring this fd for writing.
 * When both are false it means the fd is no longer needed and can be closed.
*/
typedef struct {
	bool want_read:1;
	bool want_write:1;
} fd_status_t;

// These constants make creating fd_status_t values less verbose.
const fd_status_t fd_status_R    = {.want_read = true,  .want_write = false};
const fd_status_t fd_status_W    = {.want_read = false, .want_write = true};
const fd_status_t fd_status_RW   = {.want_read = true,  .want_write = true};
const fd_status_t fd_status_NORW = {.want_read = false, .want_write = false};

static void * monitoring_thread(void * p_data);

/**
 * @brief Create, bind and listen socket
 *
 * Will use TCP over IP
 * @param address address to listen from. It can be NULL to mean any address.
 * @param port port to bind to, it can be a number or the name of a service
 * @return socket fd on success, -1 if error
 */
static int        create_socket(const char* address, const char* port)
{
	int              status;
	int              socket_fd;
	const int        reuse_address = true;
	struct addrinfo* addresses     = NULL;
	struct addrinfo* current;
	struct addrinfo  hint = {
		.ai_family = AF_UNSPEC,
		.ai_protocol = IPPROTO_TCP,
		.ai_flags = AI_PASSIVE | AI_NUMERICHOST
	};

	status = getaddrinfo(address, port, &hint, &addresses);
	if (status == EAI_SYSTEM)
	{
		log_error("Unable to translate '%s:%s' into an Internet adrress: %s", address, port, strerror(errno));
		return -1;
	}
	else if (status != 0)
	{
		log_error("Unable to translate '%s:%s' into an Internet adrress: %s", address, port, gai_strerror(status));
		return -1;
	}

	for (current = addresses; current; current = current->ai_next)
	{
		socket_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);

		if (socket_fd < 0)
		{
			log_warn("Couldn't open a socket for IPv%c, %s, %s: %s",
			         current->ai_family == AF_INET ? '4' : '6',
			         current->ai_socktype == SOCK_STREAM ? "Stream" : "Datagram",
			         current->ai_protocol == IPPROTO_TCP ? "TCP" : "UDP",
			         strerror(errno));
			continue;
		}

		if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) < 0)
			log_warn("Unable to configure socket: %s", strerror(errno));
		else if (bind(socket_fd, current->ai_addr, current->ai_addrlen) < 0)
			log_warn("Couldn't bind socket to %s: %s", address, strerror(errno));
		else if (listen(socket_fd, N_BACKLOG) < 0)
			log_warn("Couldn't listen for connections on %s: %s", address, strerror(errno));
		else
			break;

		close(socket_fd);
	}
	freeaddrinfo(addresses);

	if (current == NULL)
	{
		log_error("Could not bind / configure / listen to any address matching %s:%s", address, port);
		return -1;
	}
	return socket_fd;
}

/**
 * @brief Make socket non blocking to allow multiple connections
 *
 * @param sockfd socket file descriptor
 */
static void make_socket_non_blocking(int sockfd) {
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1)
	{
		log_error("Unable to get file status flags of socket: %s", strerror(errno));
	}

	if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		log_error("Unable to set file status flags of socker: %s", strerror(errno));
	}
	return;
}

/**
 * @brief Initialize receive buffer once peer is connected
 *
 * @param sockfd socket file descriptor
 * @param peer_addr
 * @param peer_addr_len
 * @return fd_status_t
 */
static fd_status_t on_peer_connected(int sockfd, const struct sockaddr_in* peer_addr,
									socklen_t peer_addr_len) {
	assert(sockfd < MAXFDS);

	// Initialize state to send back a '*' to the peer immediately.
	peer_state_t* peerstate = &global_state[sockfd];
	peerstate->state = WAIT_FOR_MSG;
	memset(peerstate->recv_buf, 0, SENDBUF_SIZE);
	peerstate->buf_ptr = 0;
	peerstate->buf_end = 0;

	// Signal that this socket is ready for read now.
	return fd_status_R;
}

/**
 * @brief Callback when ready to receive data from client
 *
 * @param sockfd socket file descriptor
 * @return fd_status_t
 */
static fd_status_t on_peer_ready_recv(int sockfd) {
	struct json_object *json;
	assert(sockfd < MAXFDS);
	peer_state_t* peerstate = &global_state[sockfd];

	if (peerstate->state == INITIAL_ACK ||
		peerstate->buf_ptr < peerstate->buf_end) {
		// Until the initial ACK has been sent to the peer, there's nothing we
		// want to receive. Also, wait until all data staged for sending is sent to
		// receive more data.
		return fd_status_R;
	}

	char buf[1024];
	int nbytes = recv(sockfd, buf, sizeof buf, 0);
	if (nbytes == 0) {
		// The peer disconnected.
		return fd_status_NORW;
	} else if (nbytes < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
		// The socket is not *really* ready for recv; wait until it is.
		return fd_status_R;
		} else {
			log_error("recv");
			return fd_status_NORW;
		}
	}
	bool ready_to_send = false;

	/** Store each byte received and try to parse it as a json
	 * if we succeed to parse it as a json then we can analyse
	 * and send the response
	 */
	for (int i = 0; i < nbytes; ++i) {
		switch (peerstate->state) {
		case INITIAL_ACK:
			assert(0 && "can't reach here");
			break;
		case WAIT_FOR_MSG:
			if (buf[i] == '{') {
				peerstate->state = IN_MSG;
				peerstate->recv_buf[0] = buf[i];
				peerstate->buf_end++;
			}
			break;
		case IN_MSG:
			json = json_tokener_parse(peerstate->recv_buf);
			if (json) {
				json_object_put(json);
				peerstate->state = WAIT_FOR_MSG;
				ready_to_send = true;
			} else {
				assert(peerstate->buf_end < SENDBUF_SIZE);
				peerstate->recv_buf[peerstate->buf_end++] = buf[i];
			}
			break;
		}
	}

	json = json_tokener_parse(peerstate->recv_buf);
	if (json) {
		json_object_put(json);
		peerstate->state = WAIT_FOR_MSG;
		ready_to_send = true;
	}

	// Report reading readiness iff there's nothing to analyse from the peer as a
	// result of the latest recv.
	return (fd_status_t){.want_read = !ready_to_send,
						.want_write = ready_to_send};
}

static void json_add_float_array(struct json_object *json, char * array_name, float * array, int length) {
	char array_str[256];
	sprintf(array_str, "[");
	for (int i = 0; i < length; i ++) {
		char item[32];
		sprintf(item, "%.2f", array[i]);
		strcat(array_str, item);
		if (i < length - 1) {
			strcat(array_str, ", ");
		} else {
			strcat(array_str, "]");
		}
	}
	json_object_object_add(
		json,
		array_name,
		json_object_new_string(array_str)
	);
	return;
}

/**
 * @brief Add disciplining_parameters to json response
 *
 * @param resp
 * @param disciplining_parameters
 */
static void json_add_disciplining_disciplining_parameters(struct json_object *resp, struct disciplining_parameters *dsc_params)
{
	struct json_object *disc_parameters_json = json_object_new_object();
	struct json_object *calibration_parameters_json = json_object_new_object();
	struct json_object *temp_table = json_object_new_object();
	struct disciplining_config *dsc_config = &dsc_params->dsc_config;

	json_object_object_add(
		calibration_parameters_json,
		"ctrl_nodes_length",
		json_object_new_int(dsc_config->ctrl_nodes_length)
	);
	if (dsc_config->ctrl_nodes_length > 0) {
		json_add_float_array(
			calibration_parameters_json,
			"ctrl_load_nodes",
			dsc_config->ctrl_load_nodes,
			dsc_config->ctrl_nodes_length
		);
		json_add_float_array(
			calibration_parameters_json,
			"ctrl_drift_coeffs",
			dsc_config->ctrl_drift_coeffs,
			dsc_config->ctrl_nodes_length
		);
	}
	json_object_object_add(
		calibration_parameters_json,
		"coarse_equilibrium",
		json_object_new_int64(dsc_config->coarse_equilibrium)
	);
	json_object_object_add(
		calibration_parameters_json,
		"calibration_date",
		json_object_new_int64(dsc_config->calibration_date)
	);
	json_object_object_add(
		calibration_parameters_json,
		"calibration_valid",
		json_object_new_string(dsc_config->calibration_valid ? "True" : "False")
	);

	json_object_object_add(
		calibration_parameters_json,
		"ctrl_nodes_length_factory",
		json_object_new_int(dsc_config->ctrl_nodes_length_factory)
	);
	if (dsc_config->ctrl_nodes_length > 0) {
		json_add_float_array(
			calibration_parameters_json,
			"ctrl_load_nodes_factory",
			dsc_config->ctrl_load_nodes_factory,
			dsc_config->ctrl_nodes_length_factory
		);
		json_add_float_array(
			calibration_parameters_json,
			"ctrl_drift_coeffs_factory",
			dsc_config->ctrl_drift_coeffs_factory,
			dsc_config->ctrl_nodes_length_factory
		);
	}
	json_object_object_add(
		calibration_parameters_json,
		"coarse_equilibrium_factory",
		json_object_new_int64(dsc_config->coarse_equilibrium_factory)
	);

	json_object_object_add(
		calibration_parameters_json,
		"estimated_equilibrium_ES",
		json_object_new_int64(dsc_config->estimated_equilibrium_ES)
	);
	json_object_object_add(disc_parameters_json, "calibration_parameters", calibration_parameters_json);

	for (int i = 0; i < MEAN_TEMPERATURE_ARRAY_MAX; i++) {
		if (dsc_params->temp_table.mean_fine_over_temperature[i] != 0) {
			char temperature_range[64];
			sprintf(
				temperature_range,
				"[%.2f, %.2f[",
				(i + STEPS_BY_DEGREE * MIN_TEMPERATURE) / STEPS_BY_DEGREE,
				(i + 1 + STEPS_BY_DEGREE * MIN_TEMPERATURE) / STEPS_BY_DEGREE
			);
			char mean_value[32];
			sprintf(
				mean_value,
				"%.1f",
				(float) dsc_params->temp_table.mean_fine_over_temperature[i] / 10
			);
			json_object_object_add(
				temp_table,
				temperature_range,
				json_object_new_string(mean_value)
			);
		}
	}
	json_object_object_add(disc_parameters_json, "temperature_table", temp_table);
	json_object_object_add(resp, "disciplining_parameters", disc_parameters_json);
	return;
}

/**
 * @brief Handle request received by setting monitoring request
 * and add action request in json response
 *
 * @param request_type
 * @param mon_request
 * @param resp
 */
static void json_handle_request(struct monitoring *monitoring, int request_type,
	bool authorized, enum monitoring_request *mon_request,
	struct json_object *resp)
{
	if (request_type != REQUEST_NONE &&
		request_type != REQUEST_READ_EEPROM && !authorized) {
		json_object_object_add(resp, "Action requested",
			json_object_new_string("Denied"));
		json_object_object_add(resp, "error",
			json_object_new_string("monitoring control is disabled or the token is invalid"));
		return;
	}
	if ((request_type == REQUEST_CALIBRATION ||
		 request_type == REQUEST_SAVE_EEPROM ||
		 request_type == REQUEST_FAKE_HOLDOVER_START ||
		 request_type == REQUEST_FAKE_HOLDOVER_STOP) &&
		!monitoring->disciplining_mode) {
		json_object_object_add(resp, "Action requested",
			json_object_new_string("Denied"));
		json_object_object_add(resp, "error",
			json_object_new_string("the daemon is running in monitoring-only mode"));
		return;
	}

	switch (request_type)
	{
	case REQUEST_CALIBRATION:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("calibration"));
		*mon_request = REQUEST_CALIBRATION;
		break;
	case REQUEST_GNSS_START:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("GNSS start"));
		*mon_request = REQUEST_GNSS_START;
		break;
	case REQUEST_GNSS_STOP:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("GNSS stop"));
		*mon_request = REQUEST_GNSS_STOP;
		break;
	case REQUEST_GNSS_SOFT:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("GNSS soft"));
		*mon_request = REQUEST_GNSS_SOFT;
		break;
	case REQUEST_GNSS_HARD:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("GNSS hard"));
		*mon_request = REQUEST_GNSS_HARD;
		break;
	case REQUEST_GNSS_COLD:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("GNSS cold"));
		*mon_request = REQUEST_GNSS_COLD;
		break;
	case REQUEST_READ_EEPROM:
	{
		struct disciplining_parameters dsc_params;
		int ret = read_disciplining_parameters_from_eeprom(monitoring->devices_path.disciplining_config_path,
		                                                   monitoring->devices_path.temperature_table_path,
		                                                   &dsc_params);
		if (ret != 0)
		{
			log_error("Monitoring: Could not get disciplining parameters");
		} else {
			json_add_disciplining_disciplining_parameters(resp, &dsc_params);
		}
		break;
	}
	case REQUEST_SAVE_EEPROM:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("Save EEPROM"));
		*mon_request = REQUEST_SAVE_EEPROM;
		break;
	case REQUEST_FAKE_HOLDOVER_START:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("Start fake holdover"));
		*mon_request = REQUEST_FAKE_HOLDOVER_START;
		break;
	case REQUEST_FAKE_HOLDOVER_STOP:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("Stop fake holdover"));
		*mon_request = REQUEST_FAKE_HOLDOVER_STOP;
		break;
	case REQUEST_MRO_COARSE_INC:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("MRO coarse inc"));
		*mon_request = REQUEST_MRO_COARSE_INC;
		break;
	case REQUEST_MRO_COARSE_DEC:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("MRO coarse dec"));
		*mon_request = REQUEST_MRO_COARSE_DEC;
		break;
	case REQUEST_RESET_UBLOX_SERIAL:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("Ublox Serial reset"));
		*mon_request = REQUEST_RESET_UBLOX_SERIAL;
		break;
	case REQUEST_NONE:
	default:
		json_object_object_add(resp, "Action requested",
			json_object_new_string("None"));
		break;
	}
}

static void json_add_clock_data(struct json_object *resp, struct monitoring *monitoring)
{
	struct json_object *clock = json_object_new_object();
	json_object_object_add(clock, "class",
		json_object_new_string(cstring_from_clock_class(monitoring->disciplining.clock_class))
	);
	json_object_object_add(clock, "offset",
		json_object_new_int64(monitoring->osc_attributes.phase_error));

	json_object_object_add(resp, "clock", clock);

}

/**
 * @brief Add disciplining data to json response
 *
 * @param resp
 * @param monitoring
 */
static void json_add_disciplining_data(struct json_object *resp, struct monitoring *monitoring)
{
	char fix_sized[64];
	snprintf(fix_sized, sizeof(fix_sized), "%.2f", monitoring->disciplining.convergence_progress);

	struct json_object* disciplining = json_object_new_object();
	json_object_object_add(disciplining,
	                       "status",
	                       json_object_new_string(cstring_from_disciplining_state(monitoring->disciplining.status)));
	json_object_object_add(disciplining,
	                       "current_phase_convergence_count",
	                       json_object_new_int(monitoring->disciplining.current_phase_convergence_count));
	json_object_object_add(disciplining,
	                       "valid_phase_convergence_threshold",
	                       json_object_new_int(monitoring->disciplining.valid_phase_convergence_threshold));
	json_object_object_add(disciplining,
	                       "convergence_progress",
	                       json_object_new_double_s(monitoring->disciplining.convergence_progress, fix_sized));
	json_object_object_add(disciplining,
	                       "ready_for_holdover",
	                       json_object_new_boolean(monitoring->disciplining.ready_for_holdover));
	json_object_object_add(resp, "disciplining", disciplining);
}

/**
 * @brief Add oscillator data to json response
 *
 * @param resp
 * @param monitoring
 */
static void json_add_oscillator_data(struct json_object *resp, struct monitoring *monitoring)
{
	char fix_sized[64];
	snprintf(fix_sized, sizeof(fix_sized), "%.2f", monitoring->osc_attributes.temperature);

	struct json_object* oscillator = json_object_new_object();
	json_object_object_add(oscillator, "model", json_object_new_string(monitoring->oscillator_model));
	json_object_object_add(oscillator, "fine_ctrl",
		json_object_new_int64(monitoring->ctrl_values.fine_ctrl == UINT32_MAX
			? (int64_t)-1 : (int64_t)monitoring->ctrl_values.fine_ctrl));
	json_object_object_add(oscillator, "coarse_ctrl",
		json_object_new_int64(monitoring->ctrl_values.coarse_ctrl == UINT32_MAX
			? (int64_t)-1 : (int64_t)monitoring->ctrl_values.coarse_ctrl));
	json_object_object_add(oscillator, "lock", json_object_new_boolean(monitoring->osc_attributes.locked));
	json_object_object_add(oscillator, "temperature", json_object_new_double_s(monitoring->osc_attributes.temperature, fix_sized));

	json_object_object_add(resp, "oscillator", oscillator);
}

/**
 * @brief Add GNSS data to json response. Must be called under gnss_info.lock locked
 *
 * @param resp
 * @param monitoring
 */
static void json_add_gnss_data(struct json_object *resp, struct monitoring *monitoring)
{
	struct json_object *gnss = json_object_new_object();
	json_object_object_add(gnss, "fix",
		json_object_new_int(monitoring->gnss_info.fix));
	json_object_object_add(gnss, "fixOk",
		json_object_new_boolean(monitoring->gnss_info.fixOk));
	json_object_object_add(gnss, "antenna_power",
		json_object_new_int(monitoring->gnss_info.antenna_power));
	json_object_object_add(gnss, "antenna_status",
		json_object_new_int(monitoring->gnss_info.antenna_status));
	json_object_object_add(gnss, "lsChange",
		json_object_new_int(monitoring->gnss_info.lsChange));
	json_object_object_add(gnss, "leap_seconds",
		json_object_new_int(monitoring->gnss_info.leap_seconds));
	json_object_object_add(gnss, "satellites_count",
		json_object_new_int(monitoring->gnss_info.satellites_count));
	json_object_object_add(gnss, "survey_in_position_error",
		json_object_new_double(monitoring->gnss_info.survey_in_position_error));
	json_object_object_add(gnss, "time_accuracy",
		json_object_new_int64(monitoring->gnss_info.time_accuracy));

	json_object_object_add(resp, "gnss", gnss);
}

/**
 * @brief Analyse request and send response
 *
 * @param sockfd socket file descriptor
 * @param monitoring monitoring struct pointer
 * @return fd_status_t
 */
static fd_status_t on_peer_ready_send(int sockfd, struct monitoring * monitoring) {
	enum monitoring_request request_type = REQUEST_NONE;
	struct json_object *json_req;
	struct json_object *json_token;
	struct json_object *json_resp;
	const char *request_token = NULL;
	bool authorized;
	int ret;

	assert(sockfd < MAXFDS);
	peer_state_t* peerstate = &global_state[sockfd];

	// json_tokener_parse will() return NULL if the JSON is invalid. Otherwise, it will call
	// json_object_new_object() and put the reference count of the object to 1. The object
	// must be freed by json_object_put() manually.
	struct json_object *obj = json_tokener_parse(peerstate->recv_buf);
	memset(peerstate->recv_buf, 0, SENDBUF_SIZE);
	if (!obj) {
		log_error("Monitoring: Error parsing request");
		return fd_status_W;
	}

	if (!json_object_object_get_ex(obj, "request", &json_req)) {
		json_object_put(obj);
		log_error("Monitoring: request field is missing");
		return fd_status_W;
	}
	// According to the doc "No reference counts will be changed.
	// There is no need to manually adjust reference counts through the json_object_put/json_object_get methods"

	request_type = (enum monitoring_request) json_object_get_int(json_req);
	if (json_object_object_get_ex(obj, "token", &json_token))
		request_token = json_object_get_string(json_token);
	authorized = monitoring->allow_control &&
		(monitoring->control_token == NULL ||
		 monitoring->control_token[0] == '\0' ||
		 (request_token != NULL &&
		  strcmp(request_token, monitoring->control_token) == 0));
	// json request object is not used after this point, so we can free it
	json_object_put(obj);

	json_resp = json_object_new_object();
	json_object_object_add(json_resp, "service",
		json_object_new_string("oscillatord"));
	json_object_object_add(json_resp, "version",
		json_object_new_string(PACKAGE_VERSION));
	json_object_object_add(json_resp, "protocol_version",
		json_object_new_int(1));
	json_object_object_add(json_resp, "control_enabled",
		json_object_new_boolean(monitoring->allow_control));

	/* Notify main loop about the request */
	pthread_mutex_lock(&monitoring->mutex);

	json_handle_request(monitoring, request_type, authorized,
		&monitoring->request, json_resp);

	if (monitoring->disciplining_mode || monitoring->phase_error_supported)
		json_add_disciplining_data(json_resp, monitoring);

	json_add_clock_data(json_resp, monitoring);
	json_add_oscillator_data(json_resp, monitoring);

	pthread_mutex_unlock(&monitoring->mutex);

	pthread_mutex_lock(&monitoring->gnss_info.lock);
	json_add_gnss_data(json_resp, monitoring);
	pthread_mutex_unlock(&monitoring->gnss_info.lock);

	const char *resp = json_object_to_json_string(json_resp);
	ret = send(sockfd, resp, strlen(resp), 0);
	// json_resp and it's string representation are not used after this point
	// so we can free them. All embedded objects are also freed because of transfer
	// of ownership to the outer object with json_object_object_add().
	json_object_put(json_resp);
	if (ret == -1) {
		log_error("Monitoring: Error sending response: %d", ret);
		return fd_status_W;

	} else {
		// Everything was sent successfully; reset the send queue.
		peerstate->buf_ptr = 0;
		peerstate->buf_end = 0;

		// Special-case state transition in if we were in INITIAL_ACK until now.
		if (peerstate->state == INITIAL_ACK) {
			peerstate->state = WAIT_FOR_MSG;
		}

		return fd_status_R;
	}
}

/**
 * @brief Create monitoring structure from config
 *
 * @param config
 * @return struct monitoring*
 */
struct monitoring* monitoring_init(const struct config *config, struct devices_path *devices_path)
{
	int                ret;
	struct monitoring* monitoring;
	const char*        address;
	const char*        port;

	if (devices_path == NULL) {
		log_error("No struct devices path passed !");
		return NULL;
	}

	address = config_get_default(config, "socket-address", "127.0.0.1");

	port = config_get_default(config, "socket-port", "2958");

	monitoring = (struct monitoring*)calloc(1, sizeof(struct monitoring));
	if (monitoring == NULL)
	{
		log_error("Monitoring: Could not allocate memory for monitoring struct");
		return NULL;
	}

	const char *oscillator_model = config_get(config, "oscillator");
	if (oscillator_model == NULL) {
		ret = errno;
		log_error("Monitoring: Configuration \"%s\" doesn't have an oscillator entry.",
				config->path);
		free(monitoring);
		errno = ret;
		return NULL;
	}
	monitoring->oscillator_model = strdup(oscillator_model);
	if (monitoring->oscillator_model == NULL) {
		free(monitoring);
		return NULL;
	}

	monitoring->stop = false;
	monitoring->disciplining_mode = config_get_bool_default(config, "disciplining", false);
	monitoring->phase_error_supported = false;
	monitoring->request = REQUEST_NONE;
	monitoring->allow_control = config_get_bool_default(config,
		"monitoring-allow-control", false);
	const char *control_token = config_get(config, "monitoring-control-token");
	monitoring->control_token = control_token == NULL ? NULL : strdup(control_token);
	if (control_token != NULL && monitoring->control_token == NULL) {
		free(monitoring->oscillator_model);
		free(monitoring);
		return NULL;
	}
	if (monitoring->allow_control &&
		(monitoring->control_token == NULL || monitoring->control_token[0] == '\0'))
		log_warn("Monitoring control is enabled without a token; use only on a trusted loopback endpoint");
	memcpy(&monitoring->devices_path, devices_path, sizeof(struct devices_path));

	monitoring->disciplining.clock_class = CLOCK_CLASS_UNCALIBRATED;
	monitoring->disciplining.status = WARMUP;
	monitoring->disciplining.current_phase_convergence_count = -1;
	monitoring->disciplining.valid_phase_convergence_threshold = -1;
	monitoring->disciplining.convergence_progress = 0.00;
	monitoring->disciplining.ready_for_holdover = false;
	monitoring->ctrl_values.fine_ctrl = -1;
	monitoring->ctrl_values.coarse_ctrl = -1;
	monitoring->osc_attributes.locked = false;
	monitoring->osc_attributes.temperature = -400.0;
	monitoring->osc_attributes.phase_error = 0;

	monitoring->gnss_info.antenna_power = -1;
	monitoring->gnss_info.antenna_status = -1;
	monitoring->gnss_info.leap_seconds = -1;
	monitoring->gnss_info.fix = -1;
	monitoring->gnss_info.fixOk = false;
	monitoring->gnss_info.lsChange = -10;
	monitoring->gnss_info.satellites_count = -1;
	monitoring->gnss_info.survey_in_position_error = -1.0;
	monitoring->gnss_info.time_accuracy = -1;
	pthread_mutex_init(&monitoring->gnss_info.lock, NULL);

	pthread_mutex_init(&monitoring->mutex, NULL);
	pthread_cond_init(&monitoring->cond, NULL);

	monitoring->sockfd = create_socket(address, port);
	if (monitoring->sockfd == -1)
	{
		log_error("Monitoring: Error creating monitoring socket");
		free(monitoring->control_token);
		free(monitoring->oscillator_model);
		free(monitoring);
		return NULL;
	}
	make_socket_non_blocking(monitoring->sockfd);

	ret = pthread_create(
		&monitoring->thread,
		NULL,
		monitoring_thread,
		monitoring
	);

	log_info("Monitoring: INITIALIZATION: Successfully started monitoring thread, listening on %s:%s", address, port);
	if (ret != 0)
	{
		log_error("Monitoring: Error creating monitoring thread: %d", ret);
		close(monitoring->sockfd);
		free(monitoring->control_token);
		free(monitoring->oscillator_model);
		free(monitoring);
		return NULL;
	}
	return monitoring;
}

/**
 * @brief Stop monitoring thread
 *
 * @param monitoring
 */
void monitoring_stop(struct monitoring *monitoring)
{
	if (monitoring == NULL)
		return;
	pthread_mutex_lock(&monitoring->mutex);
	monitoring->stop = true;
	pthread_cond_signal(&monitoring->cond);
	pthread_mutex_unlock(&monitoring->mutex);
	pthread_join(monitoring->thread, NULL);
	close(monitoring->sockfd);
	free(monitoring->control_token);
	free(monitoring->oscillator_model);
	free(monitoring);
	return;
}

/**
 * @brief Monitoring thread routine
 *
 * @param p_data
 * @return void*
 */
static void *monitoring_thread(void * p_data)
{
	struct monitoring *monitoring;
	bool stop;

	monitoring = (struct monitoring*) p_data;
	stop = monitoring->stop;

	int epollfd = epoll_create1(0);
	if (epollfd < 0) {
		log_error("epoll_create1");
		return NULL;
	}

	struct epoll_event accept_event;
	accept_event.data.fd = monitoring->sockfd;
	accept_event.events = EPOLLIN;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, monitoring->sockfd, &accept_event) < 0) {
		log_error("epoll_ctl EPOLL_CTL_ADD");
		return NULL;
	}

	struct epoll_event* events = calloc(MAXFDS, sizeof(struct epoll_event));
	if (events == NULL) {
		log_error("Unable to allocate memory for epoll_events");
		return NULL;
	}

	while (!stop)
	{
		int nready = epoll_wait(epollfd, events, MAXFDS, SOCKET_TIMEOUT_MS);
		for (int i = 0; i < nready; i++) {
			if (events[i].events & EPOLLERR) {
				log_error("received EPOLLERR");
				log_debug("socket %d closing", events[i].data.fd);
				if (epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, NULL) < 0) {
					log_error("epoll_ctl EPOLL_CTL_DEL");
					return NULL;
				}
				close(events[i].data.fd);
				continue;
			}

			if (events[i].data.fd == monitoring->sockfd) {
				// The listening socket is ready; this means a new peer is connecting.

				struct sockaddr_in peer_addr;
				socklen_t peer_addr_len = sizeof(peer_addr);
				int newsockfd = accept(monitoring->sockfd, (struct sockaddr*)&peer_addr,
									&peer_addr_len);
				if (newsockfd < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						// This can happen due to the nonblocking socket mode; in this
						// case don't do anything, but print a notice (since these events
						// are extremely rare and interesting to observe...)
						log_debug("accept returned EAGAIN or EWOULDBLOCK");
					} else {
						log_error("accept");
						return NULL;
					}
				} else {
					make_socket_non_blocking(newsockfd);
					if (newsockfd >= MAXFDS) {
						log_error("socket fd (%d) >= MAXFDS (%d)", newsockfd, MAXFDS);
						return NULL;
					}

					fd_status_t status =
						on_peer_connected(newsockfd, &peer_addr, peer_addr_len);
					struct epoll_event event = {0};
					event.data.fd = newsockfd;
					if (status.want_read) {
						event.events |= EPOLLIN;
					}
					if (status.want_write) {
						event.events |= EPOLLOUT;
					}

					if (epoll_ctl(epollfd, EPOLL_CTL_ADD, newsockfd, &event) < 0) {
						log_error("epoll_ctl EPOLL_CTL_ADD");
						return NULL;
					}
				}
			} else {
				// A peer socket is ready.
				if (events[i].events & EPOLLIN) {
					// Ready for reading.
					int fd = events[i].data.fd;
					fd_status_t status = on_peer_ready_recv(fd);
					struct epoll_event event = {0};
					event.data.fd = fd;
					if (status.want_read) {
						event.events |= EPOLLIN;
					}
					if (status.want_write) {
						event.events |= EPOLLOUT;
					}
					if (event.events == 0) {
						log_trace("socket %d closing", fd);
						if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
							log_error("epoll_ctl EPOLL_CTL_DEL");
							return NULL;
						}
						close(fd);
					} else if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0) {
						log_error("epoll_ctl EPOLL_CTL_MOD");
						return NULL;
					}
				} else if (events[i].events & EPOLLOUT) {
					// Ready for writing.
					int fd = events[i].data.fd;
					fd_status_t status = on_peer_ready_send(fd, monitoring);
					struct epoll_event event = {0};
					event.data.fd = fd;

					if (status.want_read) {
						event.events |= EPOLLIN;
					}
					if (status.want_write) {
						event.events |= EPOLLOUT;
					}
					if (event.events == 0) {
						log_trace("socket %d closing", fd);
						if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
							log_error("epoll_ctl EPOLL_CTL_DEL");
							return NULL;
						}
						close(fd);
					} else if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0) {
						log_error("epoll_ctl EPOLL_CTL_MOD");
						return NULL;
					}
				}
			}
		}
		pthread_mutex_lock(&monitoring->mutex);
		stop = monitoring->stop;
		pthread_mutex_unlock(&monitoring->mutex);

	}
	log_info("Monitoring: Exiting thread");

	return NULL;
}
