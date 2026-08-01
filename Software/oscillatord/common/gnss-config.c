#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ff/ff_ubx.h>

#include "gnss-config.h"

#include "f9_defvalsets.h"
#include "log.h"

#define CFG_SET_MAX_MSGS 20
#define CFG_SET_MAX_KV   (UBX_CFG_VALSET_V1_MAX_KV * CFG_SET_MAX_MSGS)

#define NUMOF(x) (int)(sizeof(x)/sizeof(*(x)))

bool check_gnss_config_in_ram(RX_t *rx, UBLOXCFG_KEYVAL_t *allKvCfg, int nAllKvCfg)
{
    bool receiverconfigured = true;
    const int batch_size = 64;

    /* Read all config keys in batches of 64 */
    UBLOXCFG_KEYVAL_t *kvRam = malloc(nAllKvCfg * sizeof(UBLOXCFG_KEYVAL_t));
    if (!kvRam) {
        log_warn("malloc fail for RAM config");
        return false;
    }
    int totalRam = 0;

    for (int offset = 0; offset < nAllKvCfg; offset += batch_size) {
        int batch = nAllKvCfg - offset;
        if (batch > batch_size) batch = batch_size;

        uint32_t keys[64];
        for (int i = 0; i < batch; i++)
            keys[i] = allKvCfg[offset + i].id;

        /* rxGetConfig needs maxKv >= received + 64 (library pre-checks space for next batch) */
        UBLOXCFG_KEYVAL_t batchResult[128];
        int n = rxGetConfig(rx, UBLOXCFG_LAYER_RAM, keys, batch, batchResult, 128);
        if (n > 0) {
            for (int i = 0; i < n && totalRam < nAllKvCfg; i++)
                kvRam[totalRam++] = batchResult[i];
        } else {
            log_debug("Config read failed at offset=%d batch=%d", offset, batch);
        }
    }

    log_info("Config read: %d/%d keys retrieved (batch_size=%d)", totalRam, nAllKvCfg, batch_size);

    if (totalRam <= 0) {
        log_warn("Could not read any config from receiver RAM");
        free(kvRam);
        return false;
    }

    // Compare config values
    for (int ixKvCfg = 0; ixKvCfg < nAllKvCfg; ixKvCfg++)
    {
        const UBLOXCFG_KEYVAL_t *kvCfg = &allKvCfg[ixKvCfg];

        for (int ixKvRam = 0; ixKvRam < totalRam; ixKvRam++)
        {
            if (kvRam[ixKvRam].id == kvCfg->id)
            {
                if (kvRam[ixKvRam].val._raw != kvCfg->val._raw)
                {
                    receiverconfigured = false;
                    char strCfg[UBLOXCFG_MAX_KEYVAL_STR_SIZE];
                    char strRam[UBLOXCFG_MAX_KEYVAL_STR_SIZE];
                    if (ubloxcfg_stringifyKeyVal(strCfg, sizeof(strCfg), kvCfg) &&
                        ubloxcfg_stringifyKeyVal(strRam, sizeof(strRam), &kvRam[ixKvRam]) )
                    {
                        log_debug("Config (%s) differs from current config (%s)", strCfg, strRam);
                    }
                }
                break;
            }
        }
    }

    free(kvRam);
    return receiverconfigured;
}

/* ****************************************************************************************************************** */

typedef struct CFG_DB_s
{
    UBLOXCFG_KEYVAL_t     *kv;
    int                    nKv;
    int                    maxKv;
} CFG_DB_t;

typedef struct IO_LINE_s
{
    char       *line;
    int         lineNr;
    int         lineLen;
    const char *file;

} IO_LINE_t;


static bool _cfgDbAdd(CFG_DB_t *db, IO_LINE_t *line);

UBLOXCFG_KEYVAL_t *get_default_value_from_config(int *nKv, int major, int minor, int bands)
{
	const int          kvSize = CFG_SET_MAX_KV * sizeof(UBLOXCFG_KEYVAL_t);
	UBLOXCFG_KEYVAL_t* kv     = malloc(kvSize);
	if (kv == NULL)
	{
		log_warn("malloc fail");
		return NULL;
	}
	memset(kv, 0, kvSize);

	CFG_DB_t db  = {.kv = kv, .nKv = 0, .maxKv = CFG_SET_MAX_KV};
	bool     res = true;
	char     current_line[128] = {};

	{
		/* Select config array based on firmware version and band mask */
		const char (*cfg_array)[128];
		size_t config_length;

		if (bands & GNSS_BAND_L5) {
			cfg_array = default_configuration_f9t_10b;
			config_length = default_configuration_f9t_10b_size;
		} else if ((major == 2 && minor >= 20) || (major >= 3)) {
			cfg_array = default_configuration_v220;
			config_length = default_configuration_v220_size;
		} else {
			cfg_array = default_configuration;
			config_length = default_configuration_size;
		}

		for (size_t i = 0; i < config_length; i++)
		{
			strncpy(current_line, cfg_array[i], 127);
			IO_LINE_t line = { .line = current_line };

			if (!_cfgDbAdd(&db, &line))
			{
				res = false;
				break;
			}
		}
	}

	if (!res)
	{
		log_warn("Failed reading config file!");
		free(kv);
		return NULL;
	}

	*nKv = db.nKv;
	return kv;
}

// ---------------------------------------------------------------------------------------------------------------------

typedef struct MSGRATE_CFG_s
{
    const char            *name;
    const char            *rate;
    const UBLOXCFG_ITEM_t *item;
} MSGRATE_CFG_t;

typedef struct BAUD_CFG_s
{
    const char    *str;
    const uint32_t val;
} BAUD_CFG_t;

typedef struct PROTFILT_CFG_s
{
    const char     *name;
    const uint32_t  id;
} PROTFILT_CFG_t;

typedef struct PORT_CFG_s
{
    const char    *name;
    uint32_t       baudrateId;
    PROTFILT_CFG_t inProt[3];
    PROTFILT_CFG_t outProt[3];
} PORT_CFG_t;

// separator for fields
static const char * const kCfgTokSep = " \t";
// separator for parts inside fields
static const char * const kCfgPartSep = ",";

static bool _cfgDbAddKeyVal(CFG_DB_t *db, IO_LINE_t *line, const uint32_t id, const UBLOXCFG_VALUE_t *value);
static bool _cfgDbApplyProtfilt(CFG_DB_t *db, IO_LINE_t *line, char *protfilt, const PROTFILT_CFG_t *protfiltCfg, const int nProtfiltCfg);

static bool _cfgDbAdd(CFG_DB_t *db, IO_LINE_t *line)
{
    log_trace("%s", line->line);
    // Named key-value pair
    if (strncmp(line->line, "CFG-", 4) == 0)
    {
        // Expect exactly two tokens separated by whitespace
        char *keyStr = strtok(line->line, kCfgTokSep);
        char *valStr = strtok(NULL, kCfgTokSep);
        char *none = strtok(NULL, kCfgTokSep);
        log_trace("- key-val: keyStr=[%s] valStr=[%s]", keyStr, valStr);
        if ( (keyStr == NULL) || (valStr == NULL) || (none != NULL) )
        {
            log_warn("Expected key-value pair!");
            return false;
        }

        // Get item
        const UBLOXCFG_ITEM_t *item = ubloxcfg_getItemByName(keyStr);
        if (item == NULL)
        {
            log_warn("Unknown item '%s'!", keyStr);
            return false;
        }

        // Get value
        UBLOXCFG_VALUE_t value;
        if (!ubloxcfg_valueFromString(valStr, item->type, item, &value))
        {
            log_warn("Could not parse value '%s' for item '%s' (type %s)!",
                valStr, item->name, ubloxcfg_typeStr(item->type));
            return false;
        }

        // Add key-value pair to the list
        if (!_cfgDbAddKeyVal(db, line, item->id, &value))
        {
            return false;
        }
    }
    // Hex key-value pair
    else if ( (line->line[0] == '0') && (line->line[1] == 'x') )
    {
        // Expect exactly two tokens separated by whitespace
        char *keyStr = strtok(line->line, kCfgTokSep);
        char *valStr = strtok(NULL, kCfgTokSep);
        char *none = strtok(NULL, kCfgTokSep);
        log_trace("- hexid-val: keyStr=[%s] valStr=[%s]", keyStr, valStr);
        if ( (keyStr == NULL) || (valStr == NULL) || (none != NULL) )
        {
            log_warn("Expected hex key-value pair!");
            return false;
        }

        uint32_t id = 0;
        int numChar = 0;
        if ( (sscanf(keyStr, "%"SCNx32"%n", &id, &numChar) != 1) || (numChar != (int)strlen(keyStr)) )
        {
            log_warn("Bad hex item ID (%s)!", keyStr);
            return false;
        }

        UBLOXCFG_VALUE_t value = { ._raw = 0 };
        bool valueOk = false;
        const UBLOXCFG_SIZE_t size = UBLOXCFG_ID2SIZE(id);
        switch (size)
        {
            case UBLOXCFG_SIZE_BIT:
                if (ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_L, NULL, &value))      // L
                {
                    valueOk = true;
                }
                break;
            case UBLOXCFG_SIZE_ONE:
                if (ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_U1, NULL, &value) ||   // U1, X1
                    ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_I1, NULL, &value))     // I1, E1
                {
                    valueOk = true;
                }
                break;
            case UBLOXCFG_SIZE_TWO:
                if (ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_U2, NULL, &value) ||   // U2, X2
                    ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_I2, NULL, &value))     // I2, E2
                {
                    valueOk = true;
                }
                break;
            case UBLOXCFG_SIZE_FOUR:
                if (ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_U4, NULL, &value) ||   // U4, X4
                    ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_I4, NULL, &value) ||   // I4, E4
                    ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_R4, NULL, &value))     // R4
                {
                    valueOk = true;
                }
                break;
            case UBLOXCFG_SIZE_EIGHT:
                if (ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_U8, NULL, &value) ||   // U8, X8
                    ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_I8, NULL, &value) ||   // I8, E8
                    ubloxcfg_valueFromString(valStr, UBLOXCFG_TYPE_R8, NULL, &value))     // R8
                {
                    valueOk = true;
                }
                break;
            default:
                log_warn("Bad size from item ID (%s)!", keyStr);
                return false;
        }
        if (!valueOk)
        {
            log_warn("Bad value '%s' for item '%s'!", valStr, keyStr);
            return false;
        }

        // Add key-value pari to the list
        if (!_cfgDbAddKeyVal(db, line, id, &value))
        {
            return false;
        }
    }
    // Output message rate config
    else if ( (strncmp(line->line, "UBX-",  4) == 0) ||
              (strncmp(line->line, "NMEA-", 5) == 0) ||
              (strncmp(line->line, "RTCM-", 5) == 0) )
    {
        // <msgname> <uart1> <uart2> <spi> <i2c> <usb>
        char *name  = strtok(line->line, kCfgTokSep);
        char *uart1 = strtok(NULL, kCfgTokSep);
        char *uart2 = strtok(NULL, kCfgTokSep);
        char *spi   = strtok(NULL, kCfgTokSep);
        char *i2c   = strtok(NULL, kCfgTokSep);
        char *usb   = strtok(NULL, kCfgTokSep);
        log_trace("- msgrate: name=[%s] uart1=[%s] uart2=[%s] spi=[%s] i2c=[%s] usb=[%s]", name, uart1, uart2, spi, i2c, usb);
        if ( (name == NULL) || (uart1 == NULL) || (uart2 == NULL) || (spi == NULL) || (i2c == NULL) || (usb == NULL) )
        {
            log_warn("Expected output message rate config!");
            return false;
        }

        // Get config items for this message
        const UBLOXCFG_MSGRATE_t *items = ubloxcfg_getMsgRateCfg(name);
        if (items == NULL)
        {
            log_warn("Unknown message name (%s)!", name);
            return false;
        }

        // Generate config key-value pairs...
        MSGRATE_CFG_t msgrateCfg[] =
        {
            { .name = "UART1", .rate = uart1, .item = items->itemUart1 },
            { .name = "UART2", .rate = uart2, .item = items->itemUart2 },
            { .name = "SPI",   .rate = spi,   .item = items->itemSpi },
            { .name = "I2C",   .rate = i2c,   .item = items->itemI2c },
            { .name = "USB",   .rate = usb,   .item = items->itemUsb }
        };
        for (int ix = 0; ix < NUMOF(msgrateCfg); ix++)
        {
            // "-" = don't configure, skip
            if ( (msgrateCfg[ix].rate[0] == '-') && (msgrateCfg[ix].rate[1] == '\0') )
            {
                continue;
            }

            // Can configure?
            if (msgrateCfg[ix].item == NULL)
            {
                log_warn("No configuration available for %s output rate on part %s!", name, msgrateCfg[ix].name);
                return false;
            }

            // Get and check value
            UBLOXCFG_VALUE_t value;
            if (!ubloxcfg_valueFromString(msgrateCfg[ix].rate, msgrateCfg[ix].item->type, msgrateCfg[ix].item, &value))
            {
                log_warn("Bad output message rate value (%s) for port %s!", msgrateCfg[ix].rate, msgrateCfg[ix].name);
                return false;
            }

            // Add key-value pair to the list
            if (!_cfgDbAddKeyVal(db, line, msgrateCfg[ix].item->id, &value))
            {
                return false;
            }
        }
    }
    // Port configuration
    else if ( (strncmp(line->line, "UART1 ", 5) == 0) ||
              (strncmp(line->line, "UART2 ", 5) == 0) ||
              (strncmp(line->line, "SPI ",   4) == 0) ||
              (strncmp(line->line, "I2C ",   4) == 0) ||
              (strncmp(line->line, "USB ",   4) == 0) )
    {
        char *port     = strtok(line->line, kCfgTokSep);
        char *baudrate = strtok(NULL, kCfgTokSep);
        char *inprot   = strtok(NULL, kCfgTokSep);
        char *outprot  = strtok(NULL, kCfgTokSep);
        log_trace("- portcfg: port=[%s] baud=[%s] inport=[%s] outprot=[%s]", port, baudrate, inprot, outprot);
        if ( (port == NULL) || (baudrate == NULL) || (inprot == NULL) || (outprot == NULL) )
        {
            log_warn("Expected port config!");
            return false;
        }

        // Acceptable baudrates (for UART)
        BAUD_CFG_t baudCfg[] =
        {
            { .str =   "9600", .val =   9600 },
            { .str =  "19200", .val =  19200 },
            { .str =  "38400", .val =  38400 },
            { .str =  "57600", .val =  57600 },
            { .str = "115200", .val = 115200 },
            { .str = "230400", .val = 230400 },
            { .str = "460800", .val = 460800 },
            { .str = "921600", .val = 921600 }
        };
        // Configurable ports and the corresponding configuration
        PORT_CFG_t portCfg[] =
        {
            {
              .name = "UART1", .baudrateId = UBLOXCFG_CFG_UART1_BAUDRATE_ID,
              .inProt  = { { .name = "UBX",    .id = UBLOXCFG_CFG_UART1INPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_UART1INPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_UART1INPROT_RTCM3X_ID } },
              .outProt = { { .name = "UBX",    .id = UBLOXCFG_CFG_UART1OUTPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_UART1OUTPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_UART1OUTPROT_RTCM3X_ID } }
            },
            {
              .name = "UART2", .baudrateId = UBLOXCFG_CFG_UART2_BAUDRATE_ID,
              .inProt  = { { .name = "UBX",    .id = UBLOXCFG_CFG_UART2INPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_UART2INPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_UART2INPROT_RTCM3X_ID } },
              .outProt = { { .name = "UBX",    .id = UBLOXCFG_CFG_UART2OUTPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_UART2OUTPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_UART2OUTPROT_RTCM3X_ID } }
            },
            {
              .name = "SPI", .baudrateId = 0,
              .inProt  = { { .name = "UBX",    .id = UBLOXCFG_CFG_SPIINPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_SPIINPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_SPIINPROT_RTCM3X_ID } },
              .outProt = { { .name = "UBX",    .id = UBLOXCFG_CFG_SPIOUTPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_SPIOUTPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_SPIOUTPROT_RTCM3X_ID } }
            },
            {
              .name = "I2C", .baudrateId = 0,
              .inProt  = { { .name = "UBX",    .id = UBLOXCFG_CFG_I2CINPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_I2CINPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_I2CINPROT_RTCM3X_ID } },
              .outProt = { { .name = "UBX",    .id = UBLOXCFG_CFG_I2COUTPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_I2COUTPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_I2COUTPROT_RTCM3X_ID } }
            },
            {
              .name = "USB", .baudrateId = 0,
              .inProt  = { { .name = "UBX",    .id = UBLOXCFG_CFG_USBINPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_USBINPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_USBINPROT_RTCM3X_ID } },
              .outProt = { { .name = "UBX",    .id = UBLOXCFG_CFG_USBOUTPROT_UBX_ID },
                           { .name = "NMEA",   .id = UBLOXCFG_CFG_USBOUTPROT_NMEA_ID },
                           { .name = "RTCM3",  .id = UBLOXCFG_CFG_USBOUTPROT_RTCM3X_ID } }
            },
        };
        // Find port config info
        PORT_CFG_t *cfg = NULL;
        for (int ix = 0; ix < NUMOF(portCfg); ix++)
        {
            if (strcmp(port, portCfg[ix].name) == 0)
            {
                cfg = &portCfg[ix];
                break;
            }
        }
        if (cfg == NULL)
        {
            log_warn("Cannot configure port '%s'!", port);
            return false;
        }

        // Config baudrate
        if ( (cfg->baudrateId != 0) && (baudrate[0] != '-') && (baudrate[1] != '\0') ) // number or "-" for UART1, 2
        {
            bool baudrateOk = false;
            for (int ix = 0; ix < NUMOF(baudCfg); ix++)
            {
                // Add key-value pair to the list
                if (strcmp(baudCfg[ix].str, baudrate) == 0)
                {
                    UBLOXCFG_VALUE_t value = { .U4 = baudCfg[ix].val };
                    if (!_cfgDbAddKeyVal(db, line, cfg->baudrateId, &value))
                    {
                        return false;
                    }
                    baudrateOk = true;
                }
            }
            if (!baudrateOk)
            {
                log_warn("Illegal baudrate value '%s'!", baudrate);
                return false;
            }
        }
        else if ( (baudrate[0] != '-') && (baudrate[1] != '\0') ) // other ports have no baudrate, so only "-" is acceptable
        {
            log_warn("Baudrate value specified for port '%s'!", port);
            return false;
        }

        // Input/output protocol filters
        if ( (inprot[0] != '-') && (inprot[1] != '\0') )
        {
            if (!_cfgDbApplyProtfilt(db, line, inprot, cfg->inProt, NUMOF(cfg->inProt)))
            {
                return false;
            }
        }
        if ( (outprot[0] != '-') && (outprot[1] != '\0') )
        {
            if (!_cfgDbApplyProtfilt(db, line, outprot, cfg->outProt, NUMOF(cfg->outProt)))
            {
                return false;
            }
        }
    }
    else
    {
        log_warn("Unknown config (%s)!", line->line);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------------------------------------------------

static bool _cfgDbAddKeyVal(CFG_DB_t *db, IO_LINE_t *line, const uint32_t id, const UBLOXCFG_VALUE_t *value)
{
    if (db->nKv >= db->maxKv)
    {
        log_warn("Too many items!");
        return false;
    }

    for (int ix = 0; ix < db->nKv; ix++)
    {
        if (db->kv[ix].id == id)
        {
            const UBLOXCFG_ITEM_t *item = ubloxcfg_getItemById(id);
            if (item != NULL)
            {
                log_warn("Duplicate item '%s'!", item->name);
            }
            else
            {
                log_warn("Duplicate item!");
            }
            return false;
        }
    }

    db->kv[db->nKv].id = id;
    db->kv[db->nKv].val = *value;
    char debugStr[UBLOXCFG_MAX_KEYVAL_STR_SIZE];
    if (ubloxcfg_stringifyKeyVal(debugStr, sizeof(debugStr), &db->kv[db->nKv]))
    {
        log_trace("Adding item %d: %s", db->nKv + 1, debugStr);
    }
    db->nKv++;

    return true;
}

// ---------------------------------------------------------------------------------------------------------------------

static bool _cfgDbApplyProtfilt(CFG_DB_t *db, IO_LINE_t *line, char *protfilt, const PROTFILT_CFG_t *protfiltCfg, const int nProtfiltCfg)
{
    char *pCfgFilt = strtok(protfilt, kCfgPartSep);
    while (pCfgFilt != NULL)
    {
        const bool protEna = pCfgFilt[0] != '!';
        if (pCfgFilt[0] == '!')
        {
            pCfgFilt++;
        }
        bool found = false;
        for (int ix = 0; ix < nProtfiltCfg; ix++)
        {
            if (strcmp(protfiltCfg[ix].name, pCfgFilt) == 0)
            {
                UBLOXCFG_VALUE_t value = { .L = protEna };
                if (!_cfgDbAddKeyVal(db, line, protfiltCfg[ix].id, &value))
                {
                    return false;
                }
                found = true;
                break;
            }
        }
        if (!found)
        {
            log_warn("Illegal protocol filter '%s'!", pCfgFilt);
            return false;
        }
        pCfgFilt = strtok(NULL, kCfgPartSep);
    }
    return true;
}

/* ****************************************************************************************************************** */
