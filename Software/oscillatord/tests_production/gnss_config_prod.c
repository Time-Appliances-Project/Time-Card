#include <stdlib.h>
#include <unistd.h>
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
#define GNSS_TEST_TIMEOUT 600
#define ARRAY_SIZE(_A) (sizeof(_A) / sizeof((_A)[0]))

int main(int argc, char *argv[])
{
    char ocp_path[256] = "";
    char gnss_path[256] = "";
    bool gnss_path_valid;
    bool ocp_path_valid;

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

        /* Fetch receiver version */
        char verStr[100];
        int major, minor;

        if (rxGetVerStr(rx, verStr, sizeof(verStr)))
        {
            if (parse_receiver_version(verStr, &major, &minor))
                log_debug("Receiver version successfully detected ! Major is %d, Minor is %d ", major, minor);
            else
                log_warn("Receiver version parsing failed");
        }
        else
            log_warn("Receiver version get command failed");

        /* Get configuration */
        int nAllKvCfg;
        UBLOXCFG_KEYVAL_t *allKvCfg = get_default_value_from_config(&nAllKvCfg, major, minor, GNSS_BAND_L1 | GNSS_BAND_L2);

        log_info("Configuring receiver with ART parameters...");
        bool res = rxSetConfig(rx, allKvCfg, nAllKvCfg, true, true, true);
        if (res)
        {
            rxClose(rx);
            if (!rxOpen(rx))
            {
                free(rx);
                log_warn("GNSS rx init failed");
            }

            log_info("Successfully reconfigured GNSS receiver");
            log_debug("Performing Hardware reset");
            if (!rxReset(rx, RX_RESET_HARD))
            {
                free(allKvCfg);
                log_warn("GNSS receiver reset error");
            }
            else
            {
                log_info("Hardware reset performed");
            }
        }
        else
        {
            log_warn("GNSS receiver configuration failed");
        }

        log_info("Gnss receiver configuration routine done");
        rxClose(rx);
        free(rx);
        free(allKvCfg);
    }
    else
    {
        log_warn("GNSS Config Aborted");
    }
}
