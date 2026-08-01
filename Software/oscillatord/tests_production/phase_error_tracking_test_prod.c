#include <arpa/inet.h>
#include <json-c/json.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include "config.h"
#include "log.h"

#define PHASE_ERROR_ABS_MAX 100
#define PHASE_ERROR_TRACKING_TIME_MIN 1

enum monitoring_request {
    REQUEST_NONE,
    REQUEST_CALIBRATION,
};

static void oscillatord_activate_service(char * template_name, bool on)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    sd_bus *bus = NULL;
    const char *path;
    char service_name[256];
    int r;

    sprintf(service_name, "oscillatord@%s.service", template_name);

    /* Connect to the system bus */
    r = sd_bus_open_system(&bus);
    if (r < 0) {
        log_error("Failed to connect to system bus: %s\n", strerror(-r));
    }

    /* Issue the method call and store the respons message in m */
    r = sd_bus_call_method(
        bus,
        "org.freedesktop.systemd1",           /* service to contact */
        "/org/freedesktop/systemd1",          /* object path */
        "org.freedesktop.systemd1.Manager",   /* interface name */
        on ? "StartUnit" : "StopUnit",                          /* method name */
        &error,                               /* object to return error in */
        &m,                                   /* return message on success */
        "ss",                                 /* input signature */
        service_name,                         /* first argument */
        "replace"                             /* second argument */
    );
    if (r < 0) {
        log_error("Failed to issue method call: %s\n", error.message);
    }

    /* Parse the response message */
    r = sd_bus_message_read(m, "o", &path);
    if (r < 0) {
        log_error("Failed to parse response message: %s\n", strerror(-r));
    }

}

static void oscillatord_start_service(char * template_name)
{
    oscillatord_activate_service(template_name, true);
    log_info("Started oscillatord@%s service", template_name);
}

static void oscillatord_stop_service(char * template_name)
{
    oscillatord_activate_service(template_name, false);
    log_info("Stopped oscillatord@%s service", template_name);
}

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


static struct json_object *send_monitoring_request(int socket_port, enum monitoring_request request) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        log_error("Could not connect to socket !");
        log_error("FAIL");
        return NULL;
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(socket_port);
    server_addr.sin_addr.s_addr = inet_addr("0.0.0.0");

    /* Initiate a connection to the server */
    int ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1)
    {
        log_error("Could not connect to socket !");
        log_error("FAIL");
        return NULL;
    }
    struct json_object *obj = json_send_and_receive(sockfd, request);
    log_info(json_object_to_json_string(obj));
    close(sockfd);
    return obj;

}

enum track_phase_error_test_state {
    WAITING_DISCIPLINING,
    TRACKING_PHASE_ERROR,
    PASSED,
    FAILED,
};

static bool oscillatord_track_phase_error_under_limit(int socket_port, int phase_error_abs_limit, int test_time_minutes)
{
    enum track_phase_error_test_state state = WAITING_DISCIPLINING;
    time_t start_test_time;
    time_t elapsed_time;
    while (state != PASSED && state != FAILED) {

        /* REQUEST PHASE ERROR */
        struct json_object *obj = send_monitoring_request(socket_port, REQUEST_NONE);
        struct json_object *layer_1, *layer_2, *layer_3;

        /* Disciplining */
        json_object_object_get_ex(obj, "disciplining", &layer_1);
        json_object_object_get_ex(obj, "clock", &layer_3);
        if (layer_1 != NULL && layer_3 != NULL) {
            json_object_object_get_ex(layer_1, "status", &layer_2);
            const char *status = json_object_get_string(layer_2);
            json_object_object_get_ex(layer_3, "offset", &layer_2);
            int phase_error = json_object_get_int(layer_2);

            switch (state) {
                case WAITING_DISCIPLINING:
                    /* Phase 1: Detect that oscillatord is disciplining and phase error is
                     * below acceptable limits to start test
                     */
                    if (strncmp(status, "TRACKING", sizeof("TRACKING")) == 0
                        && abs(phase_error) <= phase_error_abs_limit) {
                        log_info("Phase 1 PASSED: Oscillatord is now disciplining and below acceptable phase error range,"
                            " starting phase error tracking to check it stays below the limit ");
                        time(&start_test_time);
                        state = TRACKING_PHASE_ERROR;
                    }
                    break;
                case TRACKING_PHASE_ERROR:
                    log_info("Phase error: %d", phase_error);
                    /* Phase 2: Check that phase error stays below absolute limit for a period of time */
                    if (strncmp(status, "TRACKING", sizeof("TRACKING")) == 0) {
                        if (abs(phase_error) <= phase_error_abs_limit) {
                            time(&elapsed_time);
                            if ((int) difftime(elapsed_time, start_test_time) >= test_time_minutes * 60) {
                                log_info("Phase 2 PASSED: Phase error stayed below the limit for %d time", test_time_minutes);
                                log_info("Test Passed");
                                state = PASSED;
                            }
                        } else {
                            log_error("Phase 2 FAILED: Phase error reached absolute limit specified");
                            log_error("Limit is %d and phase error is %d", phase_error_abs_limit, phase_error);
                            log_error("Test Failed");
                            state = FAILED;
                        }
                    } else {
                        log_error("Card switched to %s, test cannot continue", status);
                        state = FAILED;
                    }
                    break;
                case PASSED:
                    /* Test PASSED */
                    break;
                case FAILED:
                    /* Test FAILED */
                    break;
                default:
                    log_error("Unknown state %d, aborting test", state);
                    state = FAILED;
                    break;
            }

        } else {
            log_error("Could not get disciplining data");
            break;
        }

        free(obj);
        sleep(5);
    }

    return state == PASSED;
}

int main(int argc, char *argv[])
{
    char config_file_path[256] = "";
    char ocp_path[256] = "";
	bool config_path_valid;
	bool ocp_path_valid;

	/* Set log level */
	log_set_level(0);
    snprintf(config_file_path, sizeof(config_file_path), "/etc/oscillatord_%s.conf", argv[1]);
    snprintf(ocp_path, sizeof(ocp_path), "/sys/class/timecard/%s", argv[1]);

	log_info("Checking input:");

	log_info("\t-oscillatord config file path is: \"%s\", checking...",config_file_path);
	if (access(config_file_path, F_OK) != -1)
	{
		config_path_valid = true;
        log_info("\t\tconfig file exists !");
    }
	else
	{
		config_path_valid = false;
        log_info("\t\tconfig file doesn't exists !");
    }

	log_info("\t-ocp driver path is: \"%s\", checking...",config_file_path);
	if (access(config_file_path, F_OK) != -1)
	{
		ocp_path_valid = true;
        log_info("\t\tocp driver exists !");
    }
	else
	{
		ocp_path_valid = false;
        log_info("\t\tocp driver doesn't exists !");
    }

	if (config_path_valid && ocp_path_valid && (argc == 3) && (strlen(argv[2])==4))
    {
        int socket_port;
        log_info("Starting Phase error limit test");
        const char * socket_port_string = argv[2];
        if (!socket_port_string) 
        {
            log_warn("Phase error tracking Test Aborted: socket port is not set in config !");
        }
        else
        {
            socket_port = atoi(socket_port_string);
            oscillatord_start_service(argv[1]);
            sleep(60);
            if (oscillatord_track_phase_error_under_limit(socket_port, PHASE_ERROR_ABS_MAX, PHASE_ERROR_TRACKING_TIME_MIN)) 
            {
                log_info("ART Card ran without reaching phase error limit");
                log_info("Test PASSED !");
            } 
            else
            {
                log_warn("Card did not passed phase error limit test even after calibration");
                log_warn("Test FAILED !");
            }
            oscillatord_stop_service(argv[1]);
        }
    }
    else
    {
        log_warn("Phase error tracking Test Aborted: invalid path or port!");
    }
}
