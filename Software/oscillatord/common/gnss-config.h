#ifndef OSCILLATORD_GNSS_CONFIG_H
#define OSCILLATORD_GNSS_CONFIG_H

#include <ff/ff_rx.h>
#include <ubloxcfg/ubloxcfg.h>

/* Signal band flags for receiver configuration selection */
#define GNSS_BAND_L1  (1 << 0)
#define GNSS_BAND_L2  (1 << 1)
#define GNSS_BAND_L5  (1 << 2)

bool check_gnss_config_in_ram(RX_t *rx, UBLOXCFG_KEYVAL_t *allKvCfg, int nAllKvCfg);
UBLOXCFG_KEYVAL_t *get_default_value_from_config(int *nKv, int major, int minor, int bands);

#endif /* OSCILLATORD_GNSS_CONFIG_H */
