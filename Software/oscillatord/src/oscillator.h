/**
 * @file oscillator.h
 * @brief Generic structure for oscillators supported by the program
 * @date 2022-01-10
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef SRC_OSCILLATOR_H_
#define SRC_OSCILLATOR_H_
#include <inttypes.h>

#include "config.h"
#include "gnss.h"
#include "phasemeter.h"

#include <oscillator-disciplining/oscillator-disciplining.h>

#ifndef OSCILLATOR_NAME_LENGTH
#define OSCILLATOR_NAME_LENGTH 50
#endif

struct oscillator;
struct oscillator_ctrl;
struct oscillator_attributes;

typedef struct oscillator *(*oscillator_new_cb)(struct devices_path *devices_path);
typedef int (*oscillator_get_ctrl_cb)(struct oscillator *oscillator,
		struct oscillator_ctrl *ctrl);
typedef int (*oscillator_save_cb)(struct oscillator *oscillator);
typedef int (*oscillator_parse_attributes_cb)(struct oscillator *oscillator,
		struct oscillator_attributes *attributes);
typedef int (*oscillator_apply_output_cb)(struct oscillator *oscillator,
		struct od_output *output);
typedef void (*oscillator_destroy_cb)(struct oscillator **oscillator);
typedef struct calibration_results* (*oscillator_calibrate_cb)(struct oscillator *oscillator,
		struct phasemeter *phasemeter, struct gnss *gnss, struct calibration_parameters *calib_params,
		int phase_sign);
typedef int (*oscillator_get_phase_error_cb)(struct oscillator *oscillator,
		int64_t *phase_error);
typedef int (*oscillator_get_disciplining_status_cb)(struct oscillator *oscillator, void *data);
typedef int (*oscillator_push_gnss_info_cb)(struct oscillator *oscillator, bool fixOk, const struct timespec *last_fix_utc_time);

struct oscillator_class {
	const char *name;
	oscillator_get_ctrl_cb get_ctrl;
	oscillator_save_cb save;
	oscillator_parse_attributes_cb parse_attributes;
	oscillator_apply_output_cb apply_output;
	oscillator_calibrate_cb calibrate;
	oscillator_get_phase_error_cb get_phase_error;
	oscillator_get_disciplining_status_cb get_disciplining_status;
	oscillator_push_gnss_info_cb push_gnss_info;
	/* default values use if per-instance ones haven't been set */
	uint32_t dac_max;
	uint32_t dac_min;
};

struct oscillator {
	char name[OSCILLATOR_NAME_LENGTH];
	const struct oscillator_class *class;
	/* UINT32_MAX if not specified */
	uint32_t dac_min;
	/* 0 if not specified */
	uint32_t dac_max;
};

/* Control values for the different oscillators */
struct oscillator_ctrl {
	/* Used for dummy, morion, rakon and sim */
	uint32_t dac;
	/* Used for mRO50 */
	uint32_t fine_ctrl;
	uint32_t coarse_ctrl;
};

struct oscillator_attributes {
	int64_t phase_error;
	double temperature;
	bool locked;
};

int oscillator_get_phase_error(struct oscillator *oscillator, int64_t *phase_error);
int oscillator_set_dac_min(struct oscillator *oscillator, uint32_t dac_min);
int oscillator_set_dac_max(struct oscillator *oscillator, uint32_t dac_max);
int oscillator_get_ctrl(struct oscillator *oscillator, struct oscillator_ctrl *ctrl);
int oscillator_save(struct oscillator *oscillator);
int oscillator_parse_attributes(struct oscillator *oscillator, struct oscillator_attributes *attributes);
int oscillator_apply_output(struct oscillator *oscillator, struct od_output *output);
int oscillator_get_disciplining_status(struct oscillator *oscillator, void *data);
int oscillator_push_gnss_info(struct oscillator *oscillator, bool fixOk, const struct timespec *last_fix_utc_time);
struct calibration_results * oscillator_calibrate(
	struct oscillator *oscillator,
	struct phasemeter *phasemeter,
	struct gnss *gnss,
	struct calibration_parameters * calib_params,
	int phase_sign);

#endif /* SRC_OSCILLATOR_H_ */
