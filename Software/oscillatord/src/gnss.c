#include <errno.h>
#include <error.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timex.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <ff/ff_epoch.h>
#include <ff/ff_parser.h>
#include <ff/ff_rx.h>
#include <ff/ff_stuff.h>
#include <ff/ff_ubx.h>
#include <ubloxcfg/ubloxcfg.h>

#include "gnss.h"
#include "gnss-config.h"
#include "log.h"
#include "utils.h"
#include "f9_defvalsets.h"

#define NUM_SAT_MIN 3

#define GNSS_CONNECT_MAX_TRY 5

#define GNSS_TIMEOUT_MS 1200
#define GNSS_RECONFIGURE_MAX_TRY 5
#define SEC_IN_WEEK 604800

#define GPS_EPOCH_TO_TAI 315964819

#define GAL_EPOCH_TO_GPS 619315200
#define GAL_EPOCH_TO_TAI GAL_EPOCH_TO_GPS + GPS_EPOCH_TO_TAI

#define BDS_EPOCH_TO_GPS 820108814
#define BDS_EPOCH_TO_TAI BDS_EPOCH_TO_GPS + GPS_EPOCH_TO_TAI

#define GLO_EPOCH_TO_TAI 315954019

#define ARRAY_SIZE(_A) (sizeof(_A) / sizeof((_A)[0]))

/** Survey In min duration (s) */
#define SVIN_MIN_DUR 1200
/** Survey In max duration allowed (s) */
#define SVIN_MAX_DUR SVIN_MIN_DUR + 600

#ifndef UBX_TIM_TM2_V0_SIZE
#define UBX_TIM_TP_V0_GROUP0_t UBX_TIME_TP_V0_GROUP0_t
#define UBX_TIM_SVIN_V0_GROUP0_t UBX_TIME_SVIN_V0_GROUP0_t
#endif

#ifndef FLAG
#define FLAG(field, flag) ( ((field) & (flag)) == (flag) )
#endif

#define RTCM_SOCK_PATH "/run/oscillatord/rtcm.sock"

/** The resolution of our phasemeter in pico seconds */
#define QERR_ABS_THRESHOLD_PS 5000

enum AntennaStatus {
	ANT_STATUS_INIT,
	ANT_STATUS_DONT_KNOW,
	ANT_STATUS_OK,
	ANT_STATUS_SHORT,
	ANT_STATUS_OPEN,
	ANT_STATUS_UNDEFINED
};

enum AntennaPower {
	ANT_POWER_OFF,
	ANT_POWER_ON,
	ANT_POWER_DONTKNOW,
	ANT_POWER_IDLE,
	ANT_POWER_UNDEFINED
};

enum SurveyInState {
	SURVEY_IN_KO,
	SURVEY_IN_UNKNOWN,
	SURVEY_IN_IN_PROGRESS,
	SURVEY_IN_COMPLETED
};

#define NO_FIX 1

static const char *fix_log[11] = {
	"unknown",
	"no fix",
	"DR only",
	"time",
	"2D",
	"3D",
	"3D_DR",
	"RTK_FLOAT",
	"RTK_FIXED",
	"RTK_FLOAT_DR",
	"RTK_FIXED_DR",
};

static void * gnss_thread(void * p_data);

static int gnss_get_satellites(EPOCH_t *epoch)
{
	if (epoch->haveNumSv) {
		return epoch->numSv;
	}
	return 0;
}

static void gnss_reset_session_navigation_data(struct gps_device_t *session)
{
	session->valid = false;
	session->satellites_count = 0;
	session->fix = NO_FIX;
	session->fixOk = false;
}

/**
 * @brief Convert time from epoch to UTC time
 *
 * @param epoch
 * @return time_t
 */
static time_t gnss_get_utc_time(EPOCH_t *epoch)
{
	struct tm t = {
		// Year - 1900
		.tm_year = epoch->year - 1900,
		// Month, where 0 = jan. libublox starts indexing by 1
		.tm_mon = epoch->month - 1,
		// Day of the month
		.tm_mday = epoch->day,
		.tm_hour = epoch->hour,
		.tm_min = epoch->minute,
		.tm_sec = (int) round(epoch->second),
		// Is DST on? 1 = yes, 0 = no, -1 = unknown
		.tm_isdst = -1
	};
	time_t time = mktime(&t);
	/* Temporary solution to get UTC time as mktime converts
	* considering time provided is a local time
	*/
# ifdef	__USE_MISC
	time =  time + localtime(&time)->tm_gmtoff;
# else
	time =  time + localtime(&time)->__tm_gmtoff;
# endif
	return time;
}

/**
 * @brief Parse UBX-NAV-TIMELS msg to get leap second data
 *
 * @param session gps device data of the session
 * @param msg msg received from the receiver
 */
static void gnss_parse_ubx_nav_timels(struct gps_device_t *session, PARSER_MSG_t *msg)
{
	UBX_NAV_TIMELS_V0_GROUP0_t nav_timels_msg;
	if (msg->size == UBX_NAV_TIMELS_V0_SIZE) {
		memcpy(&nav_timels_msg, &msg->data[UBX_HEAD_SIZE], sizeof(nav_timels_msg));

		session->context->leap_seconds =
			FLAG(nav_timels_msg.valid, UBX_NAV_TIMELS_V0_VALID_CURRLSVALID) ?
			nav_timels_msg.currLs :
			0;
		session->context->lsset = FLAG(nav_timels_msg.valid, UBX_NAV_TIMELS_V0_VALID_CURRLSVALID);

		if (FLAG(nav_timels_msg.valid, UBX_NAV_TIMELS_V0_VALID_TIMETOLSEVENTVALID))
		{
			session->context->timeToLsEvent = nav_timels_msg.timeToLsEvent;
			session->context->lsChange = nav_timels_msg.lsChange;

			if ((0 != session->context->lsChange) &&
				(0 < session->context->timeToLsEvent) &&
				((60 * 60 * 23) > session->context->timeToLsEvent)) {
				if (1 == session->context->lsChange) {
					session->context->leap_notify = LEAP_ADDSECOND;
				} else if (-1 == session->context->lsChange) {
					session->context->leap_notify = LEAP_DELSECOND;
				}
			} else {
				session->context->leap_notify = LEAP_NOWARNING;
			}
			return;
		}
	}
	session->context->timeToLsEvent = 0;
	session->context->lsChange = 0;
	session->context->leap_notify = LEAP_NOWARNING;
};

/**
 * @brief Parse UBX-TIM-TP msg to get time from a constellation or UTC time and compute TAR
 *
 * @param session gps device data of the session
 * @param msg msg received from the receiver
 */
static void gnss_parse_ubx_tim_tp(struct gps_device_t *session, PARSER_MSG_t *msg) {
	if (msg->size == (int) UBX_TIM_TP_V0_SIZE) {
		UBX_TIM_TP_V0_GROUP0_t gr0;
		memcpy(&gr0, &msg->data[UBX_HEAD_SIZE], sizeof(gr0));

		if (abs(gr0.qErr) > QERR_ABS_THRESHOLD_PS)
		{
			log_warn("Aberrant qErr of %i will be ignored", gr0.qErr);
			gr0.qErr = 0;
		}

		log_trace("UBX-TIM-TP: towMS %u, towSubMs %u, qErr %i, week %hu, flags %#hhx; refInfo %#hhx",
		          gr0.towMs,
		          gr0.towSubMs,
		          gr0.qErr,
		          gr0.week,
		          gr0.flags,
		          gr0.refInfo);

		int offset = 0;
		if (UBX_TIM_TP_V0_FLAGS_TIMEBASE_GET(gr0.flags) == UBX_TIM_TP_V0_FLAGS_TIMEBASE_GNSS) {
			switch(UBX_TIM_TP_V0_REFINFO_TIMEREFGNSS_GET(gr0.refInfo)) {
				case UBX_TIM_TP_V0_REFINFO_TIMEREFGNSS_GPS:
					offset = GPS_EPOCH_TO_TAI;
					break;
				case UBX_TIM_TP_V0_REFINFO_TIMEREFGNSS_BDS:
					offset = BDS_EPOCH_TO_TAI;
					break;
				case UBX_TIM_TP_V0_REFINFO_TIMEREFGNSS_GAL:
					offset = GAL_EPOCH_TO_TAI;
					break;
				case UBX_TIM_TP_V0_REFINFO_TIMEREFGNSS_GLO:
					if (session->context->lsset) {
						offset = GLO_EPOCH_TO_TAI + session->context->leap_seconds;
					} else {
						log_warn("Cannot compute TAI time from GLONASS without leap second information. Waiting for leap second data");
						return;
					}
					break;
				default:
					log_error("Unhandled Constellations %d", UBX_TIM_TP_V0_REFINFO_TIMEREFGNSS_GET(gr0.refInfo));
					return;
			}
		} else if (UBX_TIM_TP_V0_FLAGS_TIMEBASE_GET(gr0.flags) == UBX_TIM_TP_V0_FLAGS_TIMEBASE_UTC) {
			if (session->context->lsset) {
				offset = GPS_EPOCH_TO_TAI + session->context->leap_seconds;
			} else {
				log_warn("Cannot compute TAI time from UTC without leap second information. Waiting for leap second data");
				return;
			}
		}

		session->tai_time = (int) round(
			((double) gr0.towMs / 1000)
			+ ((double) gr0.week * SEC_IN_WEEK)
			+ offset
			- 1 // UBX-TIM-TP gives time at next pulse
		);
		/* Update quantization error and store quantization of last epoch */
		session->context->qErr_last_epoch = session->context->qErr;
		session->context->qErr = gr0.qErr;
		session->tai_time_set = true;
		return;
	}
}

/**
 * @brief Parse UBX-TIM-SVIN msg to get information about Survey In process
 *
 * @param session gps device data of the session
 * @param msg msg received from the receiver
 * @return enum survey_in_state
 */
static enum SurveyInState gnss_parse_ubx_tim_svin(struct gps_device_t *session, PARSER_MSG_t *msg) {
	if (msg->size == (int) UBX_TIM_SVIN_V0_SIZE) {
		UBX_TIM_SVIN_V0_GROUP0_t gr0;
		memcpy(&gr0, &msg->data[UBX_HEAD_SIZE], sizeof(gr0));
		if (gr0.active || !gr0.valid)
		{
			log_debug("UBX-TIM-SVIN: dur: %u, meanX %i, meanZ %i, meanZ %i, meanV %u, obs %u, valid %hhu, active %hhu",
			          gr0.dur,
			          gr0.meanX,
			          gr0.meanY,
			          gr0.meanZ,
			          gr0.meanV,
			          gr0.obs,
			          gr0.valid,
			          gr0.active);
		}
		session->survey_in_position_error = sqrt(gr0.meanV)/1000;
		if (!gr0.active && gr0.dur >= SVIN_MIN_DUR)
			return gr0.valid ? SURVEY_IN_COMPLETED : SURVEY_IN_KO;
		else if (gr0.dur < SVIN_MAX_DUR)
			return SURVEY_IN_IN_PROGRESS;
		else
			return SURVEY_IN_KO;
	}
	return SURVEY_IN_UNKNOWN;
}

/**
 * @brief Parse UBX-MON-RF msg to get antenna status data
 *
 * @param session gps device data of the session
 * @param msg msg received from the receiver
 */
static void gnss_get_antenna_data(struct gps_device_t *session, PARSER_MSG_t *msg)
{
	if (msg->size > (UBX_FRAME_SIZE + 4)) {
		if (msg->size >= (int) UBX_MON_RF_V0_MIN_SIZE) {
			int offs = UBX_HEAD_SIZE;
			UBX_MON_RF_V0_GROUP0_t gr0;
			memcpy(&gr0, &msg->data[offs], sizeof(gr0));
			offs += sizeof(gr0);

			// Reset antenna status and power
			session->antenna_status = 0x5; // Undefined value according to spec
			session->antenna_power = 0x5; // Undefined value according to spec

			UBX_MON_RF_V0_GROUP1_t gr1;
			while (offs <= (msg->size - 2 - (int)sizeof(gr1)))
			{
				memcpy(&gr1, &msg->data[offs], sizeof(gr1));
				// If we have multiple blocks and one is 0X2 (eq OK) and the next is not,
				// Take worst of values
				if (session->antenna_status == 0x5 || (session->antenna_status == 0x2 && gr1.antStatus != 0x2))
					session->antenna_status = gr1.antStatus;
				// Same behaviour but 0x2 means DONT_KNOW
				if (session->antenna_power == 0x5 || (session->antenna_power == 0x2 && gr1.antPower != 0x2))
					session->antenna_power = gr1.antPower;
				offs += sizeof(gr1);
			}
		}
	}
}

/* Per-band signal counts updated from UBX-RXM-RAWX every second */
static int rawx_sats_l1 = 0, rawx_sats_l2 = 0, rawx_sats_l5 = 0;

static void log_gnss_data(struct gps_device_t *session)
{
	log_debug("GNSS data: Fix %s (%d), Fix ok: %s, satellites num %d, signals L1=%d L2=%d L5=%d, survey in error: %0.2f, antenna status: %d, valid %d,"
		" time %lld, leapm_seconds %d, leap_notify %d, lsChange %d, "
		"timeToLsChange %d, lsSet: %s, QErr(n) %d, qErr(n-1) %d, pos_accuracy %d m, time_accuracy %d ns",
		fix_log[session->fix],
		session->fix,
		session->fixOk ? "True" : "False",
		session->satellites_count,
		rawx_sats_l1, rawx_sats_l2, rawx_sats_l5,
		session->survey_in_position_error,
		session->antenna_status,
		session->valid,
		session->last_fix_utc_time.tv_sec,
		session->context->leap_seconds,
		session->context->leap_notify,
		session->context->lsChange,
		session->context->timeToLsEvent,
		session->context->lsset ? "True" : "False",
		session->context->qErr,
		session->context->qErr_last_epoch,
		session->position_accuracy,
		session->time_accuracy
	);
}

/**
 * @brief Latch the fact that we've saved a fix and add in the device fudge
 *
 * @param device
 * @param td
 * Copied from GPSD
 */
static void ntp_latch(struct gps_device_t *device, struct timedelta_t *td)
{

    /* this should be an invariant of the way this function is called */
    if (0 >= device->last_fix_utc_time.tv_sec) {
        return;
    }

    (void)clock_gettime(CLOCK_REALTIME, &td->clock);
    /* structure copy of time from GPS */
    td->real = device->last_fix_utc_time;

    /* thread-safe update */
    pps_thread_fixin(&device->pps_thread, td);
}

/**
 * @brief Connect to serial device of the GNSS Receiver.
 * Try to connect a maximum of GNSS_CONNECT_MAX_TRY time
 *
 * @param rx pointer to serial communication handler
 * @return boolean indicating connection has been done or not
 */
static bool gnss_connect(RX_t *rx) {
	int tries = 0;
	while (tries < GNSS_CONNECT_MAX_TRY) {
		if (rxOpen(rx))
			return true;
		else
			usleep(5000);
		tries++;
	}
	return false;
}

static bool set_preferred_time_scale(UBLOXCFG_KEYVAL_t* keyValuePairs, size_t length, const struct config* config)
{
	const char* parameter_name          = "gnss-preferred-time-scale";
	const char* preferred_constellation = config_get(config, parameter_name);
	uint8_t     value;

	if (preferred_constellation == NULL)
		return false;
	else if (strncmp(preferred_constellation, "GPS", 3) == 0)
		value = UBLOXCFG_CFG_TP_TIMEGRID_TP1_GPS;
	else if (strncmp(preferred_constellation, "GAL", 3) == 0)
		value = UBLOXCFG_CFG_TP_TIMEGRID_TP1_GAL;
	else if (strncmp(preferred_constellation, "GLO", 3) == 0)
		value = UBLOXCFG_CFG_TP_TIMEGRID_TP1_GLO;
	else if (strncmp(preferred_constellation, "BDS", 3) == 0)
		value = UBLOXCFG_CFG_TP_TIMEGRID_TP1_BDS;
	else if (strncmp(preferred_constellation, "UTC", 3) == 0)
		value = UBLOXCFG_CFG_TP_TIMEGRID_TP1_UTC;
	else
	{
		log_error("Your configuration sets the parameter \"%s\" to \"%s\", which is not part of the possible values "
		          "(GPS, GAL, GLO, BDS, UTC)",
		          parameter_name,
		          preferred_constellation);
		return false;
	}
	log_info("Using custom value for \"%s\": %s", parameter_name, preferred_constellation);

	UBLOXCFG_KEYVAL_t* pair = keyValuePairs + length;
	while (pair --> keyValuePairs)
	{
		if (pair->id == UBLOXCFG_CFG_TP_TIMEGRID_TP1_ID)
		{
			pair->val.E1 = value;
			return true;
		}
	}
	return false;
}

static bool set_cable_delay(UBLOXCFG_KEYVAL_t* keyValuePairs, size_t length, const struct config* config)
{
	const char* parameter_name = "gnss-cable-delay";
	const char* string         = config_get(config, parameter_name);

	if (string == NULL)
		return false;

	errno = 0;
	char* end;
	long  value = strtol(string, &end, 0);

	if (!errno)
	{
		if (value <= 0 || value > INT16_MAX)
			errno = ERANGE;
		else if (end == string)
			errno = EINVAL;
	}
	if (errno)
	{
		log_error("Your configuration sets the parameter \"%s\" to \"%s\", which could not be parsed into an integer "
		          "in the range [1, %hi]: %s",
		          parameter_name,
		          string,
		          INT16_MAX,
		          strerror(errno));
		return false;
	}
	log_info("Using custom value for \"%s\": %li", parameter_name, value);

	UBLOXCFG_KEYVAL_t* pair = keyValuePairs + length;
	while (pair --> keyValuePairs)
	{
		if (pair->id == UBLOXCFG_CFG_TP_ANT_CABLEDELAY_ID)
		{
			pair->val.I2 = (int16_t)value;
			return true;
		}
	}
	return false;
}

/**
 * @brief Patch RTCM/raw message rates and UART1 output protocol in the config array.
 *
 * When gnss-rtcm-enabled=true, sets the RTCM UART1 message rates, the raw
 * UBX-RXM-RAWX / UBX-RXM-SFRBX rates, and the RTCM3 output protocol to their
 * enabled values in the reference config array, so the comparison against the
 * receiver's current config matches and the receiver is not hard-reset on
 * every restart.
 */
static bool set_rtcm_output(UBLOXCFG_KEYVAL_t* keyValuePairs, size_t length, const struct config* config)
{
	if (!config_get_bool_default(config, "gnss-rtcm-enabled", false))
		return false;

	static const uint32_t rtcm_uart1_ids[] = {
		UBLOXCFG_CFG_MSGOUT_RTCM_3X_TYPE1005_UART1_ID,
		UBLOXCFG_CFG_MSGOUT_RTCM_3X_TYPE1077_UART1_ID,
		UBLOXCFG_CFG_MSGOUT_RTCM_3X_TYPE1087_UART1_ID,
		UBLOXCFG_CFG_MSGOUT_RTCM_3X_TYPE1097_UART1_ID,
		UBLOXCFG_CFG_MSGOUT_RTCM_3X_TYPE1127_UART1_ID,
		UBLOXCFG_CFG_MSGOUT_RTCM_3X_TYPE1230_UART1_ID,
	};

	for (int i = 0; i < (int)(sizeof(rtcm_uart1_ids) / sizeof(rtcm_uart1_ids[0])); i++) {
		UBLOXCFG_KEYVAL_t* pair = keyValuePairs + length;
		while (pair --> keyValuePairs) {
			if (pair->id == rtcm_uart1_ids[i]) {
				pair->val.U1 = 1;
				break;
			}
		}
	}

	/* Also enable RTCM3 output protocol on UART1 */
	UBLOXCFG_KEYVAL_t* pair = keyValuePairs + length;
	while (pair --> keyValuePairs) {
		if (pair->id == UBLOXCFG_CFG_UART1OUTPROT_RTCM3X_ID) {
			pair->val.L = true;
			break;
		}
	}

	/* UBX-RXM-RAWX and UBX-RXM-SFRBX are enabled on UART1 at runtime by
	 * gnss_set_rtcm_config(). Reflect them in the reference config too, so the
	 * RAM-config comparison matches and the receiver is not hard-reset on every
	 * restart (same reasoning as the RTCM rates above). */
	static const char* ubx_raw_msgs[] = {
		"UBX-RXM-RAWX",
		"UBX-RXM-SFRBX",
	};
	for (int i = 0; i < (int)ARRAY_SIZE(ubx_raw_msgs); i++) {
		const UBLOXCFG_MSGRATE_t* items = ubloxcfg_getMsgRateCfg(ubx_raw_msgs[i]);
		if (items == NULL || items->itemUart1 == NULL)
			continue;
		UBLOXCFG_KEYVAL_t* p = keyValuePairs + length;
		while (p --> keyValuePairs) {
			if (p->id == items->itemUart1->id) {
				p->val.U1 = 1;
				break;
			}
		}
	}

	log_info("RTCM output enabled in base configuration for comparison");
	return true;
}

/**
 * @brief Swap L2 signal config keys with L5 equivalents for L5-capable receivers.
 *
 * The F9T-10B supports L1+L5 but not L2. When the default config (which has L2
 * signals enabled) fails on such a receiver, this function replaces L2 signal
 * keys with their L5 counterparts in the config array so it can be retried.
 *
 * Replacements:
 *   CFG-SIGNAL-GPS_L2C_ENA  -> CFG-SIGNAL-GPS_L5_ENA
 *   CFG-SIGNAL-GAL_E5B_ENA  -> CFG-SIGNAL-GAL_E5A_ENA
 *   CFG-SIGNAL-QZSS_L2C_ENA -> CFG-SIGNAL-QZSS_L5_ENA
 *   CFG-SIGNAL-GLO_L2_ENA   -> (removed, set false — no GLONASS L5)
 *   CFG-SIGNAL-BDS_B2_ENA   -> (removed, set false — B2a not universally supported)
 */
/**
 * @brief Send configuration from f9_defvalsets.h to GNSS receiver
 *
 * @param rx pointer to serial communication handler
 * @return boolean indicating receiver has correctly been reset to configuration
 */
static bool gnss_set_configuration(RX_t* rx, const struct config* config, int major, int minor)
{
	bool               receiver_configured = false;
	int                tries               = 0;

	/* Track which signal bands are active */
	bool band_l1  = true;  /* Always enabled */
	bool band_l2  = true;  /* Default: L2 enabled */
	bool band_l5  = false; /* Default: L5 disabled */

	/* Detect L5 receiver: read GPS_L5_ENA (0x10310004) from RAM */
	{
		uint32_t l5_key = 0x10310004; /* CFG-SIGNAL-GPS_L5_ENA */
		UBLOXCFG_KEYVAL_t l5_result[128];
		int n = rxGetConfig(rx, UBLOXCFG_LAYER_RAM, &l5_key, 1, l5_result, 128);
		if (n > 0 && l5_result[0].val.L) {
			log_info("L5 receiver detected (F9T-10B), using L5 signal config");
			band_l2 = false;
			band_l5 = true;
		}
	}

	// Get configuration for the detected receiver model
	int nAllKvCfg;
	int bands = GNSS_BAND_L1 | (band_l5 ? GNSS_BAND_L5 : GNSS_BAND_L2);
	UBLOXCFG_KEYVAL_t *allKvCfg = get_default_value_from_config(&nAllKvCfg, major, minor, bands);

	set_preferred_time_scale(allKvCfg, nAllKvCfg, config);
	set_cable_delay(allKvCfg, nAllKvCfg, config);
	set_rtcm_output(allKvCfg, nAllKvCfg, config);

	/* Detect if receiver is already configured by reading current config */
	receiver_configured = check_gnss_config_in_ram(rx, allKvCfg, nAllKvCfg);
	if (receiver_configured)
		log_info("Receiver already configured to desired configuration");
	else
		log_info("Receiver not configured to desired configuration, starting reconfiguration");

	while (!receiver_configured) {
		log_info("Configuring receiver with ART parameters (attempt %d/%d, %d keys)...",
			tries + 1, GNSS_RECONFIGURE_MAX_TRY, nAllKvCfg);
		bool res = rxSetConfig(rx, allKvCfg, nAllKvCfg, true, true, true);

		if (res) {
			log_info("Successfully reconfigured GNSS receiver");
			log_debug("Performing hardware reset");
			if (!rxReset(rx, RX_RESET_HARD)) {
				free(allKvCfg);
				return false;
			}
			log_info("hardware reset performed");
			receiver_configured = true;
		} else if (tries == 0 && !band_l5) {
			/* First failure on undetected receiver: may be unconfigured L5.
			 * Reload with L5 config and retry.
			 */
			log_warn("Config rejected (L2 keys unsupported?), retrying with L5 config");
			free(allKvCfg);
			band_l2 = false;
			band_l5 = true;
			allKvCfg = get_default_value_from_config(&nAllKvCfg, major, minor, GNSS_BAND_L1 | GNSS_BAND_L5);
			set_preferred_time_scale(allKvCfg, nAllKvCfg, config);
			set_cable_delay(allKvCfg, nAllKvCfg, config);
			set_rtcm_output(allKvCfg, nAllKvCfg, config);
		} else {
			log_warn("Config rejected on attempt %d/%d", tries + 1, GNSS_RECONFIGURE_MAX_TRY);
		}

		if (tries < GNSS_RECONFIGURE_MAX_TRY)
			tries++;
		else
		{
			log_error("Could not configure GNSS receiver\n");
			free(allKvCfg);
			return false;
		}
	}
	free(allKvCfg);

	/* Log active signal bands */
	log_info("Signal bands: L1=%s L2=%s L5=%s",
		band_l1 ? "on" : "off",
		band_l2 ? "on" : "off",
		band_l5 ? "on" : "off");

	return true;
}

/* Enable a UBX message at rate 1 on UART1. Used to add raw observation and
 * navigation messages to the RTCM base-station output. */
static void gnss_enable_ubx_uart1(RX_t *rx, const char *msgName)
{
	const UBLOXCFG_MSGRATE_t *items = ubloxcfg_getMsgRateCfg(msgName);
	if (items == NULL || items->itemUart1 == NULL) {
		log_warn("%s not supported by receiver, skipping", msgName);
		return;
	}
	UBLOXCFG_KEYVAL_t kv;
	kv.id = items->itemUart1->id;
	kv.val.U1 = 1;
	if (!rxSetConfig(rx, &kv, 1, true, true, true))
		log_warn("Failed to enable %s on UART1", msgName);
	else
		log_info("%s enabled on UART1", msgName);
}

/**
 * @brief Enable RTCM3 output on UART1 for NTRIP base station use.
 *
 * Enables RTCM3 output protocol and configures messages 1005, 1077,
 * 1087, 1097, 1127, 1230 at rate=1 on UART1. Also enables UBX-RXM-RAWX
 * (raw observations) and UBX-RXM-SFRBX (raw navigation subframes) so an
 * external NTRIP client can build MSM observations and broadcast ephemeris.
 * The frames are multiplexed with UBX on the same port and separated by
 * the parser.
 */
static bool gnss_set_rtcm_config(RX_t *rx)
{
	static const char *rtcm_messages[] = {
		"RTCM-3X-TYPE1005",
		"RTCM-3X-TYPE1077",
		"RTCM-3X-TYPE1087",
		"RTCM-3X-TYPE1097",
		"RTCM-3X-TYPE1127",
		"RTCM-3X-TYPE1230",
	};
	UBLOXCFG_KEYVAL_t kv[16];
	int nKv = 0;

	/* Enable RTCM3 output protocol on UART1 (in addition to UBX) */
	kv[nKv].id = UBLOXCFG_CFG_UART1OUTPROT_RTCM3X_ID;
	kv[nKv].val.L = true;
	nKv++;

	for (int i = 0; i < (int)ARRAY_SIZE(rtcm_messages); i++) {
		const UBLOXCFG_MSGRATE_t *items = ubloxcfg_getMsgRateCfg(rtcm_messages[i]);
		if (items != NULL && items->itemUart1 != NULL) {
			kv[nKv].id = items->itemUart1->id;
			kv[nKv].val.U1 = 1;
			nKv++;
		} else {
			log_warn("RTCM message %s not supported by receiver, skipping",
				rtcm_messages[i]);
		}
	}

	if (!rxSetConfig(rx, kv, nKv, true, true, true)) {
		log_error("Failed to apply RTCM configuration");
		return false;
	}

	/* Raw observations (RXM-RAWX) and navigation subframes (RXM-SFRBX) let an
	 * external NTRIP client generate MSM observations and broadcast ephemeris
	 * (RTCM 1019/1020/1042/1046), which casters need to compute a position. */
	gnss_enable_ubx_uart1(rx, "UBX-RXM-SFRBX");

	log_info("RTCM3 output enabled on UART1 (%d messages configured)", nKv - 1);
	return true;
}

/**
 * @brief Create a Unix domain socket for RTCM output.
 *
 * Creates a listening socket at RTCM_SOCK_PATH. Clients connect to
 * receive raw RTCM3 frames. Only one client is served at a time;
 * a new connection replaces the previous one.
 *
 * @return listening socket fd, or -1 on error
 */
static int rtcm_sock_create(void)
{
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = RTCM_SOCK_PATH,
	};
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		log_warn("RTCM socket create failed: %s", strerror(errno));
		return -1;
	}

	unlink(RTCM_SOCK_PATH);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		log_warn("RTCM socket bind failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	if (listen(fd, 1) < 0) {
		log_warn("RTCM socket listen failed: %s", strerror(errno));
		close(fd);
		unlink(RTCM_SOCK_PATH);
		return -1;
	}

	log_info("RTCM socket listening at %s", RTCM_SOCK_PATH);
	return fd;
}

/**
 * @brief Background thread that accepts RTCM client connections.
 *
 * Blocks on accept(). When a new client connects, it replaces the
 * previous client fd (closing the old one). The gnss_thread writes
 * RTCM frames to rtcm_client_fd without blocking.
 */
static void *rtcm_accept_thread_fn(void *p_data)
{
	struct gnss *gnss = (struct gnss *)p_data;

	while (gnss->rtcm_accept_running) {
		int client_fd = accept(gnss->rtcm_listen_fd, NULL, NULL);
		if (client_fd < 0) {
			if (errno == EINVAL || errno == EBADF)
				break; /* socket closed, shutting down */
			log_warn("RTCM accept failed: %s", strerror(errno));
			continue;
		}

		/* Swap in the new client fd under mutex */
		pthread_mutex_lock(&gnss->mutex_data);
		int old_fd = gnss->rtcm_client_fd;
		gnss->rtcm_client_fd = client_fd;
		pthread_mutex_unlock(&gnss->mutex_data);

		if (old_fd >= 0)
			close(old_fd);

		log_info("RTCM client connected (fd %d)", client_fd);
	}

	return NULL;
}

/**
 * @brief Clean up RTCM socket resources.
 */
static void rtcm_sock_cleanup(struct gnss *gnss)
{
	gnss->rtcm_accept_running = false;

	/* Close listening socket to unblock accept() */
	if (gnss->rtcm_listen_fd >= 0) {
		shutdown(gnss->rtcm_listen_fd, SHUT_RDWR);
		close(gnss->rtcm_listen_fd);
		gnss->rtcm_listen_fd = -1;
	}

	pthread_join(gnss->rtcm_accept_thread, NULL);

	pthread_mutex_lock(&gnss->mutex_data);
	int cfd = gnss->rtcm_client_fd;
	gnss->rtcm_client_fd = -1;
	pthread_mutex_unlock(&gnss->mutex_data);

	if (cfd >= 0)
		close(cfd);

	unlink(RTCM_SOCK_PATH);
}

/**
 * @brief Create gnss struct handler for thread
 *
 * @param config config structure of the program
 * @param session device session structure
 * @param fd_clock file pointer to PHC
 * @return struct gnss*
 */
struct gnss * gnss_init(const struct config *config, char *gnss_device_tty, struct gps_device_t *session, int fd_clock)
{
	struct gnss* gnss;
	int          ret  = -1;
	RX_OPTS_t    opts = RX_OPTS_DEFAULT();
	opts.autobaud     = true;
	opts.detect       = RX_DET_UBX;

	if (strchr(gnss_device_tty, '@')) {
		opts.autobaud = false;
	}

	if (session == NULL) {
		log_error("No gps session provided");
		return NULL;
	}

	gnss = (struct gnss *) malloc(sizeof(struct gnss));
	if (gnss == NULL) {
		log_error("could not allocate memory for gnss");
		return NULL;
	}

	gnss->fd_clock = fd_clock;
	gnss->session = session;
	gnss_reset_session_navigation_data(gnss->session);
	/* Init Antenna Status and Power to undefined values according to UBX Protocol */
	gnss->session->antenna_status = ANT_STATUS_UNDEFINED;
	gnss->session->antenna_power = ANT_POWER_UNDEFINED;
	gnss->rx = rxInit(gnss_device_tty, &opts);
	gnss->action = GNSS_ACTION_NONE;
	/* Init Survey In Error to undefined values */
	gnss->session->survey_in_position_error =-1.0;
	/* Init receiver version values */
	gnss->receiver_version_minor = -1;
	gnss->receiver_version_major = -1;
	if (gnss->rx == NULL)
		goto err_rxInit;

	if (!gnss_connect(gnss->rx))
		goto err_gnss_connect;

	/* Fetch receiver version and save it in gnss structure*/
	char verStr[100];
    if (rxGetVerStr(gnss->rx, verStr, sizeof(verStr))) {
		if (parse_receiver_version(verStr, &gnss->receiver_version_major, &gnss->receiver_version_minor))
			log_debug("Receiver version successfully detected ! Major is %d, Minor is %d ", gnss->receiver_version_major, gnss->receiver_version_minor);
		else
			log_warn("Receiver version parsing failed");
	}
	else
		log_warn("Receiver version get command failed");

	if (!gnss_set_configuration(gnss->rx, config, gnss->receiver_version_major, gnss->receiver_version_minor))
		goto err_gnss_connect;

	/* Always enable UBX-RXM-RAWX for per-band satellite monitoring */
	gnss_enable_ubx_uart1(gnss->rx, "UBX-RXM-RAWX");

	/* Enable RTCM3 output on UART1 if configured */
	gnss->rtcm_enabled = false;
	gnss->rtcm_listen_fd = -1;
	gnss->rtcm_client_fd = -1;
	gnss->rtcm_accept_running = false;
	if (config_get_bool_default(config, "gnss-rtcm-enabled", false)) {
		if (gnss_set_rtcm_config(gnss->rx)) {
			gnss->rtcm_enabled = true;
			mkdir("/run/oscillatord", 0755);
			gnss->rtcm_listen_fd = rtcm_sock_create();
			if (gnss->rtcm_listen_fd >= 0) {
				gnss->rtcm_accept_running = true;
				if (pthread_create(&gnss->rtcm_accept_thread, NULL,
					rtcm_accept_thread_fn, gnss) != 0) {
					log_warn("Failed to create RTCM accept thread");
					gnss->rtcm_accept_running = false;
					close(gnss->rtcm_listen_fd);
					gnss->rtcm_listen_fd = -1;
					unlink(RTCM_SOCK_PATH);
				}
			}
		} else {
			log_warn("Failed to enable RTCM output");
		}
	}

	gnss->stop = false;

	/* Initialize receiver's survey in flag */
	gnss->session->survey_completed = false;

	/* Check wether receiver's survey in should be bypassed or not */
	gnss->session->bypass_survey = config_get_bool_default(
		config,
		"gnss-bypass-survey",
		false);
	if (gnss->session->bypass_survey) {
		log_warn("GNSS Survey In will be bypassed, true timing performance might not be reached");
		log_warn("Please note that performance may be degraded and holdover might not reached specified limits");
	}

	if (!rxReset(gnss->rx, RX_RESET_GNSS_START)) {
		log_error("Could not start GNSS receiver");
		goto err_gnss_connect;
	}

	pthread_mutex_init(&gnss->mutex_data, NULL);
	pthread_cond_init(&gnss->cond_time, NULL);
	pthread_cond_init(&gnss->cond_data, NULL);

	ret = pthread_create(
		&gnss->thread,
		NULL,
		gnss_thread,
		gnss
	);

	if (ret != 0) {
		rxClose(gnss->rx);
		goto err_gnss_connect;
	}

	return gnss;

err_gnss_connect:
	free(gnss->rx);
	log_error("Could not connect to GNSS serial at %s", gnss_device_tty);
err_rxInit:
	free(gnss);
	error(EXIT_FAILURE, -ret, "gnss_init");
	return NULL;
}

/**
 * @brief Wait for next TAI time retrieved from the device
 *
 * @param gnss thread structure
 * @return time_t
 */
static time_t gnss_get_next_fix_tai_time(struct gnss * gnss)
{
	time_t time;
	pthread_mutex_lock(&gnss->mutex_data);
	pthread_cond_wait(&gnss->cond_time, &gnss->mutex_data);
	time = gnss->session->tai_time;
	pthread_mutex_unlock(&gnss->mutex_data);
	return time;

}

/**
 * @brief Get GNSS data from epoch
 *
 * @param gnss
 * @param valid Output Flags indicating GNSS data are valid (Fix >= 2D + FixOk)
 * @param qErr Output Quantization error from last Epoch
 */
int gnss_get_epoch_data(struct gnss *gnss, bool *valid, bool *survey, int32_t *qErr)
{
	if (!gnss) {
		return -1;
	}

	pthread_mutex_lock(&gnss->mutex_data);
	pthread_cond_wait(&gnss->cond_data, &gnss->mutex_data);
	if (survey != NULL)
		*survey = gnss->session->survey_completed;
	if (valid != NULL)
		*valid = gnss->session->valid;
	if (qErr != NULL)
		*qErr = gnss->session->context->qErr_last_epoch;
	pthread_mutex_unlock(&gnss->mutex_data);
	return 0;
}

/**
 * @brief Get GNSS data from epoch
 *
 * @param gnss
 * @param valid Output Flags indicating GNSS data are valid (Fix >= 2D + FixOk)
 * @param fixUtc Output Last fix time. Useful in case of fix loss
 */
int gnss_get_fix_info(struct gnss *gnss, bool *valid, struct timespec *fixUtc)
{
	if (!gnss) {
		return -1;
	}

	pthread_mutex_lock(&gnss->mutex_data);
	pthread_cond_wait(&gnss->cond_data, &gnss->mutex_data);
	if (valid != NULL)
		*valid = gnss->session->valid;
	if (fixUtc != NULL)
		*fixUtc = gnss->session->last_fix_utc_time;
	pthread_mutex_unlock(&gnss->mutex_data);
	return 0;
}

/**
 * @brief Check that time set in PHC is the same as the one coming from the GNSS receiver
 *
 * @param gnss
 * @return true
 * @return false
 */
static bool gnss_check_ptp_clock_time(struct gnss *gnss)
{
	struct timespec ts;
	bool valid = false;
	time_t gnss_time;
	int ret;
	if (gnss->fd_clock < 0) {
		log_warn("Bad clock file descriptor");
		return -1;
	}
	if (gnss_get_epoch_data(gnss, &valid, NULL, NULL))
		return -1;
	if (valid) {
		gnss_time = gnss_get_next_fix_tai_time(gnss);
		ret = clock_gettime(FD_TO_CLOCKID(gnss->fd_clock), &ts);
		if (ret == 0) {
			log_debug("GNSS tai time is %ld", gnss_time);
			log_debug("Time set on PHC is %ld", ts.tv_sec);
			if (ts.tv_sec == gnss_time) {
				log_info("PHC time is set to GNSS one");
				return true;
			} else {
				log_error("GNSS time is not the same as PTP clock time");
			}
		} else
			log_error("Could get not PHC time");
	} else {
		log_error("GNSS get valid is false");
	}
	return false;

}

/**
 * @brief Set PHC time to GNSS receiver time
 *
 * @param gnss
 * @return int: 0 on success, -1 on error
 */
int gnss_set_ptp_clock_time(struct gnss *gnss)
{
	clockid_t clkid;
	struct timespec ts;
	time_t gnss_time;
	int ret;
	bool valid = false;
	bool clock_set = false;
	bool clock_valid = false;

	if (!gnss) {
		return -1;
	}

	if (gnss->fd_clock < 0) {
		log_warn("Bad clock file descriptor");
		return -1;
	}
	clkid = FD_TO_CLOCKID(gnss->fd_clock);

	while(!clock_valid && loop) {
		if (gnss_get_epoch_data(gnss, &valid, NULL, NULL))
			return -1;

		if (valid) {
			/* Set clock time according to gnss data */
			if (!clock_set) {
				/* Configure PHC time */
				/* Wait to get next gnss TAI time */
				gnss_time = gnss_get_next_fix_tai_time(gnss);
				/* Then get clock time to preserve nanoseconds */
				ret = clock_gettime(clkid, &ts);
				if (ret == 0) {
					if (ts.tv_sec == gnss_time) {
						log_info("PTP Clock time already set");
						clock_set = true;
					} else {
						ts.tv_sec = gnss_time;
						ret = clock_settime(clkid, &ts);
						if (ret == 0) {
							clock_set = true;
							log_debug("PTP Clock Set");
							sleep(4);
						}
					}
				} else {
					log_warn("Could not get PTP clock time");
					return -1;
				}
			/* PHC time has been set, check time is correctly set */
			} else {
				if (gnss_check_ptp_clock_time(gnss)) {
					log_debug("PHC time correctly set");
					clock_valid = true;
				} else {
					log_warn("PHC time is not valid, resetting it");
					clock_set = false;
				}
			}
		} else {
			sleep(2);
		}
	}
	return 0;
}

static bool reset_serial(RX_t* rx)
{
	log_debug("Reseting receiver serial connection");
	rxClose(rx);
	usleep(500 * 1000);
	if (rxOpen(rx))
		return true;
	log_error("Unable to re-open serial connection");
	return false;
}

/**
 * @brief Count satellites per signal band from a UBX-RXM-RAWX message.
 *
 * Updates static counters on every RAWX message (1Hz).
 * Counts are included in the per-second GNSS data log.
 */

static void gnss_update_rawx_bands(const PARSER_MSG_t *msg)
{
	const uint8_t *payload = msg->data + UBX_HEAD_SIZE;
	int payload_len = msg->size - UBX_FRAME_SIZE;
	if (payload_len < 16)
		return;

	int numMeas = payload[11];
	if (payload_len < 16 + numMeas * 32)
		return;

	int l1 = 0, l2 = 0, l5 = 0;
	for (int i = 0; i < numMeas; i++) {
		int off = 16 + i * 32;
		uint8_t cno = payload[off + 26];
		uint8_t gnssId = payload[off + 20];
		uint8_t sigId = payload[off + 22];

		if (cno == 0)
			continue;

		/* Classification per u-blox signal numbering (UBX-RXM-RAWX):
		 *   GPS:     0,1=L1  3,4=L2  6,7=L5
		 *   SBAS:    0=L1  3=L5
		 *   Galileo: 0,1=E1(L1)  3,4=E5a(L5)  5,6=E5b(L2-band)
		 *   BeiDou:  0,1=B1(L1)  2,3=B2a(L5)  4,5=B2I(L2-band)  7=B3(~L2)
		 *   QZSS:    0,1=L1  4,5=L2  8,9=L5
		 *   GLONASS: 0=L1OF  2=L2OF
		 */
		switch (gnssId) {
		case 0: /* GPS */
			if (sigId <= 1) l1++;
			else if (sigId <= 4) l2++;
			else l5++;
			break;
		case 1: /* SBAS */
			if (sigId == 0) l1++;
			else l5++;
			break;
		case 2: /* Galileo */
			if (sigId <= 1) l1++;
			else if (sigId <= 4) l5++;  /* E5a = L5 band */
			else l2++;                  /* E5b = L2 band */
			break;
		case 3: /* BeiDou */
			if (sigId <= 1) l1++;       /* B1I D1/D2 (1561 MHz) */
			else if (sigId <= 3) l2++;  /* B2I D1/D2 (1207 MHz) */
			else if (sigId <= 6) l1++;  /* B1C pilot/data (1575 MHz) */
			else l5++;                  /* B2a (1176 MHz) */
			break;
		case 5: /* QZSS */
			if (sigId <= 1) l1++;
			else if (sigId <= 5) l2++;
			else l5++;
			break;
		case 6: /* GLONASS */
			if (sigId == 0) l1++;
			else l2++;
			break;
		default:
			l1++; /* Unknown constellation, count as L1 */
			break;
		}
	}

	rawx_sats_l1 = l1;
	rawx_sats_l2 = l2;
	rawx_sats_l5 = l5;
}

/* True for messages an external NTRIP client needs forwarded over the socket:
 * RTCM3 (native MSM + station/bias), plus raw observations (UBX-RXM-RAWX) and
 * navigation subframes (UBX-RXM-SFRBX, used to build broadcast ephemeris). */
static bool gnss_rtcm_forward_msg(const PARSER_MSG_t *msg)
{
	if (msg->type == PARSER_MSGTYPE_RTCM3)
		return true;
	if (msg->type == PARSER_MSGTYPE_UBX) {
		uint8_t clsId = UBX_CLSID(msg->data);
		uint8_t msgId = UBX_MSGID(msg->data);
		/* RXM-RAWX (0x02 0x15) and RXM-SFRBX (0x02 0x13) */
		return clsId == 0x02 && (msgId == 0x15 || msgId == 0x13);
	}
	return false;
}

/**
 * @brief Thread routine
 *
 * @param p_data
 * @return void*
 */
static void * gnss_thread(void * p_data)
{
	EPOCH_t coll;
	EPOCH_t epoch;
	struct gnss *gnss = (struct gnss*) p_data;
	struct gps_device_t * session;
	enum gnss_action action = GNSS_ACTION_NONE;
	bool stop;
	int rtcm_log_counter = 0;
	int rtcm_msg_count = 0;
	int rtcm_byte_count = 0;

	epochInit(&coll);

	pthread_mutex_lock(&gnss->mutex_data);
	stop = gnss->stop;
	pthread_mutex_unlock(&gnss->mutex_data);

	while (!stop)
	{
		PARSER_MSG_t *msg = rxGetNextMessageTimeout(gnss->rx, GNSS_TIMEOUT_MS);
		if (msg != NULL)
		{
			/* Update per-band satellite counts from RAWX (always enabled) */
			if (msg->type == PARSER_MSGTYPE_UBX &&
			    UBX_CLSID(msg->data) == 0x02 && UBX_MSGID(msg->data) == 0x15) {
				gnss_update_rawx_bands(msg);
			}

			/* Forward frames an external NTRIP client needs (RTCM3 plus raw
			 * observation/navigation UBX messages) to the socket client. */
			if (gnss->rtcm_enabled && gnss_rtcm_forward_msg(msg)) {
				rtcm_msg_count++;
				rtcm_byte_count += msg->size;
				pthread_mutex_lock(&gnss->mutex_data);
				int cfd = gnss->rtcm_client_fd;
				pthread_mutex_unlock(&gnss->mutex_data);
				if (cfd >= 0) {
					ssize_t ret = send(cfd, msg->data, msg->size,
						MSG_NOSIGNAL);
					if (ret < 0) {
						log_info("RTCM client disconnected");
						pthread_mutex_lock(&gnss->mutex_data);
						if (gnss->rtcm_client_fd == cfd) {
							gnss->rtcm_client_fd = -1;
							pthread_mutex_unlock(&gnss->mutex_data);
							close(cfd);
						} else {
							pthread_mutex_unlock(&gnss->mutex_data);
						}
					}
				}
			}

			pthread_mutex_lock(&gnss->mutex_data);
			session = gnss->session;
			// Epoch collect is used to fetch navigation data such as time and leap seconds
			if(epochCollect(&coll, msg, &epoch))
			{
				// if epoch has no fix there will be no Nav solution and 0 satellites
				session->satellites_count = gnss_get_satellites(&epoch);
				if (epoch.haveFix) {
					session->last_fix_utc_time.tv_sec = gnss_get_utc_time(&epoch);
					session->fix = epoch.fix;
					session->fixOk = epoch.fixOk && session->satellites_count > NUM_SAT_MIN;
					session->valid = session->fix >= EPOCH_FIX_TIME && session->fixOk;
					if (!session->valid) {
						if (session->fix < EPOCH_FIX_TIME)
							log_trace("Fix is to low: %d", session->fix);
						if (!session->fixOk)
							log_trace("Fix is not OK");
					}
					struct timedelta_t td;
					ntp_latch(session, &td);
					log_gnss_data(session);
				} else {
					session->fix = NO_FIX;
					session->fixOk = false;
				}
				if (epoch.havePos)
					session->position_accuracy = (int64_t)epoch.posAcc;
				else
					session->position_accuracy = -1;
				if (epoch.haveTime)
					session->time_accuracy = (int64_t)(epoch.timeAcc / UBX_NAV_PVT_V1_TACC_SCALE);
				else
					session->time_accuracy = -1;

				pthread_cond_signal(&gnss->cond_data);

				if (session->tai_time_set)
					pthread_cond_signal(&gnss->cond_time);
				else
					log_warn("Could not tai time from gnss, please check GNSS Configuration if this message keeps appearing more than 25 minutes");

				/* Log RTCM status periodically (every 60 epochs / ~60s) */
				if (gnss->rtcm_enabled) {
					rtcm_log_counter++;
					if (rtcm_log_counter >= 60) {
						log_info("RTCM: %d frames, %d bytes in last %ds (client %s)",
							rtcm_msg_count, rtcm_byte_count, rtcm_log_counter,
							gnss->rtcm_client_fd >= 0 ?
								"connected" : "waiting");
						rtcm_msg_count = 0;
						rtcm_byte_count = 0;
						rtcm_log_counter = 0;
					}
				}

			} else {
				// Analyze msg to parse UBX-MON-RF to get antenna status
				uint8_t clsId = UBX_CLSID(msg->data);
				uint8_t msgId = UBX_MSGID(msg->data);
				if (clsId == UBX_MON_CLSID && msgId == UBX_MON_RF_MSGID) {
					gnss_get_antenna_data(session, msg);
					log_trace("GNSS: Antenna status: 0x%x", session->antenna_status);
					log_trace("GNSS: Power status: 0x%x", session->antenna_power);
					if (session->antenna_power == UBX_MON_RF_V0_ANTPOWER_OFF) {
						/* Antenna power is off, hence this is the only message we will get on the serial
						 * We need to signal main thread that we do not have fix nor satellite count
						 * Reset data because we cannot assume either of these
						 */
						gnss_reset_session_navigation_data(gnss->session);
						pthread_cond_signal(&gnss->cond_data);
					}
				// Parse UBX-NAV-TIMELS messages there because library does not do it
				} else if (clsId == UBX_NAV_CLSID && msgId == UBX_NAV_TIMELS_MSGID)
					gnss_parse_ubx_nav_timels(session, msg);
				else if (clsId == UBX_TIM_CLSID && msgId == UBX_TIM_TP_MSGID)
					gnss_parse_ubx_tim_tp(session, msg);
				else if (clsId == UBX_TIM_CLSID && msgId == UBX_TIM_SVIN_MSGID) {
					enum SurveyInState surveyInState = gnss_parse_ubx_tim_svin(session, msg);
					if (!session->survey_completed && !gnss->session->bypass_survey) {
						switch (surveyInState) {
						case SURVEY_IN_COMPLETED:
							session->survey_completed = true;
							break;
						case SURVEY_IN_IN_PROGRESS:
						case SURVEY_IN_UNKNOWN:
							break;
						case SURVEY_IN_KO:
						default:
							log_error("Survey In did not complete in time. GNSS conditions are not stable enough for optimal timing performance");
							log_error("Please check your antenna setup (antenna on roof is way more precise) to pass survey in.");
							break;
						}
					}
				}
			}
			pthread_mutex_unlock(&gnss->mutex_data);
		} else {
			log_warn("UART GNSS Timeout !");
			pthread_mutex_lock(&gnss->mutex_data);
			/* Reset data because we cannot assume either of these */
			gnss_reset_session_navigation_data(gnss->session);
			reset_serial(gnss->rx);
			pthread_cond_signal(&gnss->cond_data);
			pthread_mutex_unlock(&gnss->mutex_data);
			usleep(5 * 1000);
		}

		/* this thread is the only writer to gnss->session, it's safe read the same values without mutex_data locked */
		if (gnss->gnss_info) {
			struct gnss_state *gnss_info = gnss->gnss_info;
			pthread_mutex_lock(&gnss_info->lock);
			gnss_info->antenna_power = gnss->session->antenna_power;
			gnss_info->antenna_status = gnss->session->antenna_status;
			gnss_info->fix = gnss->session->fix;
			gnss_info->fixOk = gnss->session->fixOk;
			gnss_info->leap_seconds = gnss->session->context->leap_seconds;
			gnss_info->lsChange = gnss->session->context->lsChange;
			gnss_info->satellites_count = gnss->session->satellites_count;
			gnss_info->survey_in_position_error = gnss->session->survey_in_position_error;
			gnss_info->time_accuracy = gnss->session->time_accuracy;
			gnss_info->position_accuracy = gnss->session->position_accuracy;
			pthread_mutex_unlock(&gnss_info->lock);
		}

		pthread_mutex_lock(&gnss->mutex_data);
		stop = gnss->stop;
		action = gnss->action;
		gnss->action = GNSS_ACTION_NONE;
		pthread_mutex_unlock(&gnss->mutex_data);

		if (action == GNSS_ACTION_START) {
			log_debug("Performing GNSS START");
			if (!rxReset(gnss->rx, RX_RESET_GNSS_START))
				log_error("Could not start GNSS Receiver");
			else
				log_info("GNSS START performed");
		} else if (action == GNSS_ACTION_STOP) {
			log_debug("Performing GNSS STOP");
			if (!rxReset(gnss->rx, RX_RESET_GNSS_STOP))
				log_error("Could not stop GNSS Receiver");
			else
				log_info("GNSS STOP performed");
		} else if (action == GNSS_ACTION_SOFT) {
			log_debug("Performing GNSS SOFT RESET");
			if (!rxReset(gnss->rx, RX_RESET_SOFT))
				log_error("Could not soft reset GNSS Receiver");
			else
				log_info("GNSS SOFT RESET performed");
		} else if (action == GNSS_ACTION_HARD) {
			log_debug("Performing GNSS HARD RESET");
			if (!rxReset(gnss->rx, RX_RESET_HARD))
				log_error("Could not hard reset GNSS Receiver");
			else
				log_info("GNSS HARD RESET performed");
		} else if (action == GNSS_ACTION_COLD) {
			log_debug("Performing GNSS COLD RESET");
			if (!rxReset(gnss->rx, RX_RESET_COLD))
				log_error("Could not cold reset GNSS Receiver");
			else
				log_info("GNSS COLD RESET performed");
		} else if (action == GNSS_ACTION_RESET_SERIAL)
		{
			reset_serial(gnss->rx);
		}
	}

	log_debug("Closing gnss session");
	rxClose(gnss->rx);
	free(gnss->rx);
	gnss->rx = NULL;
	free(gnss);
	gnss = NULL;
	return NULL;
}

/**
 * @brief Stop gnss thread
 *
 * @param gnss
 */
void gnss_stop(struct gnss *gnss)
{
	if (!gnss)
		return;

	pthread_mutex_lock(&gnss->mutex_data);
	gnss->stop = true;
	pthread_mutex_unlock(&gnss->mutex_data);

	pthread_join(gnss->thread, NULL);

	if (gnss->rtcm_enabled)
		rtcm_sock_cleanup(gnss);
}

void gnss_set_action(struct gnss *gnss, enum gnss_action action)
{
	if (!gnss)
		return;

	if (action != GNSS_ACTION_START && action != GNSS_ACTION_STOP && action != GNSS_ACTION_SOFT && action != GNSS_ACTION_HARD && action != GNSS_ACTION_COLD) {
		log_error("Unknown action %d", action);
		return;
	}

	pthread_mutex_lock(&gnss->mutex_data);
	gnss->action = action;
	pthread_mutex_unlock(&gnss->mutex_data);
	return;
}
