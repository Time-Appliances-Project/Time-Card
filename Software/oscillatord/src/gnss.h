/**
 * @file gnss.h
 * @brief File handling Ublox F9 gnss receiver
 * @version 0.1
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2022
 *
 * Ublox F9 is handled by a thread that will configure it if requested and
 * parse incoming messages.
 * When enough data is collected to get a precise measure of the time, a signal is generated
 * to tell the rest program that PHC time can be set.
 * Thread uses ubloxcfg library to manage and decode data from the receiver.
 */
#ifndef OSCILLATORD_GNSS_H
#define OSCILLATORD_GNSS_H

#include <ff/ff_rx.h>
#include <pthread.h>
#include <termios.h>

#include "config.h"
#include "ntpshm/ppsthread.h"

#define MAX_DEVICES 4
#define NTPSHMSEGS      (MAX_DEVICES * 2)       /* number of NTP SHM segments */
#define NTP_MIN_FIXES   3  /* # fixes to wait for before shipping NTP time */

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)	((clockid_t) ((((unsigned int) ~fd) << 3) | CLOCKFD))

typedef struct timespec timespec_t;	/* Unix time as sec, nsec */
struct gps_device_t;

/*
 * Each input source has an associated type.  This is currently used in two
 * ways:
 *
 * (1) To determine if we require that gpsd be the only process opening a
 * device.  We make an exception for PTYs because the master side has to be
 * opened by test code.
 *
 * (2) To determine whether it's safe to send wakeup strings.  These are
 * required on some unusual RS-232 devices (such as the TNT compass and
 * Thales/Ashtech GPSes) but should not be shipped to unidentified USB
 * or Bluetooth devices as we don't even know in advance those are GPSes;
 * they might not cope well.
 *
 * Where it says "case detected but not used" it means that we can identify
 * a source type but no behavior is yet contingent on it.  A "discoverable"
 * device is one for which there is discoverable metadata such as a
 * vendor/product ID.
 *
 * We should never see a block device; that would indicate a serious error
 * in command-line usage or the hotplug system.
 */
typedef enum {source_unknown,
              source_blockdev,  /* block devices can't be GPS sources */
              source_rs232,     /* potential GPS source, not discoverable */
              source_usb,       /* potential GPS source, discoverable */
              source_bluetooth, /* potential GPS source, discoverable */
              source_can,       /* potential GPS source, fixed CAN format */
              source_pty,       /* PTY: we don't require exclusive access */
              source_tcp,       /* TCP/IP stream: case detected but not used */
              source_udp,       /* UDP stream: case detected but not used */
              source_gpsd,      /* Remote gpsd instance over TCP/IP */
              source_pps,       /* PPS-only device, such as /dev/ppsN */
              source_pipe,      /* Unix FIFO; don't use blocking I/O */
} sourcetype_t;

enum gnss_action {
	GNSS_ACTION_NONE,
	GNSS_ACTION_START,
	GNSS_ACTION_STOP,
	GNSS_ACTION_SOFT,
	GNSS_ACTION_HARD,
	GNSS_ACTION_COLD,
	GNSS_ACTION_RESET_SERIAL
};

struct gps_context_t {
	int valid;                          /* member validity flags */
#define LEAP_SECOND_VALID       0x01    /* we have or don't need correction */
#define GPS_TIME_VALID          0x02    /* GPS week/tow is valid */
#define CENTURY_VALID           0x04    /* have received ZDA or 4-digit year */
	// struct gpsd_errout_t errout;        /* debug verbosity level and hook */
	bool readonly;                      /* if true, never write to device */
	bool passive;                       // if true, never autoconfigure device
	// if true, remove fix gate to time, for some RTC backed receivers.
	// DANGEROUS
	bool batteryRTC;
	speed_t fixed_port_speed;           // Fixed port speed, if non-zero
	char fixed_port_framing[4];         // Fixed port framing, if non-blank
	int32_t qErr;                       // Quantization Error of current Epoch
	int32_t qErr_last_epoch;                       // Quantization Error of last Epoch
	/* DGPS status */
	int fixcnt;                         /* count of good fixes seen */
	/* timekeeping */
	time_t start_time;                  /* local time of daemon startup */
	int leap_seconds;                   /* Unix secs to UTC (GPS-UTC offset) */
	unsigned short gps_week;            /* GPS week, usually 10 bits */
	timespec_t gps_tow;                 /* GPS time of week */
	int century;                        /* for NMEA-only devices without ZDA */
	int rollovers;                      /* rollovers since start of run */
	int leap_notify;                    /* notification state from subframe */
#define LEAP_NOWARNING  0x0     /* normal, no leap second warning */
#define LEAP_ADDSECOND  0x1     /* last minute of day has 60 seconds */
#define LEAP_DELSECOND  0x2     /* last minute of day has 59 seconds */
#define LEAP_NOTINSYNC  0x3     /* overload, clock is free running */
	int lsChange;
	int timeToLsEvent;
	bool lsset;
	/* we need the volatile here to tell the C compiler not to
		* 'optimize' as 'dead code' the writes to SHM */
	volatile struct shmTime *shmTime[NTPSHMSEGS];
	bool shmTimeInuse[NTPSHMSEGS];
	void (*pps_hook)(struct gps_device_t *, struct timedelta_t *);
#ifdef SHM_EXPORT_ENABLE
    /* we don't want the compiler to treat writes to shmexport as dead code,
     * and we don't want them reordered either */
    volatile void *shmexport;
    int shmid;                          /* ID of SHM  (for later IPC_RMID) */
#endif
	ssize_t (*serial_write)(struct gps_device_t *,
		const char *buf, const size_t len);
};


/**
 * @struct gnss_state
 * @brief Structure containing data with the latest gnss values
 */
struct gnss_state {
	int64_t position_accuracy; // in meters
	int64_t time_accuracy; // in nanoseconds
	float survey_in_position_error;
	int fix;
	int satellites_count;
	int leap_seconds;
	int lsChange;
	int8_t antenna_power;
	int8_t antenna_status;
	bool fixOk;
	pthread_mutex_t lock;
};

/**
 * @struct gps_device_t
 * @brief Structure containing data about the gnss device
 */
struct gps_device_t {
	/** gps context as defined in gpsd */
	struct gps_context_t        *context;
	sourcetype_t sourcetype;
	volatile struct shmTime *shm_clock;
	volatile struct shmTime *shm_pps;
	/** pointer to thread catching PPS event to fill the NTP SHM*/
	volatile struct pps_thread_t pps_thread;
	/** count of fixes from this device */
	int fixcnt;
	/** UTC time of last fix */
	struct timespec last_fix_utc_time;
	/** GNSS fix value */
	int fix;
	/** Indicate if fix is OK */
	bool fixOk;
	int8_t antenna_status;
	int8_t antenna_power;
	/** General indicator that GNSS data are valid */
	bool valid;
	/** Indicate TAI time as been set from a constellation time of UTC */
	bool tai_time_set;
	/** TAI time */
	int tai_time;
	/** Number of satellites used */
	int satellites_count;
	/** Wether Survey In should be bypassed or not */
	bool bypass_survey;
	/** Survey in successfully completed */
	bool survey_completed;
	/** Survey in error in meter from meanV field from UBX-TIM-SVIN msg */
	float survey_in_position_error;
	int64_t position_accuracy; // in meters
	int64_t time_accuracy; // in nanoseconds
};

/**
 * @struct gnss
 * @brief General thread structure
 */
struct gnss {
	bool session_open;
	RX_t *rx;
	struct gps_device_t *session;
	pthread_t thread;
	pthread_mutex_t mutex_data;
	pthread_cond_t cond_time;
	pthread_cond_t cond_data;
	int fd_clock;
	enum gnss_action action;
	bool stop;
	int receiver_version_major;
	int receiver_version_minor;
	struct gnss_state *gnss_info;
	bool rtcm_enabled;
	int rtcm_listen_fd;
	int rtcm_client_fd;
	pthread_t rtcm_accept_thread;
	bool rtcm_accept_running;
};

struct gnss* gnss_init(const struct config *config, char *gnss_device_tty, struct gps_device_t *session, int fd_clock);
int gnss_get_epoch_data(struct gnss *gnss, bool *valid, bool *survey, int32_t *qErr);
void gnss_stop(struct gnss *gnss);
void gnss_set_action(struct gnss *gnss, enum gnss_action action);
int gnss_set_ptp_clock_time(struct gnss *gnss);
int gnss_get_fix_info(struct gnss *gnss, bool *valid, struct timespec *fixUtc);

#endif
