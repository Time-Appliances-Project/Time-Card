#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <ff/ff_epoch.h>
#include <ff/ff_rx.h>
#include <ff/ff_ubx.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>

#include "utils.h"
#include "gnss.h"
#include "gnss-config.h"
#include "log.h"
#include "f9_defvalsets.h"

#define GNSS_TIMEOUT_MS 1500
#define GNSS_TEST_TIMEOUT 120
#define GNSS_RECONFIGURE_MAX_TRY 5
#define ARRAY_SIZE(_A) (sizeof(_A) / sizeof((_A)[0]))

int main(int argc, char *argv[])
{
    char ocp_path[256] = "";
    char gnss_path[256] = "";
    bool gnss_path_valid;
    bool ocp_path_valid;

    bool got_gnss_fix = false;
    bool got_mon_rf_message = false;
    bool got_nav_timels_message = false;
    bool got_tim_tp_message = false;

	/* Set log level */
	log_set_level(1);

	log_info("Checking input:");
    snprintf(ocp_path, sizeof(ocp_path) - 1, "/sys/class/timecard/%s", argv[1]);

	log_info("\t-ocp path is: \"%s\", checking...", ocp_path);
	if (access(ocp_path, F_OK) != -1)
	{
		ocp_path_valid = true;
        log_info("\t\tocp path exists !");
    }
	else
	{
		ocp_path_valid = false;
        log_info("\t\tocp path doesn't exists !");
    }

    if (ocp_path_valid)
    {
        log_info("\t-sysfs path %s", ocp_path);

        DIR* ocp_dir = opendir(ocp_path);
        struct dirent * entry = readdir(ocp_dir);
        while (entry != NULL)
        {
            if (strcmp(entry->d_name, "ttyGNSS") == 0)
                {
                    find_dev_path(ocp_path, entry, gnss_path);
                    log_info("\t-ttyGPS detected: %s", gnss_path);
                    gnss_path_valid = true;
                }
            entry = readdir(ocp_dir);
        }
    }

    if (gnss_path_valid)
    {
        RX_OPTS_t opts = RX_OPTS_DEFAULT();
        opts.autobaud = true;
        opts.detect = RX_DET_UBX;
        log_info("Path is %s", gnss_path);
        RX_t * rx = rxInit(gnss_path, &opts);

        if (!rxOpen(rx)) {
            free(rx);
            log_error("\t- Gnss rx init failed");
            return false;
        }

        EPOCH_t coll;
        EPOCH_t epoch;

        epochInit(&coll);

        time_t timeout = time(NULL) + GNSS_TEST_TIMEOUT;
        while ((!got_gnss_fix || !got_mon_rf_message || !got_nav_timels_message || !got_tim_tp_message) && time(NULL) < timeout)
        {
            PARSER_MSG_t *msg = rxGetNextMessageTimeout(rx, GNSS_TIMEOUT_MS);
            if (msg != NULL)
            {
                if(epochCollect(&coll, msg, &epoch))
                {
                    if(epoch.haveFix && epoch.fixOk && (epoch.fix >= EPOCH_FIX_S3D || epoch.fix == EPOCH_FIX_TIME) && !got_gnss_fix) {
                        log_info("\t- Got fix !");
                        got_gnss_fix = true;
                    }
                }
                else
                {
                    uint8_t clsId = UBX_CLSID(msg->data);
                    uint8_t msgId = UBX_MSGID(msg->data);
                    if (clsId == UBX_MON_CLSID && msgId == UBX_MON_RF_MSGID && !got_mon_rf_message) {
                        log_info("\t- Got UBX-MON-RF message");
                        got_mon_rf_message = true;
                    } else if (clsId == UBX_NAV_CLSID && msgId == UBX_NAV_TIMELS_MSGID && !got_nav_timels_message) {
                        log_info("\t- Got UBX-NAV-TIMELS message");
                        got_nav_timels_message = true;
                    } else if (clsId == UBX_TIM_CLSID && msgId == UBX_TIM_TP_MSGID && !got_tim_tp_message) {
                        log_info("\t- Got UBX-TIM-TP message");
                        got_tim_tp_message = true;
                    }
                }
            }
            else
            {
                log_warn("GNSS: UART Timeout !");
                usleep(5 * 1000);
            }
        }

        rxClose(rx);
        free(rx);

        if (got_gnss_fix && got_mon_rf_message && got_nav_timels_message && got_tim_tp_message)
        {
            log_info("GNSS Test Passed");
            return true;
        }
        else
        {
            log_warn("GNSS Test Failed");
        }
    }
    else
    {
        log_warn("GNSS Test Aborted");
    }

}
