// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/serial_8250.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/platform_data/i2c-xiic.h>
#include <linux/platform_data/i2c-ocores.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/spi/spi.h>
#include <linux/spi/xilinx_spi.h>
#include <linux/spi/altera.h>
#include <net/devlink.h>
#include <linux/i2c.h>
#include <linux/mtd/mtd.h>
#include <linux/miscdevice.h>
#include <linux/nvmem-consumer.h>
#include <linux/version.h>
#include <linux/crc16.h>
#include <linux/string.h>
#include <linux/timekeeping.h>

#include <linux/version.h>

/* Compatibility for kernel API changes; see DRV/README.md. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 2, 0)
#define timer_delete_sync(t) timer_delete_sync(t)
#endif
#ifndef timer_container_of
#define timer_container_of(var, timer, field) from_timer(var, timer, field)
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(10, 3))
#define OCP_BIN_ATTR_CONST const
#else
#define OCP_BIN_ATTR_CONST
#endif

/*---------------------------------------------------------------------------*/
#ifndef MRO50_IOCTL_H
#define MRO50_IOCTL_H

#define MRO50_READ_FINE		_IOR('M', 1, u32 *)
#define MRO50_READ_COARSE	_IOR('M', 2, u32 *)
#define MRO50_ADJUST_FINE	_IOW('M', 3, u32)
#define MRO50_ADJUST_COARSE	_IOW('M', 4, u32)
#define MRO50_READ_TEMP		_IOR('M', 5, u32 *)
#define MRO50_READ_CTRL		_IOR('M', 6, u32 *)
#define MRO50_SAVE_COARSE	_IO('M', 7)
#define MRO50_READ_EEPROM_BLOB	_IOR('M', 8, u8 *)
#define MRO50_WRITE_EEPROM_BLOB	_IOW('M', 8, u8 *)
#define MRO50_READ_EXTENDED_EEPROM_BLOB	_IOR('M', 9, u8 *)
#define MRO50_WRITE_EXTENDED_EEPROM_BLOB	_IOW('M', 9, u8 *)
#define MRO50_BOARD_CONFIG_READ _IOR('M', 10, u32 *)
#define MRO50_BOARD_CONFIG_WRITE _IOW('M', 10, u32 *)

#endif /* MRO50_IOCTL_H */
/*---------------------------------------------------------------------------*/

#ifndef PCI_VENDOR_ID_FACEBOOK
#define PCI_VENDOR_ID_FACEBOOK 0x1d9b
#endif

#ifndef PCI_DEVICE_ID_FACEBOOK_TIMECARD
#define PCI_DEVICE_ID_FACEBOOK_TIMECARD 0x0400
#endif

#ifndef PCI_VENDOR_ID_CELESTICA
#define PCI_VENDOR_ID_CELESTICA 0x18d4
#endif

#ifndef PCI_DEVICE_ID_CELESTICA_TIMECARD
#define PCI_DEVICE_ID_CELESTICA_TIMECARD 0x1008
#endif

#ifndef PCI_VENDOR_ID_OROLIA
#define PCI_VENDOR_ID_OROLIA 0x1ad7
#endif

#ifndef PCI_DEVICE_ID_OROLIA_ARTCARD
#define PCI_DEVICE_ID_OROLIA_ARTCARD 0xa000
#endif

static struct class timecard_class = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
	.owner		= THIS_MODULE,
#endif
	.name		= "timecard",
};

/*
 * These register blocks are controlled by synthesis generics, not by the
 * public core version.  Leave them inaccessible unless the operator has an
 * explicit contract for the loaded FPGA image.  Permissions are read-only so
 * the policy cannot change while a card is active.
 */
static bool clock_optional_registers;
module_param_named(clock_optional_registers, clock_optional_registers,
		   bool, 0444);
MODULE_PARM_DESC(clock_optional_registers,
	"allow Clock servo/log registers on a bitstream known to synthesize them");

static bool tod_optional_telemetry;
module_param_named(tod_optional_telemetry, tod_optional_telemetry, bool, 0444);
MODULE_PARM_DESC(tod_optional_telemetry,
	"allow ToD UTC/leap/GNSS/satellite registers on a known bitstream");

static bool irig_optional_features;
module_param_named(irig_optional_features, irig_optional_features, bool, 0444);
MODULE_PARM_DESC(irig_optional_features,
	"allow synthesis-optional IRIG controls for a contracted bitstream");

/*
 * The feature opt-ins above are not sufficient on their own.  The operator
 * must also pin them to the exact raw value read from the Image Version
 * register.  This prevents an opt-in intended for one FPGA build from silently
 * carrying over after a reflash.  Zero, invalid image words, firmware-loader
 * images and boards without the published Image register never match.
 */
static unsigned int optional_image_version;
module_param_named(optional_image_version, optional_image_version, uint, 0444);
MODULE_PARM_DESC(optional_image_version,
	"legacy exact raw FPGA Image Version word (requires optional_image_device)");

/*
 * A raw image word is not a device identity.  Binding one module-wide value to
 * every enumerated card could therefore enable optional MMIO on a second card
 * merely because it reports the same version.  Keep the legacy version option,
 * but require an exact PCI BDF alongside it.  The contract array provides the
 * same binding for multiple cards; each entry is <domain:bus:slot.func>=<raw>.
 */
static char *optional_image_device;
module_param_named(optional_image_device, optional_image_device, charp, 0444);
MODULE_PARM_DESC(optional_image_device,
	"PCI BDF bound to optional_image_version (for example 0000:03:00.0)");

#define OCP_OPTIONAL_IMAGE_CONTRACT_MAX 32
static char *optional_image_contracts[OCP_OPTIONAL_IMAGE_CONTRACT_MAX];
static unsigned int optional_image_contract_count;
module_param_array_named(optional_image_contracts, optional_image_contracts,
			 charp, &optional_image_contract_count, 0444);
MODULE_PARM_DESC(optional_image_contracts,
	"per-device exact-image contracts: BDF=raw-version[,BDF=raw-version...]");

#define CSR_BASE 0x0L
/* msi_base */
#define CSR_PCIE_MSI_BASE (CSR_BASE + 0x1800L)
/* ptm_requester */
#define CSR_PTM_REQUESTER_BASE (CSR_BASE + 0x3000L)
#define CSR_PTM_REQUESTER_CONTROL_ENABLE_OFFSET 0
#define CSR_PTM_REQUESTER_CONTROL_TRIGGER_OFFSET 1
#define CSR_PTM_REQUESTER_STATUS_VALID_OFFSET 0
#define CSR_PTM_REQUESTER_STATUS_BUSY_OFFSET 1

struct ptm_reg {
	u32 ctrl;
	u32 status;
	u32 phy_tx_delay;
	u32 phy_rx_delay;
	u32 master_time[2];
	u32 link_delay;
	u32 t1_time[2];
	u32 t4_time[2];
};

struct ocp_reg {
	u32	ctrl;
	u32	status;
	u32	select;
	u32	version;
	u32	time_ns;
	u32	time_sec;
	u32	__pad0[2];
	u32	adjust_ns;
	u32	adjust_sec;
	u32	__pad1[2];
	u32	offset_ns;
	u32	offset_window_ns;
	u32	__pad2[2];
	u32	drift_ns;
	u32	drift_window_ns;
	u32	__pad3[6];
	u32	servo_offset_p;
	u32	servo_offset_i;
	u32	servo_drift_p;
	u32	servo_drift_i;
	u32	status_offset;
	u32	status_drift;
};

#define OCP_CTRL_ENABLE		BIT(0)
#define OCP_CTRL_ADJUST_TIME	BIT(1)
#define OCP_CTRL_ADJUST_OFFSET	BIT(2)
#define OCP_CTRL_ADJUST_DRIFT	BIT(3)
#define OCP_CTRL_ADJUST_SERVO	BIT(8)
#define OCP_CTRL_READ_TIME_REQ	BIT(30)
#define OCP_CTRL_READ_TIME_DONE	BIT(31)

#define OCP_STATUS_IN_SYNC	BIT(0)
#define OCP_STATUS_IN_HOLDOVER	BIT(1)

#define OCP_SELECT_CLK_NONE	0
#define OCP_SELECT_CLK_REG	0xfe

#define FPGA_CORE_VERSION(_major, _minor) \
	(((u32)(_major) << 24) | ((u32)(_minor) << 16))

#define CLOCK_VERSION_STATUS		FPGA_CORE_VERSION(1, 2)
#define CLOCK_VERSION_OPTIONAL_REGS	FPGA_CORE_VERSION(1, 6)

struct tod_reg {
	u32	ctrl;
	u32	status;
	u32	uart_polarity;
	u32	version;
	u32	adj_sec;
	u32	local_offset;
	u32	__pad0[2];
	u32	uart_baud;
	u32	__pad1[3];
	u32	utc_status;
	u32	leap;
	u32	__pad2[2];
	u32 gnss_status;
	u32 num_sat;
};

#define TOD_CTRL_PROTOCOL			BIT(28)
#define TOD_CTRL_PROTOCOL_MASK		((1U << 3) - 1)
#define TOD_CTRL_PROTOCOL_SHIFT		28
#define TOD_CTRL_DISABLE_FMT_A		BIT(17)
#define TOD_CTRL_DISABLE_FMT_B		BIT(16)
#define TOD_CTRL_DISABLE_MESSAGE_SHIFT	16
#define TOD_CTRL_DISABLE_MESSAGE_MASK	(0xffU << TOD_CTRL_DISABLE_MESSAGE_SHIFT)
#define TOD_MESSAGE_NMEA_BASE_MASK	0x1bU
#define TOD_MESSAGE_NMEA_FULL_MASK	0x1fU
#define TOD_MESSAGE_UBX_BASE_MASK	0x07U
#define TOD_MESSAGE_UBX_FULL_MASK	0x1fU
#define TOD_MESSAGE_TSIP_MASK		0x1fU
#define TOD_MESSAGE_ESIP_MASK		0xffU
#define TOD_CTRL_ENABLE				BIT(0)
#define TOD_CTRL_GNSS_MASK			((1U << 4) - 1)
#define TOD_CTRL_GNSS_SHIFT			24

#define TOD_STATUS_PARSE_ERROR		BIT(0)
#define TOD_STATUS_CHECKSUM_ERROR	BIT(1)
#define TOD_STATUS_UART_ERROR		BIT(2)
#define TOD_STATUS_ERROR_MASK		(TOD_STATUS_PARSE_ERROR | \
					 TOD_STATUS_CHECKSUM_ERROR | \
					 TOD_STATUS_UART_ERROR)

#define TOD_VERSION_STATUS		FPGA_CORE_VERSION(1, 2)
#define TOD_VERSION_POLARITY		FPGA_CORE_VERSION(1, 3)
#define TOD_VERSION_ALL_ERRORS		FPGA_CORE_VERSION(1, 4)
#define TOD_VERSION_GNSS_SELECT		FPGA_CORE_VERSION(1, 5)
#define TOD_VERSION_UBX			FPGA_CORE_VERSION(1, 6)
#define TOD_VERSION_GNSS_TELEMETRY	FPGA_CORE_VERSION(1, 7)
#define TOD_VERSION_TSIP		FPGA_CORE_VERSION(1, 9)
#define TOD_VERSION_NMEA_STATUS_MESSAGE FPGA_CORE_VERSION(2, 0)
#define TOD_VERSION_ESIP		FPGA_CORE_VERSION(2, 1)
#define TOD_VERSION_NMEA_GNSS_TELEMETRY FPGA_CORE_VERSION(2, 2)
#define TOD_VERSION_PFEC		FPGA_CORE_VERSION(2, 3)
#define TOD_MESSAGE_PFEC_MASK		0x7fU
#define TOD_MASTER_VERSION_STATUS	FPGA_CORE_VERSION(1, 1)
#define TOD_MASTER_VERSION_POLARITY	FPGA_CORE_VERSION(1, 2)
#define TOD_MASTER_VERSION_GNSS		FPGA_CORE_VERSION(1, 3)
#define TOD_MASTER_VERSION_RMC		FPGA_CORE_VERSION(1, 4)
#define TOD_MASTER_VERSION_UTC		FPGA_CORE_VERSION(1, 6)
#define TOD_MASTER_STATUS_ERROR		BIT(0)
#define TOD_MASTER_CTRL_ENABLE		BIT(0)
#define TOD_MASTER_CTRL_GNSS_SHIFT	24
#define TOD_MASTER_CTRL_GNSS_MASK	(0xfU << TOD_MASTER_CTRL_GNSS_SHIFT)
#define TOD_MASTER_CTRL_MESSAGE_SHIFT	16
#define TOD_MASTER_DISABLE_RMC		BIT(0)
#define TOD_MASTER_DISABLE_ZDA		BIT(1)
#define TOD_MASTER_DISABLE_UTC		BIT(2)
#define TOD_MASTER_LOCAL_SIGN		BIT(31)
#define TOD_MASTER_LOCAL_HOUR_SHIFT	16
#define TOD_MASTER_LOCAL_HOUR_MASK	(0xfU << TOD_MASTER_LOCAL_HOUR_SHIFT)
#define TOD_MASTER_LOCAL_MINUTE_MASK	0x3fU
#define TOD_MASTER_LOCAL_MAX_MINUTES	((13 * 60) + 59)

#define TOD_STATUS_UTC_MASK			0xff
#define TOD_STATUS_UTC_VALID		BIT(8)
#define TOD_STATUS_LEAP_ANNOUNCE	BIT(12)
#define TOD_STATUS_LEAP_VALID		BIT(16)

#define TOD_UART_BAUD_MASK			((1U << 4) - 1)
#define TOD_UART_BAUD_SHIFT			0

#define TOD_GNSS_ANT_STATUS_MASK	((1U << 3) - 1)
#define TOD_GNSS_ANT_STATUS_SHIFT	0
#define TOD_GNSS_JAM_STATE_MASK		((1U << 2) - 1)
#define TOD_GNSS_JAM_STATE_SHIFT	3
#define TOD_GNSS_JAM_STRENGTH_MASK	((1U << 7) - 1)
#define TOD_GNSS_JAM_STRENGTH_SHIFT	5
#define TOD_GNSS_FIX_OK				BIT(16)
#define TOD_GNSS_FIX_MASK			((1U << 8) - 1)
#define TOD_GNSS_FIX_SHIFT			17
#define TOD_GNSS_SPOOF_STATE_MASK	((1U << 2) - 1)
#define TOD_GNSS_SPOOF_STATE_SHIFT	25
#define TOD_GNSS_FIX_SPOOF_VAL		BIT(28)
#define TOD_GNSS_ANT_JAM_VAL		BIT(29)

#define TOD_SAT_SEEN_MASK			((1U << 8) - 1)
#define TOD_SAT_SEEN_SHIFT			0
#define TOD_SAT_LOCKED_MASK			((1U << 8) - 1)
#define TOD_SAT_LOCKED_SHIFT		8
#define TOD_SAT_VAL					BIT(16)

struct ts_reg {
	u32	enable;
	u32	error;
	u32	polarity;
	u32	version;
	u32	__pad0[4];
	u32	cable_delay;
	u32	__pad1[3];
	u32	intr;
	u32	intr_mask;
	u32	event_count;
	u32	__pad2[1];
	u32	ts_count;
	u32	time_ns;
	u32	time_sec;
	u32	data_width;
	u32	data;
};

struct pps_reg {
	u32	ctrl;
	u32	status;
	u32	polarity;
	u32	version;
	u32	pulse_width;
	u32	__pad0[3];
	u32	cable_delay;
};

#define PPS_STATUS_FILTER_ERR	BIT(0)
#define PPS_STATUS_SUPERV_ERR	BIT(1)
#define PPS_STATUS_ERR_MASK	(PPS_STATUS_FILTER_ERR | PPS_STATUS_SUPERV_ERR)

/* PPS cable delay is signed magnitude: sign in bit 31, magnitude in 29:0. */
#define PPS_CABLE_DELAY_SIGN		BIT(31)
#define PPS_CABLE_DELAY_MAG_MASK	(BIT(30) - 1)
#define PPS_CABLE_DELAY_LEGACY_MASK	0x0000ffffU
#define PPS_VERSION_PULSE_WIDTH		FPGA_CORE_VERSION(1, 0)
#define PPS_MASTER_VERSION_POLARITY	FPGA_CORE_VERSION(1, 1)
#define PPS_SLAVE_VERSION_POLARITY	FPGA_CORE_VERSION(1, 2)
#define PPS_MASTER_VERSION_STATUS	FPGA_CORE_VERSION(1, 2)
#define PPS_SLAVE_VERSION_STATUS	FPGA_CORE_VERSION(1, 3)
#define PPS_MASTER_VERSION_CABLE_DELAY	FPGA_CORE_VERSION(1, 4)
#define PPS_VERSION_WIDE_CABLE_DELAY	FPGA_CORE_VERSION(1, 6)
#define PPS_MASTER_PULSE_WIDTH_MIN	1U
#define PPS_MASTER_PULSE_WIDTH_MAX	999U

struct img_reg {
	u32	version;
};

struct msi_reg {
	u32 	enable;
};

struct gpio_reg {
	u32	gpio1;
	u32	__pad0;
	u32	gpio2;
	u32	__pad1;
};

struct irig_master_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0;
	u32	version;
	u32	adj_sec;
	u32	control_bits;
};

#define IRIG_CTRL_ENABLE	BIT(0)
#define IRIG_CTRL_CODE_SHIFT	16
#define IRIG_CTRL_CODE_MASK	(0x7U << IRIG_CTRL_CODE_SHIFT)
#define IRIG_CTRL_MODE_SHIFT	24
#define IRIG_CTRL_MODE_MASK	(0x3U << IRIG_CTRL_MODE_SHIFT)
#define IRIG_CTRL_MODE_B	(1U << IRIG_CTRL_MODE_SHIFT)
#define IRIG_CTRL_AM		BIT(1)
#define IRIG_SLAVE_CTRL_YEAR_VALID BIT(8)
#define IRIG_CONTROL_BITS_MASK	0x07ffffffU
#define IRIG_MASTER_VERSION_MODE_CONTROL FPGA_CORE_VERSION(1, 2)
#define IRIG_SLAVE_VERSION_MODE_CONTROL  FPGA_CORE_VERSION(1, 3)
#define IRIG_MASTER_VERSION_AM		FPGA_CORE_VERSION(1, 5)
#define IRIG_SLAVE_VERSION_CODE_YEAR	FPGA_CORE_VERSION(1, 5)
#define IRIG_SLAVE_VERSION_AM		FPGA_CORE_VERSION(1, 6)
#define IRIG_YEAR_MASK			0x00000fffU
#define IRIG_YEAR_MIN			1970U
#define IRIG_YEAR_MAX			2069U
#define IRIG_M_CTRL_ENABLE	IRIG_CTRL_ENABLE

struct irig_slave_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0;
	u32	version;
	u32	adj_sec;
	u32	control_bits;
	u32	__pad1[2];
	u32	cable_delay;
	u32	year;
};

#define IRIG_S_CTRL_ENABLE	IRIG_CTRL_ENABLE

struct dcf_master_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0;
	u32	version;
	u32	adj_sec;
};

#define DCF_M_CTRL_ENABLE	BIT(0)

struct dcf_slave_reg {
	u32	ctrl;
	u32	status;
	u32	__pad0;
	u32	version;
	u32	adj_sec;
	u32	__pad1[3];
	u32	air_delay;
	u32	__pad2[3];
	u32	bit_position;
};

#define DCF_S_CTRL_ENABLE	BIT(0)

#define TIMECODE_STATUS_ERROR	BIT(0)
#define TIMECODE_VERSION_STATUS	FPGA_CORE_VERSION(1, 1)
#define TIMECODE_COR_SIGN	BIT(31)
#define TIMECODE_COR_MAG_MASK	0x7fffffffU
#define IRIG_CABLE_DELAY_MASK	0x0000ffffU
#define DCF_AIR_DELAY_MASK	0x3fffffffU
#define DCF_BIT_POSITION_MASK	0x0000003fU

static_assert(offsetof(struct irig_master_reg, control_bits) == 0x14);
static_assert(offsetof(struct irig_slave_reg, control_bits) == 0x14);
static_assert(offsetof(struct irig_slave_reg, cable_delay) == 0x20);
static_assert(offsetof(struct irig_slave_reg, year) == 0x24);
static_assert(offsetof(struct dcf_slave_reg, air_delay) == 0x20);
static_assert(offsetof(struct dcf_slave_reg, bit_position) == 0x30);
static_assert(offsetof(struct pps_reg, polarity) == 0x08);
static_assert(offsetof(struct pps_reg, version) == 0x0c);
static_assert(offsetof(struct pps_reg, pulse_width) == 0x10);
static_assert(offsetof(struct pps_reg, cable_delay) == 0x20);

struct signal_reg {
	u32	enable;
	u32	status;
	u32	polarity;
	u32	version;
	u32	__pad0[4];
	u32	cable_delay;
	u32	__pad1[3];
	u32	intr;
	u32	intr_mask;
	u32	__pad2[2];
	u32	start_ns;
	u32	start_sec;
	u32	pulse_ns;
	u32	pulse_sec;
	u32	period_ns;
	u32	period_sec;
	u32	repeat_count;
};

#define SIGNAL_VERSION_CABLE_DELAY	FPGA_CORE_VERSION(1, 2)
#define SIGNAL_VERSION_CURRENT_MAP	FPGA_CORE_VERSION(1, 3)
#define SIGNAL_CABLE_DELAY_MAX		0xffffU
#define SIGNAL_CTRL_ENABLE		BIT(0)
#define SIGNAL_CTRL_VALUES_VALID	BIT(1)

struct frequency_reg {
	u32	ctrl;
	u32	status;
};

struct board_config_reg {
	u32 mro50_serial_activate;
};

#define FREQ_STATUS_VALID	BIT(31)
#define FREQ_STATUS_ERROR	BIT(30)
#define FREQ_STATUS_OVERRUN	BIT(29)
#define FREQ_STATUS_MASK	(BIT(24) - 1)

struct ptp_ocp_flash_info {
	const char *name;
	int pci_offset;
	int data_size;
	void *data;
};

#define OCP_FIRMWARE_MAGIC_HEADER "OCPC"
struct ptp_ocp_firmware_header {
	char magic[4];
	__be16 pci_vendor_id;
	__be16 pci_device_id;
	__be32 image_size;
	__be16 hw_revision;
	__be16 crc;
};

struct ptp_ocp_i2c_info {
	const char *name;
	unsigned long fixed_rate;
	size_t data_size;
	void *data;
};

struct ptp_ocp_ext_info {
	int index;
	irqreturn_t (*irq_fcn)(int irq, void *priv);
	int (*enable)(void *priv, struct ptp_clock_request *rq,
		      u32 req, bool enable);
};

struct ptp_ocp_ext_src {
	void __iomem		*mem;
	struct ptp_ocp		*bp;
	struct ptp_ocp_ext_info	*info;
	int			irq_vec;
};

enum ptp_ocp_sma_mode {
	SMA_MODE_IN,
	SMA_MODE_OUT,
};

struct ptp_ocp_sma_connector {
	enum	ptp_ocp_sma_mode mode;
	bool	fixed_fcn;
	bool	fixed_dir;
	bool	disabled;
	u8	default_fcn;
};

struct ocp_attr_group {
	u64 cap;
	const struct attribute_group *group;
};

#define OCP_CAP_BASIC	BIT(0)
#define OCP_CAP_SIGNAL	BIT(1)
#define OCP_CAP_FREQ	BIT(2)

struct ptp_ocp_signal {
	ktime_t		period;
	ktime_t		pulse;
	ktime_t		phase;
	ktime_t		start;
	int		duty;
	u32		repeat_count;
	u16		cable_delay;
	bool		polarity;
	bool		running;
};

struct ptp_ocp_serial_port {
	int line;
	int baud;
};

#define OCP_BOARD_ID_LEN			13
#define OCP_SERIAL_LEN				6
#define OCP_CONFIG_SIZE				4096
#define OCP_SECOND_IN_NANOSECOND 	1000000000

enum {
	PORT_GNSS,
	PORT_GNSS2,
	PORT_MAC, /* miniature atomic clock */
	PORT_NMEA,

	__PORT_COUNT,
};

struct ptp_ocp {
	struct pci_dev		*pdev;
	struct device		dev;
	spinlock_t		lock;
	struct mutex		mutex;
	struct ocp_reg __iomem	*reg;
	struct tod_reg __iomem	*tod;
	struct pps_reg __iomem	*pps_to_ext;
	struct pps_reg __iomem	*pps_to_clk;
	struct board_config_reg __iomem	*board_config;
	struct gpio_reg __iomem	*pps_select;
	struct gpio_reg __iomem	*sma_map1;
	struct gpio_reg __iomem	*sma_map2;
	struct gpio_reg __iomem	*ext_ctrl;
	struct irig_master_reg	__iomem *irig_out;
	struct irig_slave_reg	__iomem *irig_in;
	struct dcf_master_reg	__iomem *dcf_out;
	struct dcf_slave_reg	__iomem *dcf_in;
	struct tod_reg		__iomem *nmea_out;
	struct frequency_reg	__iomem *freq_in[4];
	struct msi_reg __iomem	*msi;
	struct ptm_reg __iomem	*ptm;
	struct ptp_ocp_ext_src	*signal_out[4];
	struct ptp_ocp_ext_src	*pps;
	struct ptp_ocp_ext_src	*ts0;
	struct ptp_ocp_ext_src	*ts1;
	struct ptp_ocp_ext_src	*ts2;
	struct ptp_ocp_ext_src	*ts3;
	struct ptp_ocp_ext_src	*ts4;
	struct ocp_art_osc_reg	__iomem *osc;
	struct ocp_art_gpio_reg __iomem *art_sma;
	struct img_reg __iomem	*image;
	struct ptp_clock	*ptp;
	struct ptp_clock_info	ptp_info;
	struct platform_device	*i2c_ctrl;
	struct platform_device	*i2c_mac;
	struct platform_device	*spi_flash;
	struct clk_hw		*i2c_clk;
	struct timer_list	watchdog;
	const struct ocp_attr_group *attr_tbl;
	const struct ptp_ocp_eeprom_map *eeprom_map;
	struct dentry		*debug_root;
	struct miscdevice	mro50;
	time64_t		gnss_lost;
	int			id;
	int			n_irqs;
	struct ptp_ocp_serial_port	port[__PORT_COUNT];
	bool			fw_loader;
	u8			fw_tag;
	u16			fw_version;
	u8			board_id[OCP_BOARD_ID_LEN];
	u8			serial[OCP_SERIAL_LEN];
	bool			has_eeprom_data;
	int			i2c_count;
	u32			pps_req_map;
	int			flash_start;
	int			config_start;
	u32			utc_tai_offset;
	u32			ts_window_adjust;
	u64			fw_cap;
	struct ptp_ocp_signal	signal[4];
	struct ptp_ocp_sma_connector sma[4];
	const struct ocp_sma_op *sma_op;
	struct system_time_snapshot snapshot;
	u64			ptm_t1_prev;
	u64			ptm_t4_prev;
};

static bool
ptp_ocp_core_version_at_least(u32 __iomem *version_reg, u32 minimum)
{
	u32 version = ioread32(version_reg);

	return version && version != U32_MAX && version >= minimum;
}

static int
ptp_ocp_contract_entry_for_device(const char *entry, const char *device,
				  u32 *expected)
{
	const char *separator;
	size_t device_len;
	u32 value;

	if (!entry || !*entry)
		return false;
	separator = strchr(entry, '=');
	if (!separator)
		return false;

	device_len = separator - entry;
	if (device_len != strlen(device) ||
	    strncasecmp(entry, device, device_len))
		return false;

	/* A matching BDF with a malformed value invalidates the binding. */
	if (!separator[1] || strchr(separator + 1, '=') ||
	    kstrtou32(separator + 1, 0, &value) ||
	    !value || value == U32_MAX)
		return -EINVAL;

	*expected = value;
	return true;
}

static bool
ptp_ocp_optional_image_contract_expected(struct ptp_ocp *bp, u32 *expected)
{
	const char *device = pci_name(bp->pdev);
	u32 candidate, selected = 0;
	bool found = false;
	unsigned int i;
	int match;

	/* The legacy scalar is safe only when it names this exact PCI function. */
	if (optional_image_device &&
	    !strcasecmp(optional_image_device, device)) {
		if (!optional_image_version || optional_image_version == U32_MAX)
			return false;
		selected = optional_image_version;
		found = true;
	}

	for (i = 0; i < optional_image_contract_count; i++) {
		match = ptp_ocp_contract_entry_for_device(
			optional_image_contracts[i], device, &candidate);
		if (match < 0)
			return false;
		if (!match)
			continue;
		/* Conflicting contracts for one function are ambiguous: fail closed. */
		if (found && selected != candidate)
			return false;
		selected = candidate;
		found = true;
	}

	if (!found)
		return false;
	*expected = selected;
	return true;
}

static bool
ptp_ocp_optional_image_contract_matches(struct ptp_ocp *bp)
{
	u32 expected, image_version;

	if (!bp->image || bp->fw_loader ||
	    !ptp_ocp_optional_image_contract_expected(bp, &expected))
		return false;

	image_version = ioread32(&bp->image->version);
	if (!image_version || image_version == U32_MAX)
		return false;

	return image_version == expected;
}

static bool
ptp_ocp_clock_has_status(struct ptp_ocp *bp)
{
	return ptp_ocp_core_version_at_least(&bp->reg->version,
					     CLOCK_VERSION_STATUS);
}

static bool
ptp_ocp_clock_optional_registers_enabled(struct ptp_ocp *bp)
{
	return clock_optional_registers &&
		ptp_ocp_optional_image_contract_matches(bp) &&
		ptp_ocp_core_version_at_least(&bp->reg->version,
					      CLOCK_VERSION_OPTIONAL_REGS);
}

static bool
ptp_ocp_irig_optional_features_enabled(struct ptp_ocp *bp)
{
	return irig_optional_features &&
		ptp_ocp_optional_image_contract_matches(bp);
}

static bool
ptp_ocp_tod_optional_utc_enabled(struct ptp_ocp *bp)
{
	u32 ctrl, protocol, version;

	if (!tod_optional_telemetry || !bp->tod ||
	    !ptp_ocp_optional_image_contract_matches(bp))
		return false;
	version = ioread32(&bp->tod->version);
	if (!version || version == U32_MAX)
		return false;
	ctrl = ioread32(&bp->tod->ctrl);
	protocol = (ctrl >> TOD_CTRL_PROTOCOL_SHIFT) & TOD_CTRL_PROTOCOL_MASK;

	switch (protocol) {
	case 0: /* NMEA UTC/status message */
		return version >= TOD_VERSION_NMEA_STATUS_MESSAGE;
	case 1: /* UBX NAV-TIMEUTC / NAV-TIMELS */
		return version >= TOD_VERSION_UBX;
	case 2: /* TSIP timing information */
		return version >= TOD_VERSION_TSIP;
	case 3: /* ESIP CRW */
		return version >= TOD_VERSION_ESIP;
	case 4: /* PFEC GNTPS_A */
		return version >= TOD_VERSION_PFEC;
	default:
		return false;
	}
}

static bool
ptp_ocp_tod_optional_gnss_enabled(struct ptp_ocp *bp)
{
	u32 ctrl, protocol, version;

	if (!tod_optional_telemetry || !bp->tod ||
	    !ptp_ocp_optional_image_contract_matches(bp))
		return false;
	version = ioread32(&bp->tod->version);
	if (!version || version == U32_MAX)
		return false;
	ctrl = ioread32(&bp->tod->ctrl);
	protocol = (ctrl >> TOD_CTRL_PROTOCOL_SHIFT) & TOD_CTRL_PROTOCOL_MASK;

	switch (protocol) {
	case 0: /* NMEA GxGNS/GxGSV status was added in 2.2. */
		return version >= TOD_VERSION_NMEA_GNSS_TELEMETRY;
	case 1: /* UBX GNSS telemetry was added in 1.7. */
		return version >= TOD_VERSION_GNSS_TELEMETRY;
	case 2:
		return version >= TOD_VERSION_TSIP;
	case 3:
		return version >= TOD_VERSION_ESIP;
	case 4: /* PFEC GNTPS_B */
		return version >= TOD_VERSION_PFEC;
	default:
		return false;
	}
}

#define OCP_REQ_TIMESTAMP	BIT(0)
#define OCP_REQ_PPS		BIT(1)

struct ocp_resource {
	unsigned long offset;
	int size;
	int irq_vec;
	int (*setup)(struct ptp_ocp *bp, struct ocp_resource *r);
	void *extra;
	unsigned long bp_offset;
	const char * const name;
};

struct ocp_driver_data {
	struct ocp_resource *ocp_resource_msi;
	struct ocp_resource *ocp_resource_msix;
};

static int ptp_ocp_register_mem(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_register_i2c(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_register_spi(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_register_serial(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_register_ext(struct ptp_ocp *bp, struct ocp_resource *r);
static int ptp_ocp_fb_board_init(struct ptp_ocp *bp, struct ocp_resource *r);
static irqreturn_t ptp_ocp_ts_irq(int irq, void *priv);
static irqreturn_t ptp_ocp_signal_irq(int irq, void *priv);
static int ptp_ocp_ts_enable(void *priv, struct ptp_clock_request *rq,
			     u32 req, bool enable);
static int ptp_ocp_signal_from_perout(struct ptp_ocp *bp, int gen,
				      struct ptp_perout_request *req);
static int ptp_ocp_signal_enable(void *priv, struct ptp_clock_request *rq,
				 u32 req, bool enable);
static int ptp_ocp_sma_store(struct ptp_ocp *bp, const char *buf, int sma_nr);
static void ptp_ocp_link_child(struct ptp_ocp *bp, const char *name,
			       const char *link);

static int ptp_ocp_art_board_init(struct ptp_ocp *bp, struct ocp_resource *r);

static const struct ocp_attr_group fb_timecard_groups[];
static const struct ocp_sma_op ocp_fb_sma_op;

static const struct ocp_attr_group art_timecard_groups[];
static const struct ocp_sma_op ocp_art_sma_op;

struct ptp_ocp_eeprom_map {
	u16	off;
	u16	len;
	u32	bp_offset;
	const void * const tag;
};

#define EEPROM_ENTRY(addr, member)				\
	.off = addr,						\
	.len = sizeof_field(struct ptp_ocp, member),		\
	.bp_offset = offsetof(struct ptp_ocp, member)

#define BP_MAP_ENTRY_ADDR(bp, map) ({				\
	(void *)((uintptr_t)(bp) + (map)->bp_offset);		\
})

static struct ptp_ocp_eeprom_map fb_eeprom_map[] = {
	{ EEPROM_ENTRY(0x43, board_id) },
	{ EEPROM_ENTRY(0x00, serial), .tag = "mac" },
	{ }
};

static struct ptp_ocp_eeprom_map art_eeprom_map[] = {
	{ EEPROM_ENTRY(0x200 + 0x43, board_id) },
	{ EEPROM_ENTRY(0x200 + 0x63, serial) },
	{ }
};

#define bp_assign_entry(bp, res, val) ({				\
	uintptr_t addr = (uintptr_t)(bp) + (res)->bp_offset;		\
	*(typeof(val) *)addr = val;					\
})

#define OCP_RES_LOCATION(member) \
	.name = #member, .bp_offset = offsetof(struct ptp_ocp, member)

#define OCP_MEM_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_mem

#define OCP_SERIAL_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_serial

#define OCP_I2C_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_i2c

#define OCP_SPI_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_spi

#define OCP_EXT_RESOURCE(member) \
	OCP_RES_LOCATION(member), .setup = ptp_ocp_register_ext

/* This is the MSI vector mapping used.
 * 0: PPS (TS5)
 * 1: TS0
 * 2: TS1
 * 3: GNSS1
 * 4: GNSS2
 * 5: MAC
 * 6: TS2
 * 7: I2C controller
 * 8: HWICAP (notused)
 * 9: SPI Flash
 * 10: NMEA
 * 11: Signal Generator 1
 * 12: Signal Generator 2
 * 13: Signal Generator 3
 * 14: Signal Generator 4
 * 15: TS3
 * 16: TS4
 --
 * 8: Orolia TS1
 * 10: Orolia TS2
 * 11: Orolia TS0 (GNSS)
 * 12: Orolia PPS
 * 14: Orolia TS3
 * 15: Orolia TS4
 */

static struct ocp_resource ocp_fb_resource_rev1[] = {
	{
		OCP_MEM_RESOURCE(reg),
		.offset = 0x01000000, .size = 0x10000,
	},
	{
		OCP_EXT_RESOURCE(ts0),
		.offset = 0x01010000, .size = 0x10000, .irq_vec = 1,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 0,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts1),
		.offset = 0x01020000, .size = 0x10000, .irq_vec = 2,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 1,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts2),
		.offset = 0x01060000, .size = 0x10000, .irq_vec = 6,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 2,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts3),
		.offset = 0x01110000, .size = 0x10000, .irq_vec = 15,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 3,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts4),
		.offset = 0x01120000, .size = 0x10000, .irq_vec = 16,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 4,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	/* Timestamp for PHC and/or PPS generator */
	{
		OCP_EXT_RESOURCE(pps),
		.offset = 0x010C0000, .size = 0x10000, .irq_vec = 0,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 5,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[0]),
		.offset = 0x010D0000, .size = 0x10000, .irq_vec = 11,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 1,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[1]),
		.offset = 0x010E0000, .size = 0x10000, .irq_vec = 12,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 2,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[2]),
		.offset = 0x010F0000, .size = 0x10000, .irq_vec = 13,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 3,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[3]),
		.offset = 0x01100000, .size = 0x10000, .irq_vec = 14,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 4,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_MEM_RESOURCE(pps_to_ext),
		.offset = 0x01030000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(pps_to_clk),
		.offset = 0x01040000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(tod),
		.offset = 0x01050000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(irig_in),
		.offset = 0x01070000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(irig_out),
		.offset = 0x01080000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(dcf_in),
		.offset = 0x01090000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(dcf_out),
		.offset = 0x010A0000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(nmea_out),
		.offset = 0x010B0000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(image),
		.offset = 0x00020000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(ext_ctrl),
		.offset = 0x00100000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(pps_select),
		.offset = 0x00130000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(sma_map1),
		.offset = 0x00140000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(sma_map2),
		.offset = 0x00220000, .size = 0x1000,
	},
	{
		OCP_I2C_RESOURCE(i2c_ctrl),
		.offset = 0x00150000, .size = 0x10000, .irq_vec = 7,
		.extra = &(struct ptp_ocp_i2c_info) {
			.name = "xiic-i2c",
			.fixed_rate = 50000000,
			.data_size = sizeof(struct xiic_i2c_platform_data),
			.data = &(struct xiic_i2c_platform_data) {
				.num_devices = 3,
				.devices = (struct i2c_board_info[]) {
					{ I2C_BOARD_INFO("24c02", 0x50) },
					{ I2C_BOARD_INFO("24mac402", 0x58),
					  .platform_data = "mac" },
					{ I2C_BOARD_INFO("timecard-led-mux", 0x70) },
				},
			},
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_GNSS]),
		.offset = 0x00160000 + 0x1000, .irq_vec = 3,
		.extra = &(struct ptp_ocp_serial_port) {
			.baud = 115200,
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_GNSS2]),
		.offset = 0x00170000 + 0x1000, .irq_vec = 4,
		.extra = &(struct ptp_ocp_serial_port) {
			.baud = 115200,
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_MAC]),
		.offset = 0x00180000 + 0x1000, .irq_vec = 5,
		.extra = &(struct ptp_ocp_serial_port) {
			.baud = 57600,
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_NMEA]),
		.offset = 0x00190000 + 0x1000, .irq_vec = 10,
	},
	{
		OCP_SPI_RESOURCE(spi_flash),
		.offset = 0x00310000, .size = 0x10000, .irq_vec = 9,
		.extra = &(struct ptp_ocp_flash_info) {
			.name = "xilinx_spi", .pci_offset = 0,
			.data_size = sizeof(struct xspi_platform_data),
			.data = &(struct xspi_platform_data) {
				.num_chipselect = 1,
				.bits_per_word = 8,
				.num_devices = 1,
				.devices = &(struct spi_board_info) {
					.modalias = "spi-nor",
				},
			},
		},
	},
	{
		OCP_MEM_RESOURCE(freq_in[0]),
		.offset = 0x01200000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[1]),
		.offset = 0x01210000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[2]),
		.offset = 0x01220000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[3]),
		.offset = 0x01230000, .size = 0x10000,
	},
	{
		.setup = ptp_ocp_fb_board_init,
	},
	{ }
};

static struct ocp_resource ocp_fb_resource_rev2[] = {
	{
		OCP_MEM_RESOURCE(msi),
		.offset = CSR_PCIE_MSI_BASE, .size = 0x4,
	},
	{
		OCP_MEM_RESOURCE(ptm),
		.offset = CSR_PTM_REQUESTER_BASE, .size = 0x800,
	},
	{
		OCP_MEM_RESOURCE(reg),
		.offset = 0x03000000, .size = 0x10000,
	},
	{
		OCP_EXT_RESOURCE(ts0),
		.offset = 0x03010000, .size = 0x10000, .irq_vec = 33,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 0,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts1),
		.offset = 0x03020000, .size = 0x10000, .irq_vec = 34,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 1,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts2),
		.offset = 0x03060000, .size = 0x10000, .irq_vec = 38,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 2,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts3),
		.offset = 0x03110000, .size = 0x10000, .irq_vec = 47,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 3,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts4),
		.offset = 0x03120000, .size = 0x10000, .irq_vec = 48,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 4,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	/* Timestamp for PHC and/or PPS generator */
	{
		OCP_EXT_RESOURCE(pps),
		.offset = 0x030C0000, .size = 0x10000, .irq_vec = 32,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 5,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[0]),
		.offset = 0x030D0000, .size = 0x10000, .irq_vec = 43,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 1,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[1]),
		.offset = 0x030E0000, .size = 0x10000, .irq_vec = 44,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 2,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[2]),
		.offset = 0x030F0000, .size = 0x10000, .irq_vec = 45,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 3,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(signal_out[3]),
		.offset = 0x03100000, .size = 0x10000, .irq_vec = 46,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 4,
			.irq_fcn = ptp_ocp_signal_irq,
			.enable = ptp_ocp_signal_enable,
		},
	},
	{
		OCP_MEM_RESOURCE(pps_to_ext),
		.offset = 0x03030000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(pps_to_clk),
		.offset = 0x03040000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(tod),
		.offset = 0x03050000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(irig_in),
		.offset = 0x03070000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(irig_out),
		.offset = 0x03080000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(dcf_in),
		.offset = 0x03090000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(dcf_out),
		.offset = 0x030A0000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(nmea_out),
		.offset = 0x030B0000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(image),
		.offset = 0x02020000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(ext_ctrl),
		.offset = 0x02100000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(pps_select),
		.offset = 0x02130000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(sma_map1),
		.offset = 0x02140000, .size = 0x1000,
	},
	{
		OCP_MEM_RESOURCE(sma_map2),
		.offset = 0x02220000, .size = 0x1000,
	},
	{
		OCP_I2C_RESOURCE(i2c_ctrl),
		.offset = 0x02150000, .size = 0x10000, .irq_vec = 39,
		.extra = &(struct ptp_ocp_i2c_info) {
			.name = "xiic-i2c",
			.fixed_rate = 50000000,
			.data_size = sizeof(struct xiic_i2c_platform_data),
			.data = &(struct xiic_i2c_platform_data) {
				.num_devices = 3,
				.devices = (struct i2c_board_info[]) {
					{ I2C_BOARD_INFO("24c02", 0x50) },
					{ I2C_BOARD_INFO("24mac402", 0x58),
					  .platform_data = "mac" },
					{ I2C_BOARD_INFO("timecard-led-mux", 0x70) },
				},
			},
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_GNSS]),
		.offset = 0x02160000 + 0x1000, .irq_vec = 35,
		.extra = &(struct ptp_ocp_serial_port) {
			.baud = 115200,
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_GNSS2]),
		.offset = 0x02170000 + 0x1000, .irq_vec = 36,
		.extra = &(struct ptp_ocp_serial_port) {
			.baud = 115200,
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_MAC]),
		.offset = 0x02180000 + 0x1000, .irq_vec = 37,
		.extra = &(struct ptp_ocp_serial_port) {
			.baud = 57600,
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_NMEA]),
		.offset = 0x02190000 + 0x1000, .irq_vec = 42,
	},
	{
		OCP_SPI_RESOURCE(spi_flash),
		.offset = 0x02310000, .size = 0x10000, .irq_vec = 41,
		.extra = &(struct ptp_ocp_flash_info) {
			.name = "xilinx_spi", .pci_offset = 0,
			.data_size = sizeof(struct xspi_platform_data),
			.data = &(struct xspi_platform_data) {
				.num_chipselect = 1,
				.bits_per_word = 8,
				.num_devices = 1,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
				.force_irq = true,
#endif
				.devices = &(struct spi_board_info) {
					.modalias = "spi-nor",
				},
			},
		},
	},
	{
		OCP_MEM_RESOURCE(freq_in[0]),
		.offset = 0x03200000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[1]),
		.offset = 0x03210000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[2]),
		.offset = 0x03220000, .size = 0x10000,
	},
	{
		OCP_MEM_RESOURCE(freq_in[3]),
		.offset = 0x03230000, .size = 0x10000,
	},
	{
		.setup = ptp_ocp_fb_board_init,
	},
	{ }
};

static struct ocp_driver_data ocp_fb_driver_data[] = {
	{
		.ocp_resource_msi = (struct ocp_resource *) (&ocp_fb_resource_rev1),
		.ocp_resource_msix = (struct ocp_resource *) (&ocp_fb_resource_rev2),
	},
	{ }
};

struct ocp_art_osc_reg {
	u32	ctrl;
	u32	value;
	u32	adjust;
	u32	temp;
};

#define MRO50_CTRL_ENABLE		BIT(0)
#define MRO50_CTRL_LOCK			BIT(1)
#define MRO50_CTRL_READ_CMD		BIT(2)
#define MRO50_CTRL_READ_COARSE		BIT(3)
#define MRO50_CTRL_READ_DONE		BIT(4)
#define MRO50_CTRL_ADJUST_CMD		BIT(5)
#define MRO50_CTRL_ADJUST_COARSE	BIT(6)
#define MRO50_CTRL_SAVE_COARSE		BIT(7)

#define MRO50_CMD_READ		(MRO50_CTRL_ENABLE | MRO50_CTRL_READ_CMD)
#define MRO50_CMD_ADJUST	(MRO50_CTRL_ENABLE | MRO50_CTRL_ADJUST_CMD)

#define MRO50_OP_READ_FINE	MRO50_CMD_READ
#define MRO50_OP_READ_COARSE	(MRO50_CMD_READ | MRO50_CTRL_READ_COARSE)
#define MRO50_OP_ADJUST_FINE	MRO50_CMD_ADJUST
#define MRO50_OP_ADJUST_COARSE	(MRO50_CMD_ADJUST | MRO50_CTRL_ADJUST_COARSE)
#define MRO50_OP_SAVE_COARSE	(MRO50_CTRL_ENABLE | MRO50_CTRL_SAVE_COARSE)

#define OCP_ART_CONFIG_SIZE		144
#define OCP_ART_TEMP_TABLE_SIZE	368

/* PTM */
#define PTM_CONTROL_ENABLE  (1 << CSR_PTM_REQUESTER_CONTROL_ENABLE_OFFSET)
#define PTM_CONTROL_TRIGGER (1 << CSR_PTM_REQUESTER_CONTROL_TRIGGER_OFFSET)
#define PTM_STATUS_VALID    (1 << CSR_PTM_REQUESTER_STATUS_VALID_OFFSET)
#define PTM_STATUS_BUSY     (1 << CSR_PTM_REQUESTER_STATUS_BUSY_OFFSET)
/* t1 */
#define PTM_T1_TIME_L       (CSR_PTM_REQUESTER_T1_TIME_ADDR + (4))
#define PTM_T1_TIME_H       (CSR_PTM_REQUESTER_T1_TIME_ADDR + (0))
/* t2 */
#define PTM_MASTER_TIME_L   (CSR_PTM_REQUESTER_MASTER_TIME_ADDR + (4))
#define PTM_MASTER_TIME_H   (CSR_PTM_REQUESTER_MASTER_TIME_ADDR + (0))
/* t4 */
#define PTM_T4_TIME_L       (CSR_PTM_REQUESTER_T4_TIME_ADDR + (4))
#define PTM_T4_TIME_H       (CSR_PTM_REQUESTER_T4_TIME_ADDR + (0))

struct ocp_art_gpio_reg {
	struct {
		u32	gpio;
		u32	__pad[3];
	} map[4];
};

static struct ocp_resource ocp_art_resource[] = {
	{
		OCP_MEM_RESOURCE(reg),
		.offset = 0x01000000, .size = 0x10000,
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_GNSS]),
		.offset = 0x00160000 + 0x1000, .irq_vec = 3,
	},
	{
		OCP_MEM_RESOURCE(art_sma),
		.offset = 0x003C0000, .size = 0x1000,
	},
	/* Timestamp associated with GNSS1 receiver PPS */
	{
		OCP_EXT_RESOURCE(ts0),
		.offset = 0x360000, .size = 0x20, .irq_vec = 12,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 0,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts1),
		.offset = 0x380000, .size = 0x20, .irq_vec = 8,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 1,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts2),
		.offset = 0x390000, .size = 0x20, .irq_vec = 10,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 2,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts3),
		.offset = 0x3A0000, .size = 0x20, .irq_vec = 14,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 3,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_EXT_RESOURCE(ts4),
		.offset = 0x3B0000, .size = 0x20, .irq_vec = 15,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 4,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	/* Timestamp associated with Internal PPS of the card */
	{
		OCP_EXT_RESOURCE(pps),
		.offset = 0x00330000, .size = 0x20, .irq_vec = 11,
		.extra = &(struct ptp_ocp_ext_info) {
			.index = 5,
			.irq_fcn = ptp_ocp_ts_irq,
			.enable = ptp_ocp_ts_enable,
		},
	},
	{
		OCP_MEM_RESOURCE(osc),
		.offset = 0x00340000, .size = 0x20,
	},
	{
		OCP_SPI_RESOURCE(spi_flash),
		.offset = 0x00310000, .size = 0x10000, .irq_vec = 9,
		.extra = &(struct ptp_ocp_flash_info) {
			.name = "spi_altera", .pci_offset = 0,
			.data_size = sizeof(struct altera_spi_platform_data),
			.data = &(struct altera_spi_platform_data) {
				.num_chipselect = 1,
				.num_devices = 1,
				.devices = &(struct spi_board_info) {
					.modalias = "spi-nor",
				},
			},
		},
	},
	{
		OCP_I2C_RESOURCE(i2c_ctrl),
		.offset = 0x350000, .size = 0x100, .irq_vec = 4,
		.extra = &(struct ptp_ocp_i2c_info) {
			.name = "ocores-i2c",
			.fixed_rate = 400000,
			.data_size = sizeof(struct ocores_i2c_platform_data),
			.data = &(struct ocores_i2c_platform_data) {
				.clock_khz = 125000,
				.bus_khz = 400,
				.num_devices = 1,
				.devices = &(struct i2c_board_info) {
					I2C_BOARD_INFO("24c08", 0x50),
				},
			},
		},
	},
	{
		OCP_SERIAL_RESOURCE(port[PORT_MAC]),
		.offset = 0x00190000, .irq_vec = 7,
		.extra = &(struct ptp_ocp_serial_port) {
			.baud = 9600,
		},
	},
	{
		OCP_MEM_RESOURCE(board_config),
		.offset = 0x210000, .size = 0x1000,
	},
	{
		.setup = ptp_ocp_art_board_init,
	},
	{ }
};

static struct ocp_driver_data ocp_art_driver_data[] = {
	{
		.ocp_resource_msi = (struct ocp_resource *) (&ocp_art_resource),
		.ocp_resource_msix = (struct ocp_resource *) (&ocp_art_resource),
	},
	{ }
};

static const struct pci_device_id ptp_ocp_pcidev_id[] = {
	{ PCI_DEVICE_DATA(FACEBOOK, TIMECARD, &ocp_fb_driver_data) },
	{ PCI_DEVICE_DATA(CELESTICA, TIMECARD, &ocp_fb_driver_data) },
	{ PCI_DEVICE_DATA(OROLIA, ARTCARD, &ocp_art_driver_data) },
	{ }
};
MODULE_DEVICE_TABLE(pci, ptp_ocp_pcidev_id);

static DEFINE_MUTEX(ptp_ocp_lock);
static DEFINE_IDR(ptp_ocp_idr);

struct ocp_selector {
	const char *name;
	int value;
};

static const struct ocp_selector ptp_ocp_clock[] = {
	{ .name = "NONE",	.value = 0 },
	{ .name = "TOD",	.value = 1 },
	{ .name = "IRIG",	.value = 2 },
	{ .name = "PPS",	.value = 3 },
	{ .name = "PTP",	.value = 4 },
	{ .name = "RTC",	.value = 5 },
	{ .name = "DCF",	.value = 6 },
	{ .name = "REGS",	.value = 0xfe },
	{ .name = "EXT",	.value = 0xff },
	{ }
};

static const struct ocp_selector ptp_ocp_tod_protocol[] = {
	{ .name = "NMEA",	.value = 0 },
	{ .name = "UBX",	.value = 1 },
	{ .name = "TSIP",	.value = 2 },
	{ .name = "ESIP",	.value = 3 },
	{ .name = "PFEC",	.value = 4 },
	{ }
};

static const struct ocp_selector ptp_ocp_tod_gnss[] = {
	{ .name = "ALL",	.value = 0 },
	{ .name = "COMBINED",	.value = 1 },
	{ .name = "GPS",	.value = 2 },
	{ .name = "GLONASS",	.value = 3 },
	{ .name = "GALILEO",	.value = 4 },
	{ .name = "BEIDOU",	.value = 5 },
	{ }
};

static const struct ocp_selector ptp_ocp_nmea_gnss[] = {
	{ .name = "DEFAULT",     .value = 0 },
	{ .name = "COMBINED",    .value = 1 },
	{ .name = "GPS",         .value = 2 },
	{ .name = "GLONASS",     .value = 3 },
	{ .name = "GALILEO",     .value = 4 },
	{ .name = "BEIDOU",      .value = 5 },
	{ .name = "PROPRIETARY", .value = 15 },
	{ }
};

static const struct ocp_selector ptp_ocp_tod_baud_rates[] = {
	{ .name = "1200",	.value = 0 },
	{ .name = "2400",	.value = 1 },
	{ .name = "4800",	.value = 2 },
	{ .name = "9600",	.value = 3 },
	{ .name = "19200",	.value = 4 },
	{ .name = "38400",	.value = 5 },
	{ .name = "57600",	.value = 6 },
	{ .name = "115200",	.value = 7 },
	{ .name = "230400",	.value = 8 },
	{ .name = "460800",	.value = 9 },
	{ .name = "921600",	.value = 10 },
	{ .name = "1000000",.value = 11 },
	{ .name = "2000000",.value = 12 },
	{ }
};

#define SMA_ENABLE		BIT(15)
#define SMA_SELECT_MASK		((1U << 15) - 1)
#define SMA_DISABLE		0x10000

static const struct ocp_selector ptp_ocp_sma_in[] = {
	{ .name = "10Mhz",	.value = 0x0000 },
	{ .name = "PPS1",	.value = 0x0001 },
	{ .name = "PPS2",	.value = 0x0002 },
	{ .name = "TS1",	.value = 0x0004 },
	{ .name = "TS2",	.value = 0x0008 },
	{ .name = "IRIG",	.value = 0x0010 },
	{ .name = "DCF",	.value = 0x0020 },
	{ .name = "TS3",	.value = 0x0040 },
	{ .name = "TS4",	.value = 0x0080 },
	{ .name = "FREQ1",	.value = 0x0100 },
	{ .name = "FREQ2",	.value = 0x0200 },
	{ .name = "FREQ3",	.value = 0x0400 },
	{ .name = "FREQ4",	.value = 0x0800 },
	{ .name = "None",	.value = SMA_DISABLE },
	{ }
};

static const struct ocp_selector ptp_ocp_sma_out[] = {
	{ .name = "10Mhz",	.value = 0x0000 },
	{ .name = "PHC",	.value = 0x0001 },
	{ .name = "MAC",	.value = 0x0002 },
	{ .name = "GNSS1",	.value = 0x0004 },
	{ .name = "GNSS2",	.value = 0x0008 },
	{ .name = "IRIG",	.value = 0x0010 },
	{ .name = "DCF",	.value = 0x0020 },
	{ .name = "GEN1",	.value = 0x0040 },
	{ .name = "GEN2",	.value = 0x0080 },
	{ .name = "GEN3",	.value = 0x0100 },
	{ .name = "GEN4",	.value = 0x0200 },
	{ .name = "GND",	.value = 0x2000 },
	{ .name = "VCC",	.value = 0x4000 },
	{ }
};

static const struct ocp_selector ptp_ocp_art_sma_in[] = {
	{ .name = "PPS1",	.value = 0x0001 },
	{ .name = "10Mhz",	.value = 0x0008 },
	{ }
};

static const struct ocp_selector ptp_ocp_art_sma_out[] = {
	{ .name = "MAC",	.value = 0x0002 },
	{ .name = "GNSS",	.value = 0x0004 },
	{ .name = "10Mhz",	.value = 0x0010 },
	{ }
};

struct ocp_sma_op {
	const struct ocp_selector *tbl[2];
	void (*init)(struct ptp_ocp *bp);
	u32 (*get)(struct ptp_ocp *bp, int sma_nr);
	int (*set_inputs)(struct ptp_ocp *bp, int sma_nr, u32 val);
	int (*set_output)(struct ptp_ocp *bp, int sma_nr, u32 val);
};

static inline void
ptp_ocp_sma_init(struct ptp_ocp *bp)
{
	return bp->sma_op->init(bp);
}

static inline u32
ptp_ocp_sma_get(struct ptp_ocp *bp, int sma_nr)
{
	return bp->sma_op->get(bp, sma_nr);
}

static inline int
ptp_ocp_sma_set_inputs(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	return bp->sma_op->set_inputs(bp, sma_nr, val);
}

static inline int
ptp_ocp_sma_set_output(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	return bp->sma_op->set_output(bp, sma_nr, val);
}

static const char *
ptp_ocp_select_name_from_val(const struct ocp_selector *tbl, int val)
{
	int i;

	for (i = 0; tbl[i].name; i++)
		if (tbl[i].value == val)
			return tbl[i].name;
	return NULL;
}

static int
ptp_ocp_select_val_from_name(const struct ocp_selector *tbl, const char *name)
{
	const char *select;
	int i;

	for (i = 0; tbl[i].name; i++) {
		select = tbl[i].name;
		if (!strncasecmp(name, select, strlen(select)))
			return tbl[i].value;
	}
	return -EINVAL;
}

static ssize_t
ptp_ocp_select_table_show(const struct ocp_selector *tbl, char *buf)
{
	ssize_t count;
	int i;

	count = 0;
	for (i = 0; tbl[i].name; i++)
		count += sysfs_emit_at(buf, count, "%s ", tbl[i].name);
	if (count)
		count--;
	count += sysfs_emit_at(buf, count, "\n");
	return count;
}

static int
__ptp_ocp_gettime_locked(struct ptp_ocp *bp, struct timespec64 *ts,
			 struct ptp_system_timestamp *sts)
{
	u32 ctrl, time_sec, time_ns;
	int i;

	ptp_read_system_prets(sts);

	ctrl = OCP_CTRL_READ_TIME_REQ | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	for (i = 0; i < 100; i++) {
		ctrl = ioread32(&bp->reg->ctrl);
		if (ctrl & OCP_CTRL_READ_TIME_DONE)
			break;
	}
	ptp_read_system_postts(sts);

	if (sts && bp->ts_window_adjust) {
		s64 ns = timespec64_to_ns(&sts->post_ts);

		sts->post_ts = ns_to_timespec64(ns - bp->ts_window_adjust);
	}

	time_ns = ioread32(&bp->reg->time_ns);
	time_sec = ioread32(&bp->reg->time_sec);

	ts->tv_sec = time_sec;
	ts->tv_nsec = time_ns;

	return ctrl & OCP_CTRL_READ_TIME_DONE ? 0 : -ETIMEDOUT;
}

static int
ptp_ocp_gettimex(struct ptp_clock_info *ptp_info, struct timespec64 *ts,
		 struct ptp_system_timestamp *sts)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	unsigned long flags;
	int err;

	spin_lock_irqsave(&bp->lock, flags);
	err = __ptp_ocp_gettime_locked(bp, ts, sts);
	spin_unlock_irqrestore(&bp->lock, flags);

	return err;
}

static void
__ptp_ocp_settime_locked(struct ptp_ocp *bp, const struct timespec64 *ts)
{
	u32 ctrl, time_sec, time_ns;
	u32 select;

	time_ns = ts->tv_nsec;
	time_sec = ts->tv_sec;

	select = ioread32(&bp->reg->select);
	iowrite32(OCP_SELECT_CLK_REG, &bp->reg->select);

	iowrite32(time_ns, &bp->reg->adjust_ns);
	iowrite32(time_sec, &bp->reg->adjust_sec);

	ctrl = OCP_CTRL_ADJUST_TIME | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	/* restore clock selection */
	iowrite32(select & 0xff, &bp->reg->select);
}

static int
ptp_ocp_settime(struct ptp_clock_info *ptp_info, const struct timespec64 *ts)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	unsigned long flags;

	spin_lock_irqsave(&bp->lock, flags);
	__ptp_ocp_settime_locked(bp, ts);
	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static void
__ptp_ocp_adjtime_locked(struct ptp_ocp *bp, u32 adj_val)
{
	u32 select, ctrl;

	select = ioread32(&bp->reg->select);
	iowrite32(OCP_SELECT_CLK_REG, &bp->reg->select);

	iowrite32(adj_val, &bp->reg->offset_ns);
	iowrite32(NSEC_PER_SEC, &bp->reg->offset_window_ns);

	ctrl = OCP_CTRL_ADJUST_OFFSET | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	/* restore clock selection */
	iowrite32(select & 0xff, &bp->reg->select);
}

static void
ptp_ocp_adjtime_coarse(struct ptp_ocp *bp, u64 delta_ns)
{
	struct timespec64 ts;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&bp->lock, flags);
	err = __ptp_ocp_gettime_locked(bp, &ts, NULL);
	if (likely(!err)) {
		timespec64_add_ns(&ts, delta_ns);
		__ptp_ocp_settime_locked(bp, &ts);
	}
	spin_unlock_irqrestore(&bp->lock, flags);
}

static int
ptp_ocp_adjtime(struct ptp_clock_info *ptp_info, s64 delta_ns)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	unsigned long flags;
	u32 adj_ns, sign;

	if (delta_ns > NSEC_PER_SEC || -delta_ns > NSEC_PER_SEC) {
		ptp_ocp_adjtime_coarse(bp, delta_ns);
		return 0;
	}

	sign = delta_ns < 0 ? BIT(31) : 0;
	adj_ns = sign ? -delta_ns : delta_ns;

	spin_lock_irqsave(&bp->lock, flags);
	__ptp_ocp_adjtime_locked(bp, sign | adj_ns);
	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static void
__ptp_ocp_adjfine_locked(struct ptp_ocp *bp, long scaled_ppm)
{
	u32 ctrl, drift_adj;
	u32 select;
	s32 delta;

	// ppbs
	delta = 1000 * (abs(scaled_ppm) >> 16);

	// if there are fractions
    if ((abs(scaled_ppm) & 0xFFFF) != 0)
    {
        delta += 1000 / (0x10000 / (abs(scaled_ppm) & 0xFFFF)); // fractional ppms rounded to 1ns
    }

    // recover sign
    if (scaled_ppm < 0)
    {
        delta = -1 * delta;
    }

	drift_adj = abs(delta);

	if (delta < 0)
	{
		drift_adj |= 0x80000000;
	}

	select = ioread32(&bp->reg->select);
	iowrite32(OCP_SELECT_CLK_REG, &bp->reg->select);

	iowrite32(OCP_SECOND_IN_NANOSECOND, &bp->reg->drift_window_ns);

	iowrite32(drift_adj, &bp->reg->drift_ns);

	ctrl = OCP_CTRL_ADJUST_DRIFT | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	/* restore clock selection */
	iowrite32(select & 0xff, &bp->reg->select);
}

static int
ptp_ocp_adjfine(struct ptp_clock_info *ptp_info, long scaled_ppm)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	unsigned long flags;

	spin_lock_irqsave(&bp->lock, flags);
	__ptp_ocp_adjfine_locked(bp, scaled_ppm);
	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static int
ptp_ocp_null_adjphase(struct ptp_clock_info *ptp_info, s32 phase_ns)
{
	return -EOPNOTSUPP;
}

#ifdef CONFIG_PCIE_PTM
static int
ptp_ocp_syncdevicetime(ktime_t *device_time,
            struct system_counterval_t *system_counterval,
            void *ctx)
{
	u32 t1_curr_h, t1_curr_l;
	u32 t2_curr_h, t2_curr_l;
	u32 prop_delay;
	u64 ptm_master_time;
	struct ptp_ocp *bp = ctx;
	struct ptm_reg __iomem *reg = bp->ptm;
	u64 t1_curr;
	ktime_t t1, t2_curr;
	int count;

	/* Get a snapshot of system clocks to use as historic value. */
	ktime_get_snapshot(&bp->snapshot);

	/* request */
	iowrite32(PTM_CONTROL_ENABLE | PTM_CONTROL_TRIGGER, &reg->ctrl);

	/* wait until valid */
	count = 100;
	do {
		count--;
		if (((ioread32(&reg->status) & PTM_STATUS_BUSY) == 0) && ((ioread32(&reg->status) & PTM_STATUS_VALID) == 1))
			break;
	} while (count > 0);

	if (count <= 0) {
		printk("Exceeded number of tries for PTM cycle\n");
		return -ETIMEDOUT;
	}

	t1_curr_l = ioread32(&reg->t1_time[1]);
	t1_curr_h = ioread32(&reg->t1_time[0]);
	t1_curr = ((u64)t1_curr_h << 32 | t1_curr_l);
	t1 = ns_to_ktime(t1_curr);

	t2_curr_l = ioread32(&reg->master_time[1]);
	t2_curr_h = ioread32(&reg->master_time[0]);
	t2_curr = ((u64)t2_curr_h << 32 | t2_curr_l);

	/* t3-t2 from downstream port */
	prop_delay = ioread32(&reg->link_delay);
	/* PTM Master Time formula */
	ptm_master_time = t2_curr - (((bp->ptm_t4_prev - bp->ptm_t1_prev) - prop_delay) >> 1);

	*device_time = t1;

#if IS_ENABLED(CONFIG_X86_TSC) && !defined(CONFIG_UML)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
	*system_counterval = (struct system_counterval_t) {
		.cycles = ptm_master_time,
		.cs_id = CSID_X86_ART,
		.use_nsecs = true,
	};
#else
	*system_counterval = convert_art_ns_to_tsc(ptm_master_time);
#endif
#else
	*system_counterval = (struct system_counterval_t) { };
#endif

	/* store T4 & T1 for next request */
	bp->ptm_t4_prev = (((u64) ioread32(&reg->t4_time[0]) << 32) |
		(ioread32(&reg->t4_time[1]) & 0xffffffff));

	bp->ptm_t1_prev = t1_curr;

	return 0;
}

static int
ptp_ocp_getcrosststamp(struct ptp_clock_info *ptp_info,
        	struct system_device_crosststamp *cts)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	if (!bp->pdev->ptm_enabled)
		return 0;

	return get_device_system_crosststamp(ptp_ocp_syncdevicetime,
                         bp, &bp->snapshot, cts);
}
#else
static int
ptp_ocp_getcrosststamp(struct ptp_clock_info *ptp_info,
        	struct system_device_crosststamp *cts)
{
	return 0;
}
#endif

static int
ptp_ocp_enable(struct ptp_clock_info *ptp_info, struct ptp_clock_request *rq,
	       int on)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	struct ptp_ocp_ext_src *ext = NULL;
	u32 req;
	int err;

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		req = OCP_REQ_TIMESTAMP;
		switch (rq->extts.index) {
		case 0:
			ext = bp->ts0;
			break;
		case 1:
			ext = bp->ts1;
			break;
		case 2:
			ext = bp->ts2;
			break;
		case 3:
			ext = bp->ts3;
			break;
		case 4:
			ext = bp->ts4;
			break;
		case 5:
			ext = bp->pps;
			break;
		}
		break;
	case PTP_CLK_REQ_PPS:
		req = OCP_REQ_PPS;
		ext = bp->pps;
		break;
	case PTP_CLK_REQ_PEROUT:
		switch (rq->perout.index) {
		case 0:
			/* This is a request for 1PPS on an output SMA.
			 * Allow, but assume manual configuration.
			 */
			if (on && (rq->perout.period.sec != 1 ||
				   rq->perout.period.nsec != 0))
				return -EINVAL;
			return 0;
		case 1:
		case 2:
		case 3:
		case 4:
			req = rq->perout.index - 1;
			ext = bp->signal_out[req];
			err = ptp_ocp_signal_from_perout(bp, req, &rq->perout);
			if (err)
				return err;
			break;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = -ENXIO;
	if (ext)
		err = ext->info->enable(ext, rq, req, on);

	return err;
}

static int
ptp_ocp_verify(struct ptp_clock_info *ptp_info, unsigned pin,
	       enum ptp_pin_function func, unsigned chan)
{
	struct ptp_ocp *bp = container_of(ptp_info, struct ptp_ocp, ptp_info);
	char buf[16];

	switch (func) {
	case PTP_PF_NONE:
		snprintf(buf, sizeof(buf), "IN: None");
		break;
	case PTP_PF_EXTTS:
		/* Allow timestamps, but require sysfs configuration. */
		return 0;
	case PTP_PF_PEROUT:
		/* channel 0 is 1PPS from PHC.
		 * channels 1..4 are the frequency generators.
		 */
		if (chan)
			snprintf(buf, sizeof(buf), "OUT: GEN%d", chan);
		else
			snprintf(buf, sizeof(buf), "OUT: PHC");
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ptp_ocp_sma_store(bp, buf, pin + 1);
}

static const struct ptp_clock_info ptp_ocp_clock_info = {
	.owner			= THIS_MODULE,
	.name			= KBUILD_MODNAME,
	.max_adj		= 100000000,
	.gettimex64		= ptp_ocp_gettimex,
	.settime64		= ptp_ocp_settime,
	.adjtime		= ptp_ocp_adjtime,
	.adjfine		= ptp_ocp_adjfine,
	.adjphase		= ptp_ocp_null_adjphase,
	.getcrosststamp = ptp_ocp_getcrosststamp,
	.enable			= ptp_ocp_enable,
	.verify			= ptp_ocp_verify,
	.pps			= true,
	.n_ext_ts		= 6,
	.n_per_out		= 5,
};

static void
__ptp_ocp_clear_drift_locked(struct ptp_ocp *bp)
{
	u32 ctrl, select;

	select = ioread32(&bp->reg->select);
	iowrite32(OCP_SELECT_CLK_REG, &bp->reg->select);

	iowrite32(0, &bp->reg->drift_ns);

	ctrl = OCP_CTRL_ADJUST_DRIFT | OCP_CTRL_ENABLE;
	iowrite32(ctrl, &bp->reg->ctrl);

	/* restore clock selection */
	iowrite32(select & 0xff, &bp->reg->select);
}

static void
ptp_ocp_master_correction_write(u32 __iomem *ctrl_reg,
				u32 __iomem *correction_reg,
				u32 correction)
{
	u32 ctrl;

	ctrl = ioread32(ctrl_reg);
	if (ctrl & BIT(0))
		iowrite32(ctrl & ~BIT(0), ctrl_reg);
	iowrite32(correction, correction_reg);
	if (ctrl & BIT(0))
		iowrite32(ctrl, ctrl_reg);
}

static void
ptp_ocp_utc_distribute(struct ptp_ocp *bp, u32 val)
{
	unsigned long flags;
	u32 magnitude, master_correction;

	/*
	 * IRIG and DCF transmit UTC from the card's TAI clock, so their
	 * correction is negative.  The receive cores perform the inverse
	 * conversion and therefore need the same magnitude with a positive
	 * sign.  These cores use sign-magnitude rather than two's complement.
	 */
	magnitude = val & TIMECODE_COR_MAG_MASK;
	if (val != magnitude)
		dev_warn_once(&bp->pdev->dev,
			      "UTC offset %#x exceeds the timecode correction field; truncating\n",
			      val);
	master_correction = TIMECODE_COR_SIGN | magnitude;

	spin_lock_irqsave(&bp->lock, flags);

	bp->utc_tai_offset = magnitude;

	if (bp->irig_out && ptp_ocp_core_version_at_least(
			&bp->irig_out->version, FPGA_CORE_VERSION(1, 0)))
		ptp_ocp_master_correction_write(&bp->irig_out->ctrl,
						&bp->irig_out->adj_sec,
						master_correction);
	if (bp->irig_in && ptp_ocp_core_version_at_least(
			&bp->irig_in->version, FPGA_CORE_VERSION(1, 0)))
		iowrite32(magnitude, &bp->irig_in->adj_sec);
	if (bp->dcf_out && ptp_ocp_core_version_at_least(
			&bp->dcf_out->version, FPGA_CORE_VERSION(1, 0)))
		ptp_ocp_master_correction_write(&bp->dcf_out->ctrl,
						&bp->dcf_out->adj_sec,
						master_correction);
	if (bp->dcf_in && ptp_ocp_core_version_at_least(
			&bp->dcf_in->version, FPGA_CORE_VERSION(1, 0)))
		iowrite32(magnitude, &bp->dcf_in->adj_sec);
	/* The ToD Master is a separate synthesis-optional AXI block.  Generic
	 * UTC distribution can run during initialization/watchdog activity, so
	 * it must never use that path as an implicit capability probe.  Its
	 * correction has a dedicated, explicitly requested sysfs control. */

	spin_unlock_irqrestore(&bp->lock, flags);
}

static void
ptp_ocp_watchdog(struct timer_list *t)
{
	struct ptp_ocp *bp = timer_container_of(bp, t, watchdog);
	unsigned long flags;
	u32 status, utc_offset;

	/* The timer is only installed when the PPS Slave has a status register. */
	status = ioread32(&bp->pps_to_clk->status);

	status &= PPS_STATUS_ERR_MASK;
	if (status) {
		/* Both documented error bits are write-one-to-clear. */
		iowrite32(status, &bp->pps_to_clk->status);
		if (!bp->gnss_lost) {
			spin_lock_irqsave(&bp->lock, flags);
			__ptp_ocp_clear_drift_locked(bp);
			spin_unlock_irqrestore(&bp->lock, flags);
			bp->gnss_lost = ktime_get_real_seconds();
		}

	} else if (bp->gnss_lost) {
		bp->gnss_lost = 0;
	}

	/* if GNSS provides correct data we can rely on
	 * it to get leap second information
	 */
	if (ptp_ocp_tod_optional_utc_enabled(bp)) {
		status = ioread32(&bp->tod->utc_status);
		utc_offset = status & TOD_STATUS_UTC_MASK;
		if (status & TOD_STATUS_UTC_VALID &&
		    utc_offset != bp->utc_tai_offset)
			ptp_ocp_utc_distribute(bp, utc_offset);
	}

	mod_timer(&bp->watchdog, jiffies + HZ);
}

static void
ptp_ocp_estimate_pci_timing(struct ptp_ocp *bp)
{
	ktime_t start, end, delay = S64_MAX;
	u32 ctrl;
	int i;

	for (i = 0; i < 3; i++) {
		ctrl = ioread32(&bp->reg->ctrl);
		ctrl = OCP_CTRL_READ_TIME_REQ | OCP_CTRL_ENABLE;

		iowrite32(ctrl, &bp->reg->ctrl);

		start = ktime_get_raw_ns();

		ctrl = ioread32(&bp->reg->ctrl);

		end = ktime_get_raw_ns();

		delay = min(delay, end - start);
	}
	bp->ts_window_adjust = (delay >> 5) * 3;
}

static int
ptp_ocp_init_clock(struct ptp_ocp *bp)
{
	struct timespec64 ts;
	bool sync;
	u32 version;

	version = ioread32(&bp->reg->version);
	if (!version || version == U32_MAX) {
		dev_err(&bp->pdev->dev, "invalid Clock core version\n");
		return -ENODEV;
	}

	/* Servo coefficients may only be latched while the clock is disabled. */
	iowrite32(0, &bp->reg->ctrl);

	if (ptp_ocp_clock_optional_registers_enabled(bp)) {
		/* NO DRIFT Correction: offset P/I 1/8 and 1/16. */
		iowrite32(0x2000, &bp->reg->servo_offset_p);
		iowrite32(0x1000, &bp->reg->servo_offset_i);
		iowrite32(0, &bp->reg->servo_drift_p);
		iowrite32(0, &bp->reg->servo_drift_i);

		/* Latch servo values while the clock is disabled. */
		iowrite32(OCP_CTRL_ADJUST_SERVO, &bp->reg->ctrl);
	}
	iowrite32(OCP_CTRL_ENABLE, &bp->reg->ctrl);

	if ((ioread32(&bp->reg->ctrl) & OCP_CTRL_ENABLE) == 0) {
		dev_err(&bp->pdev->dev, "clock not enabled\n");
		return -ENODEV;
	}

	ptp_ocp_estimate_pci_timing(bp);

	sync = ptp_ocp_clock_has_status(bp) &&
		(ioread32(&bp->reg->status) & OCP_STATUS_IN_SYNC);
	if (!sync) {
		ktime_get_clocktai_ts64(&ts);
		ptp_ocp_settime(&bp->ptp_info, &ts);
	}

	/* If there is a clock supervisor, then enable the watchdog */
	if (bp->pps_to_clk &&
	    ptp_ocp_core_version_at_least(&bp->pps_to_clk->version,
					  PPS_SLAVE_VERSION_STATUS)) {
		timer_setup(&bp->watchdog, ptp_ocp_watchdog, 0);
		mod_timer(&bp->watchdog, jiffies + HZ);
	}

	return 0;
}

static void
ptp_ocp_tod_init(struct ptp_ocp *bp)
{
	u32 ctrl, reg, version;

	/* Preserve the reset/image-selected protocol and message gates. */
	version = ioread32(&bp->tod->version);
	if (!version || version == U32_MAX)
		return;
	ctrl = ioread32(&bp->tod->ctrl);
	if (!(ctrl & TOD_CTRL_ENABLE))
		iowrite32(ctrl | TOD_CTRL_ENABLE, &bp->tod->ctrl);

	if (ptp_ocp_tod_optional_utc_enabled(bp)) {
		reg = ioread32(&bp->tod->utc_status);
		if (reg & TOD_STATUS_UTC_VALID)
			ptp_ocp_utc_distribute(bp, reg & TOD_STATUS_UTC_MASK);
	}
}

static const char *
ptp_ocp_tod_proto_name(int idx)
{
	static const char * const proto_name[] = {
		"NMEA",
		"UBX",
		"TSIP",
		"ESIP",
		"PFEC",
		"unknown"
	};
	if (idx >= ARRAY_SIZE(proto_name))
		idx = ARRAY_SIZE(proto_name) - 1;
	return proto_name[idx];
}

static const char *
ptp_ocp_tod_gnss_name(int idx)
{
	static const char * const gnss_name[] = {
		"ALL", "COMBINED", "GPS", "GLONASS", "GALILEO", "BEIDOU",
		"Unknown"
	};
	if (idx >= ARRAY_SIZE(gnss_name))
		idx = ARRAY_SIZE(gnss_name) - 1;
	return gnss_name[idx];
}

static const char *
ptp_ocp_tod_gnss_ant_status(int idx)
{
	static const char * const ant_status[] = {
		"INIT", "DONTKNOW", "OK", "SHORT", "OPEN",
		"Unknown"
	};
	if (idx >= ARRAY_SIZE(ant_status))
		idx = ARRAY_SIZE(ant_status) - 1;
	return ant_status[idx];
}

static const char *
ptp_ocp_tod_gnss_jam_state(int idx)
{
	static const char * const jam_state[] = {
		"Unknown or feature disabled", "OK",
		"Warning", "Critical"
	};
	if (idx >= ARRAY_SIZE(jam_state))
		idx = 0;
	return jam_state[idx];
}

static const char *
ptp_ocp_tod_gnss_fix(int idx)
{
	static const char * const fix_name[] = {
		"No fix", "Dead reckning only", "2-D fix",
		"3-D fix", "GPS+dead reckoning combined", "Unknown"
	};
	if (idx >= ARRAY_SIZE(fix_name))
		idx = ARRAY_SIZE(fix_name) - 1;
	return fix_name[idx];
}

static const char *
ptp_ocp_tod_gnss_spoof_state(int idx)
{
	static const char * const spoof_state[] = {
		"Unknown or deactivated", "No spoofing indicated",
		"Spoofing indicated"
	};
	if (idx >= ARRAY_SIZE(spoof_state))
		idx = 0;
	return spoof_state[idx];
}

static const char *
ptp_ocp_tty_port_name(int idx)
{
	static const char * const tty_name[] = {
		"GNSS", "GNSS2", "MAC", "NMEA"
	};
	return tty_name[idx];
}

struct ptp_ocp_nvmem_match_info {
	struct ptp_ocp *bp;
	const void * const tag;
};

static int
ptp_ocp_nvmem_match(struct device *dev, const void *data)
{
	const struct ptp_ocp_nvmem_match_info *info = data;

	dev = dev->parent;
	if (!i2c_verify_client(dev) || info->tag != dev->platform_data)
		return 0;

	while ((dev = dev->parent))
		if (dev->driver && !strcmp(dev->driver->name, KBUILD_MODNAME))
			return info->bp == dev_get_drvdata(dev);
	return 0;
}

static inline struct nvmem_device *
ptp_ocp_nvmem_device_get(struct ptp_ocp *bp, const void * const tag)
{
	struct ptp_ocp_nvmem_match_info info = { .bp = bp, .tag = tag };

	return nvmem_device_find(&info, ptp_ocp_nvmem_match);
}

static inline void
ptp_ocp_nvmem_device_put(struct nvmem_device **nvmemp)
{
	if (!IS_ERR_OR_NULL(*nvmemp))
		nvmem_device_put(*nvmemp);
	*nvmemp = NULL;
}

static void
ptp_ocp_read_eeprom(struct ptp_ocp *bp)
{
	const struct ptp_ocp_eeprom_map *map;
	struct nvmem_device *nvmem;
	const void *tag;
	int ret;

	if (!bp->i2c_ctrl)
		return;

	tag = NULL;
	nvmem = NULL;

	for (map = bp->eeprom_map; map->len; map++) {
		if (map->tag != tag) {
			tag = map->tag;
			ptp_ocp_nvmem_device_put(&nvmem);
		}
		if (!nvmem) {
			nvmem = ptp_ocp_nvmem_device_get(bp, tag);
			if (IS_ERR(nvmem)) {
				ret = PTR_ERR(nvmem);
				goto fail;
			}
		}
		ret = nvmem_device_read(nvmem, map->off, map->len,
					BP_MAP_ENTRY_ADDR(bp, map));
		if (ret != map->len)
			goto fail;
	}

	bp->has_eeprom_data = true;

out:
	ptp_ocp_nvmem_device_put(&nvmem);
	return;

fail:
	dev_err(&bp->pdev->dev, "could not read eeprom: %d\n", ret);
	goto out;
}

static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(10, 3))
ptp_ocp_firstchild(struct device *dev, const void *data)
#else
ptp_ocp_firstchild(struct device *dev, void *data)
#endif
{
	return 1;
}

static struct device *
ptp_ocp_find_flash(struct ptp_ocp *bp)
{
	struct device *dev, *last;

	last = NULL;
	dev = &bp->spi_flash->dev;

	while ((dev = device_find_child(dev, NULL, ptp_ocp_firstchild))) {
		if (!strcmp("mtd", dev_bus_name(dev)))
			break;
		put_device(last);
		last = dev;
	}
	put_device(last);

	return dev;
}

static int
ptp_ocp_devlink_flash_check(struct devlink *devlink, const struct firmware *fw,
						size_t *image_size)
{
	struct ptp_ocp *bp = devlink_priv(devlink);
	const struct ptp_ocp_firmware_header *hdr;
	struct pci_dev *pdev = bp->pdev;
	u16 crc;

	devlink_flash_update_status_notify(devlink, "Checking flash image",
					   NULL, 0, 0);

	hdr = (const struct ptp_ocp_firmware_header *)fw->data;
	if (memcmp(hdr->magic, OCP_FIRMWARE_MAGIC_HEADER, 4)) {
		dev_err(&pdev->dev, "No image header found, fallback to raw data flashing\n");
		*image_size = fw->size;
		return 0;
	}

	// here we have header, have to check everything to confirm that image is correct
	if (be16_to_cpu(hdr->pci_vendor_id) != pdev->vendor ||
		be16_to_cpu(hdr->pci_device_id) != pdev->device) {
		dev_err(&pdev->dev, "The image is for different hardware\n");
		return -EINVAL;
	}

	/* Disable it while we don't have resources to read revision from */
#if 0
	if (__be16_to_cpu(hdr->hw_revision) != bp->vendor)
		dev_err(&pdev->dev, "The image is for different hardware revision\n");
		return -EINVAL;
	}
#endif
	*image_size = be32_to_cpu(hdr->image_size);
	if (*image_size != (fw->size - sizeof(*hdr))) {
		dev_err(&pdev->dev, "The image size is not correct\n");
		return -EINVAL;
	}

	crc = crc16(0xFFFF, &fw->data[sizeof(*hdr)], *image_size);
	if (be16_to_cpu(hdr->crc) != crc) {
		dev_err(&pdev->dev, "The image CRC is not correct\n");
		return -EINVAL;
	}

	return sizeof(*hdr);
}

static int
ptp_ocp_devlink_flash(struct devlink *devlink, struct device *dev,
		      const struct firmware *fw)
{
	struct mtd_info *mtd = dev_get_drvdata(dev);
	struct ptp_ocp *bp = devlink_priv(devlink);
	size_t off, len, resid, wrote;
	struct erase_info erase;
	size_t base, blksz;
	int err = 0, image_off;

	image_off = ptp_ocp_devlink_flash_check(devlink, fw, &resid);
	if (image_off < 0) {
		err = image_off;
		goto out;
	}

	off = 0;
	base = bp->flash_start;
	blksz = 4096;

	while (resid) {
		devlink_flash_update_status_notify(devlink, "Flashing",
						   NULL, off, fw->size);

		len = min_t(size_t, resid, blksz);
		erase.addr = base + off;
		erase.len = blksz;

		err = mtd_erase(mtd, &erase);
		if (err)
			goto out;

		err = mtd_write(mtd, base + off, len, &wrote, &fw->data[image_off + off]);
		if (err)
			goto out;

		off += blksz;
		resid -= len;
	}
out:
	return err;
}

static int
ptp_ocp_devlink_flash_update(struct devlink *devlink,
			     struct devlink_flash_update_params *params,
			     struct netlink_ext_ack *extack)
{
	struct ptp_ocp *bp = devlink_priv(devlink);
	struct device *dev;
	const char *msg;
	int err;

	dev = ptp_ocp_find_flash(bp);
	if (!dev) {
		dev_err(&bp->pdev->dev, "Can't find Flash SPI adapter\n");
		return -ENODEV;
	}

	devlink_flash_update_status_notify(devlink, "Preparing to flash",
					   NULL, 0, 0);

	err = ptp_ocp_devlink_flash(devlink, dev, params->fw);

	msg = err ? "Flash error" : "Flash complete";
	devlink_flash_update_status_notify(devlink, msg, NULL, 0, 0);

	put_device(dev);
	return err;
}

static int
ptp_ocp_devlink_info_get(struct devlink *devlink, struct devlink_info_req *req,
			 struct netlink_ext_ack *extack)
{
	struct ptp_ocp *bp = devlink_priv(devlink);
	const char *fw_image;
	char buf[32];
	int err;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 2, 0)
	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;
#endif

	fw_image = bp->fw_loader ? "loader" : "fw";
	sprintf(buf, "%d.%d", bp->fw_tag, bp->fw_version);
	err = devlink_info_version_running_put(req, fw_image, buf);
	if (err)
		return err;

	if (!bp->has_eeprom_data) {
		ptp_ocp_read_eeprom(bp);
		if (!bp->has_eeprom_data)
			return 0;
	}

	sprintf(buf, "%pM", bp->serial);
	err = devlink_info_serial_number_put(req, buf);
	if (err)
		return err;

	err = devlink_info_version_fixed_put(req,
			DEVLINK_INFO_VERSION_GENERIC_BOARD_ID,
			bp->board_id);
	if (err)
		return err;

	return 0;
}

static const struct devlink_ops ptp_ocp_devlink_ops = {
	.flash_update = ptp_ocp_devlink_flash_update,
	.info_get = ptp_ocp_devlink_info_get,
};

static void __iomem *
__ptp_ocp_get_mem(struct ptp_ocp *bp, resource_size_t start, int size)
{
	struct resource res = DEFINE_RES_MEM_NAMED(start, size, "ptp_ocp");

	return devm_ioremap_resource(&bp->pdev->dev, &res);
}

static void __iomem *
ptp_ocp_get_mem(struct ptp_ocp *bp, struct ocp_resource *r)
{
	resource_size_t start;

	start = pci_resource_start(bp->pdev, 0) + r->offset;
	return __ptp_ocp_get_mem(bp, start, r->size);
}

static void
ptp_ocp_set_irq_resource(struct resource *res, int irq)
{
	struct resource r = DEFINE_RES_IRQ(irq);
	*res = r;
}

static void
ptp_ocp_set_mem_resource(struct resource *res, resource_size_t start, int size)
{
	struct resource r = DEFINE_RES_MEM(start, size);
	*res = r;
}

static int
ptp_ocp_register_spi(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct ptp_ocp_flash_info *info;
	struct pci_dev *pdev = bp->pdev;
	struct platform_device *p;
	struct resource res[2];
	resource_size_t start;
	int id;

	start = pci_resource_start(pdev, 0) + r->offset;
	ptp_ocp_set_mem_resource(&res[0], start, r->size);
	ptp_ocp_set_irq_resource(&res[1], pci_irq_vector(pdev, r->irq_vec));

	info = r->extra;
	id = pci_dev_id(pdev) << 1;
	id += info->pci_offset;

	p = platform_device_register_resndata(&pdev->dev, info->name, id,
					      res, 2, info->data,
					      info->data_size);
	if (IS_ERR(p))
		return PTR_ERR(p);

	bp_assign_entry(bp, r, p);

	return 0;
}

static struct platform_device *
ptp_ocp_i2c_bus(struct pci_dev *pdev, struct ocp_resource *r, int id)
{
	struct ptp_ocp_i2c_info *info;
	struct resource res[2];
	resource_size_t start;

	info = r->extra;
	start = pci_resource_start(pdev, 0) + r->offset;
	ptp_ocp_set_mem_resource(&res[0], start, r->size);
	ptp_ocp_set_irq_resource(&res[1], pci_irq_vector(pdev, r->irq_vec));

	return platform_device_register_resndata(&pdev->dev, info->name,
						 id, res, 2,
						 info->data, info->data_size);
}

static int
ptp_ocp_register_i2c(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct pci_dev *pdev = bp->pdev;
	struct ptp_ocp_i2c_info *info;
	struct platform_device *p;
	struct clk_hw *clk;
	char buf[32];
	int id;

	info = r->extra;
	id = pci_dev_id(bp->pdev) << 2;
	id += bp->i2c_count;

	if (!bp->i2c_clk) {
		sprintf(buf, "AXI.%d", id);
		clk = clk_hw_register_fixed_rate(&pdev->dev, buf, NULL, 0,
						 info->fixed_rate);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		bp->i2c_clk = clk;
	}

	sprintf(buf, "%s.%d", info->name, id);
	devm_clk_hw_register_clkdev(&pdev->dev, bp->i2c_clk, NULL, buf);

	p = ptp_ocp_i2c_bus(bp->pdev, r, id);
	if (IS_ERR(p))
		return PTR_ERR(p);

	bp_assign_entry(bp, r, p);

	return 0;
}

/* The expectation is that this is triggered only on error. */
static irqreturn_t
ptp_ocp_signal_irq(int irq, void *priv)
{
	struct ptp_ocp_ext_src *ext = priv;
	struct signal_reg __iomem *reg = ext->mem;
	struct ptp_ocp *bp = ext->bp;
	u32 enable, status;
	int gen;

	gen = ext->info->index - 1;
	if (!ptp_ocp_core_version_at_least(&reg->version,
					   SIGNAL_VERSION_CURRENT_MAP)) {
		/* Only Enable and Version have stable offsets before version 1.3. */
		iowrite32(0, &reg->enable);
		bp->signal[gen].running = false;
		return IRQ_HANDLED;
	}

	enable = ioread32(&reg->enable);
	status = ioread32(&reg->status);

	/* disable generator on error */
	if (status || !enable) {
		iowrite32(0, &reg->intr_mask);
		iowrite32(0, &reg->enable);
		bp->signal[gen].running = false;
	}

	iowrite32(1, &reg->intr);	/* write one to acknowledge */

	return IRQ_HANDLED;
}

static int
ptp_ocp_signal_set(struct ptp_ocp *bp, int gen, struct ptp_ocp_signal *s)
{
	struct signal_reg __iomem *reg;
	struct ptp_system_timestamp sts;
	struct timespec64 ts;
	ktime_t start_ns;
	int err;

	if (!bp->signal_out[gen])
		return -EOPNOTSUPP;
	reg = bp->signal_out[gen]->mem;
	if (!ptp_ocp_core_version_at_least(&reg->version,
					   SIGNAL_VERSION_CURRENT_MAP))
		return -EOPNOTSUPP;

	if (!s->period)
		return 0;

	if (!s->pulse)
		s->pulse = ktime_divns(s->period * s->duty, 100);

	err = ptp_ocp_gettimex(&bp->ptp_info, &ts, &sts);
	if (err)
		return err;

	start_ns = ktime_set(ts.tv_sec, ts.tv_nsec) + NSEC_PER_MSEC;
	if (!s->start) {
		/* roundup() does not work on 32-bit systems */
		s->start = DIV_ROUND_UP_ULL(start_ns, s->period) * s->period;
		s->start = ktime_add(s->start, s->phase);
	}

	if (s->duty < 1 || s->duty > 99)
		return -EINVAL;

	if (s->pulse < 1 || s->pulse > s->period)
		return -EINVAL;

	if (s->start < start_ns)
		return -EINVAL;

	bp->signal[gen] = *s;

	return 0;
}

static int
ptp_ocp_signal_from_perout(struct ptp_ocp *bp, int gen,
			   struct ptp_perout_request *req)
{
	struct ptp_ocp_signal s = { };

	s.polarity = bp->signal[gen].polarity;
	s.repeat_count = bp->signal[gen].repeat_count;
	s.cable_delay = bp->signal[gen].cable_delay;
	s.period = ktime_set(req->period.sec, req->period.nsec);
	if (!s.period)
		return 0;

	if (req->flags & PTP_PEROUT_DUTY_CYCLE) {
		s.pulse = ktime_set(req->on.sec, req->on.nsec);
		s.duty = ktime_divns(s.pulse * 100, s.period);
	}

	if (req->flags & PTP_PEROUT_PHASE)
		s.phase = ktime_set(req->phase.sec, req->phase.nsec);
	else
		s.start = ktime_set(req->start.sec, req->start.nsec);

	return ptp_ocp_signal_set(bp, gen, &s);
}

static int
ptp_ocp_signal_enable(void *priv, struct ptp_clock_request *rq,
		      u32 req, bool enable)
{
	struct ptp_ocp_ext_src *ext = priv;
	struct signal_reg __iomem *reg = ext->mem;
	struct ptp_ocp *bp = ext->bp;
	struct timespec64 ts;
	int gen;

	gen = ext->info->index - 1;
	if (!ptp_ocp_core_version_at_least(&reg->version,
					   SIGNAL_VERSION_CURRENT_MAP)) {
		/* The interrupt and timing fields moved in version 1.3. */
		iowrite32(0, &reg->enable);
		bp->signal[gen].running = false;
		return enable ? -EOPNOTSUPP : 0;
	}

	iowrite32(0, &reg->intr_mask);
	iowrite32(0, &reg->enable);
	bp->signal[gen].running = false;
	if (!enable)
		return 0;

	ts = ktime_to_timespec64(bp->signal[gen].start);
	iowrite32(ts.tv_sec, &reg->start_sec);
	iowrite32(ts.tv_nsec, &reg->start_ns);

	ts = ktime_to_timespec64(bp->signal[gen].period);
	iowrite32(ts.tv_sec, &reg->period_sec);
	iowrite32(ts.tv_nsec, &reg->period_ns);

	ts = ktime_to_timespec64(bp->signal[gen].pulse);
	iowrite32(ts.tv_sec, &reg->pulse_sec);
	iowrite32(ts.tv_nsec, &reg->pulse_ns);

	iowrite32(bp->signal[gen].polarity, &reg->polarity);
	if (ptp_ocp_core_version_at_least(&reg->version,
					       SIGNAL_VERSION_CABLE_DELAY))
		iowrite32(bp->signal[gen].cable_delay, &reg->cable_delay);
	if (ptp_ocp_core_version_at_least(&reg->version,
					       SIGNAL_VERSION_CURRENT_MAP))
		iowrite32(bp->signal[gen].repeat_count, &reg->repeat_count);

	iowrite32(BIT(0) | BIT(1), &reg->status); /* clear sticky errors */
	iowrite32(1, &reg->intr);		/* clear interrupt state */
	iowrite32(1, &reg->intr_mask);		/* enable interrupt */
	iowrite32(3, &reg->enable);		/* valid & enable */

	bp->signal[gen].running = true;

	return 0;
}

static irqreturn_t
ptp_ocp_ts_irq(int irq, void *priv)
{
	struct ptp_ocp_ext_src *ext = priv;
	struct ts_reg __iomem *reg = ext->mem;
	struct ptp_clock_event ev;
	u32 sec, nsec;

	if (ext == ext->bp->pps) {
		if (ext->bp->pps_req_map & OCP_REQ_PPS) {
			ev.type = PTP_CLOCK_PPS;
			ptp_clock_event(ext->bp->ptp, &ev);
		}

		if ((ext->bp->pps_req_map & ~OCP_REQ_PPS) == 0)
			goto out;
	}

	/* XXX should fix API - this converts s/ns -> ts -> s/ns */
	sec = ioread32(&reg->time_sec);
	nsec = ioread32(&reg->time_ns);

	ev.type = PTP_CLOCK_EXTTS;
	ev.index = ext->info->index;
	ev.timestamp = sec * NSEC_PER_SEC + nsec;

	ptp_clock_event(ext->bp->ptp, &ev);

out:
	iowrite32(1, &reg->intr);	/* write 1 to ack */

	return IRQ_HANDLED;
}

static int
ptp_ocp_ts_enable(void *priv, struct ptp_clock_request *rq,
		  u32 req, bool enable)
{
	struct ptp_ocp_ext_src *ext = priv;
	struct ts_reg __iomem *reg = ext->mem;
	struct ptp_ocp *bp = ext->bp;
	u32 val;

	if (ext == bp->pps) {
		u32 old_map = bp->pps_req_map;

		if (enable)
			bp->pps_req_map |= req;
		else
			bp->pps_req_map &= ~req;

		/* if no state change, just return */
		if ((!!old_map ^ !!bp->pps_req_map) == 0)
			return 0;
	}

	if (enable) {
		val = rq->extts.flags & PTP_FALLING_EDGE ? 1 : 0;
		iowrite32(val, &reg->polarity);
		iowrite32(1, &reg->enable);
		iowrite32(1, &reg->intr_mask);
		iowrite32(1, &reg->intr);
	} else {
		iowrite32(0, &reg->intr_mask);
		iowrite32(0, &reg->enable);
	}

	return 0;
}

static void
ptp_ocp_unregister_ext(struct ptp_ocp_ext_src *ext)
{
	ext->info->enable(ext, NULL, ~0, false);
	pci_free_irq(ext->bp->pdev, ext->irq_vec, ext);
	kfree(ext);
}

static int
ptp_ocp_register_ext(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct pci_dev *pdev = bp->pdev;
	struct ptp_ocp_ext_src *ext;
	int err;

	ext = kzalloc(sizeof(*ext), GFP_KERNEL);
	if (!ext)
		return -ENOMEM;

	ext->mem = ptp_ocp_get_mem(bp, r);
	if (IS_ERR(ext->mem)) {
		err = PTR_ERR(ext->mem);
		goto out;
	}

	ext->bp = bp;
	ext->info = r->extra;
	ext->irq_vec = r->irq_vec;

	err = pci_request_irq(pdev, r->irq_vec, ext->info->irq_fcn, NULL,
			      ext, "ocp%d.%s", bp->id, r->name);
	if (err) {
		dev_err(&pdev->dev, "Could not get irq %d\n", r->irq_vec);
		goto out;
	}

	bp_assign_entry(bp, r, ext);

	return 0;

out:
	kfree(ext);
	return err;
}

static int
ptp_ocp_serial_line(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct pci_dev *pdev = bp->pdev;
	struct uart_8250_port uart;

	/* Setting UPF_IOREMAP and leaving port.membase unspecified lets
	 * the serial port device claim and release the pci resource.
	 */
	memset(&uart, 0, sizeof(uart));
	uart.port.dev = &pdev->dev;
	uart.port.iotype = UPIO_MEM;
	uart.port.regshift = 2;
	uart.port.mapbase = pci_resource_start(pdev, 0) + r->offset;
	uart.port.irq = pci_irq_vector(pdev, r->irq_vec);
	uart.port.uartclk = 50000000;
	uart.port.flags = UPF_FIXED_TYPE | UPF_IOREMAP | UPF_NO_THRE_TEST;
	uart.port.type = PORT_16550A;

	return serial8250_register_8250_port(&uart);
}

static int
ptp_ocp_register_serial(struct ptp_ocp *bp, struct ocp_resource *r)
{
	struct ptp_ocp_serial_port *p = (struct ptp_ocp_serial_port *)r->extra;
	struct ptp_ocp_serial_port port = {};

	port.line = ptp_ocp_serial_line(bp, r);
	if (port.line < 0)
		return port.line;

	if (p) {
		port.baud = p->baud;
	}
	bp_assign_entry(bp, r, port);

	return 0;
}

static int
ptp_ocp_register_mem(struct ptp_ocp *bp, struct ocp_resource *r)
{
	void __iomem *mem;

	mem = ptp_ocp_get_mem(bp, r);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	bp_assign_entry(bp, r, mem);

	return 0;
}

static void
_ptp_ocp_signal_init(struct ptp_ocp_signal *s, struct signal_reg __iomem *reg)
{
	u32 val, version;

	version = ioread32(&reg->version);
	iowrite32(0, &reg->enable);		/* disable */
	s->duty = 50;
	if (!version || version == U32_MAX ||
	    version < SIGNAL_VERSION_CURRENT_MAP)
		return;

	val = ioread32(&reg->polarity);
	s->polarity = val ? true : false;
	if (version >= SIGNAL_VERSION_CABLE_DELAY)
		s->cable_delay = ioread32(&reg->cable_delay) &
				 SIGNAL_CABLE_DELAY_MAX;
	if (version >= SIGNAL_VERSION_CURRENT_MAP)
		s->repeat_count = ioread32(&reg->repeat_count);
}

static void
ptp_ocp_signal_init(struct ptp_ocp *bp)
{
	int i;

	for (i = 0; i < 4; i++)
		if (bp->signal_out[i])
			_ptp_ocp_signal_init(&bp->signal[i],
					     bp->signal_out[i]->mem);
}

static int
ptp_ocp_fb_set_pins(struct ptp_ocp *bp)
{
	struct ptp_pin_desc *config;
	int i;

	config = kzalloc(sizeof(*config) * 4, GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	for (i = 0; i < 4; i++) {
		sprintf(config[i].name, "sma%d", i + 1);
		config[i].index = i;
	}

	bp->ptp_info.n_pins = 4;
	bp->ptp_info.pin_config = config;

	return 0;
}

static void
ptp_ocp_fb_set_version(struct ptp_ocp *bp)
{
	u64 cap = OCP_CAP_BASIC;
	u32 version;

	version = ioread32(&bp->image->version);

	/* if lower 16 bits are empty, this is the fw loader. */
	if ((version & 0xffff) == 0) {
		version = version >> 16;
		bp->fw_loader = true;
	}

	bp->fw_tag = version >> 15;
	bp->fw_version = version & 0x7fff;

	if (bp->fw_tag) {
		/* FPGA firmware */
		if (version >= 5)
			cap |= OCP_CAP_SIGNAL | OCP_CAP_FREQ;
	} else {
		/* SOM firmware */
		if (version >= 19)
			cap |= OCP_CAP_SIGNAL;
		if (version >= 20)
			cap |= OCP_CAP_FREQ;
	}

	bp->fw_cap = cap;
}

/* FB specific board initializers; last "resource" registered. */
static int
ptp_ocp_fb_board_init(struct ptp_ocp *bp, struct ocp_resource *r)
{
	int err;

	bp->flash_start = 1024 * 4096;
	bp->config_start = bp->flash_start - OCP_CONFIG_SIZE;
	bp->eeprom_map = fb_eeprom_map;
	bp->fw_version = ioread32(&bp->image->version);
	bp->attr_tbl = fb_timecard_groups;
	bp->sma_op = &ocp_fb_sma_op;

	ptp_ocp_fb_set_version(bp);

	ptp_ocp_tod_init(bp);
	/* Do not probe or rewrite the synthesis-optional ToD Master at boot.
	 * Its typed sysfs controls validate Version when the operator elects
	 * to use the NMEA generator.  This also preserves firmware settings. */
	ptp_ocp_sma_init(bp);
	ptp_ocp_signal_init(bp);

	err = ptp_ocp_fb_set_pins(bp);
	if (err)
		return err;

	return ptp_ocp_init_clock(bp);
}

static int
__ptp_ocp_mro50_wait_cmd(struct ocp_art_osc_reg __iomem *reg, u32 done)
{
	u32 ctrl;
	int i;

	for (i = 0; i < 100; i++) {
		ctrl = ioread32(&reg->ctrl);
		if (ctrl & done)
			break;
		usleep_range(100, 1000);
	}
	return ctrl & done ? 0 : -ETIMEDOUT;
}

static int
__ptp_ocp_mro50_write_locked(struct ocp_art_osc_reg __iomem *reg,
			     u32 ctrl, u32 val)
{
	iowrite32(val, &reg->adjust);
	iowrite32(ctrl, &reg->ctrl);

	return __ptp_ocp_mro50_wait_cmd(reg, MRO50_CTRL_ADJUST_CMD);
}

static int
__ptp_ocp_mro50_read_locked(struct ocp_art_osc_reg __iomem *reg,
			    u32 ctrl, u32 *val)
{
	int err;

	iowrite32(ctrl, &reg->ctrl);
	err = __ptp_ocp_mro50_wait_cmd(reg, MRO50_CTRL_READ_DONE);
	if (!err)
		*val = ioread32(&reg->value);

	return err;
}

static int
ptp_ocp_mro50_read(struct ptp_ocp *bp, u32 ctrl, u32 *val)
{
	int err;

	mutex_lock(&bp->mutex);
	err = __ptp_ocp_mro50_read_locked(bp->osc, ctrl, val);
	mutex_unlock(&bp->mutex);

	return err;
}

static int
ptp_ocp_mro50_write(struct ptp_ocp *bp, u32 ctrl, u32 val)
{
	int err;

	mutex_lock(&bp->mutex);
	err = __ptp_ocp_mro50_write_locked(bp->osc, ctrl, val);
	mutex_unlock(&bp->mutex);

	return err;
}

static long
ptp_ocp_mro50_ioctl_blob(struct ptp_ocp *bp, unsigned cmd, unsigned long arg, size_t sz)
{
	struct nvmem_device *nvmem;
	u8 buf[512];
	int err = 0;

	nvmem = ptp_ocp_nvmem_device_get(bp, NULL);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	switch (cmd) {
	case MRO50_READ_EEPROM_BLOB:
	case MRO50_READ_EXTENDED_EEPROM_BLOB:
		err = nvmem_device_read(nvmem, 0x0, sz, buf);
		if (err != sz) {
			err = -EFAULT;
			goto out;
		}
		err = 0;
		if (copy_to_user((u8 __user *)arg, buf, sz))
			err = -EFAULT;
		break;
	case MRO50_WRITE_EEPROM_BLOB:
	case MRO50_WRITE_EXTENDED_EEPROM_BLOB:
		err = copy_from_user(buf, (void __user *)arg, sz);
		if (err) {
			err = -EFAULT;
			goto out;
		}
		err = nvmem_device_write(nvmem, 0x0, sz, buf);
		if (err != sz)
			err = -EFAULT;
		else
		  err = 0;
		break;
	default:
		err = -ENOTTY;
	}

out:
	ptp_ocp_nvmem_device_put(&nvmem);

	return err;
}

static long
ptp_ocp_mro50_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct miscdevice *mro50;
	struct ptp_ocp *bp;
	u32 val;
	int err;

	mro50 = file->private_data;
	bp = container_of(mro50, struct ptp_ocp, mro50);

	switch (cmd) {
	case MRO50_BOARD_CONFIG_READ:
		val = ioread32(&bp->board_config->mro50_serial_activate);
		err = 0;
		break;
	case MRO50_BOARD_CONFIG_WRITE:
		if (get_user(val, (u32 __user *)arg))
			return -EFAULT;
		iowrite32(val, &bp->board_config->mro50_serial_activate);
		return 0;
	case MRO50_READ_FINE:
		err = ptp_ocp_mro50_read(bp, MRO50_OP_READ_FINE, &val);
		break;
	case MRO50_READ_COARSE:
		err = ptp_ocp_mro50_read(bp, MRO50_OP_READ_COARSE, &val);
		break;
	case MRO50_READ_TEMP:
		val = ioread32(&bp->osc->temp);
		err = 0;
		break;
	case MRO50_READ_CTRL:
		val = ioread32(&bp->osc->ctrl);
		err = 0;
		break;
	case MRO50_ADJUST_FINE:
		if (get_user(val, (u32 __user *)arg))
			return -EFAULT;
		return ptp_ocp_mro50_write(bp, MRO50_OP_ADJUST_FINE, val);
	case MRO50_ADJUST_COARSE:
		if (get_user(val, (u32 __user *)arg))
			return -EFAULT;
		return ptp_ocp_mro50_write(bp, MRO50_OP_ADJUST_COARSE, val);
	case MRO50_SAVE_COARSE:
		mutex_lock(&bp->mutex);
		iowrite32(MRO50_OP_SAVE_COARSE, &bp->osc->ctrl);
		mutex_unlock(&bp->mutex);
		return 0;
	case MRO50_READ_EEPROM_BLOB:
	case MRO50_WRITE_EEPROM_BLOB:
		return ptp_ocp_mro50_ioctl_blob(bp, cmd, arg, 256);
	case MRO50_READ_EXTENDED_EEPROM_BLOB:
	case MRO50_WRITE_EXTENDED_EEPROM_BLOB:
		return ptp_ocp_mro50_ioctl_blob(bp, cmd, arg, 512);
	default:
		return -ENOTTY;
	}

	if (!err && put_user(val, (int __user *)arg))
		err = -EFAULT;

	return err;
}

static const struct file_operations ptp_ocp_mro50_fops = {
	.owner =		THIS_MODULE,
	.unlocked_ioctl =	ptp_ocp_mro50_ioctl,
};

static void
ptp_ocp_unregister_mro50(struct miscdevice *mro50)
{
	struct ptp_ocp *bp = container_of(mro50, struct ptp_ocp, mro50);

	iowrite32(0, &bp->osc->ctrl);

	misc_deregister(mro50);
	kfree(mro50->name);
}

static int
ptp_ocp_register_mro50(struct ptp_ocp *bp)
{
	struct miscdevice *mro50 = &bp->mro50;
	char *name;
	int len;
	int err;

	len = strlen("mro50.X") + 1;

	name = kmalloc(len, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	snprintf(name, len, "mro50.%d", bp->id);

	mro50->minor = MISC_DYNAMIC_MINOR;
	mro50->fops = &ptp_ocp_mro50_fops;
	mro50->name = name;

	err = misc_register(mro50);
	if (err)
		goto out;

	iowrite32(MRO50_CTRL_ENABLE, &bp->osc->ctrl);

	return 0;

out:
	kfree(name);
	return err;
}

/* ART specific board initializers; last "resource" registered. */
static int
ptp_ocp_art_board_init(struct ptp_ocp *bp, struct ocp_resource *r)
{
	int err;

	bp->flash_start = 0x1000000;
	bp->config_start = bp->flash_start - OCP_CONFIG_SIZE;
	bp->eeprom_map = art_eeprom_map;
	bp->attr_tbl = art_timecard_groups;
	bp->fw_version = ioread32(&bp->reg->version);
	bp->fw_cap = OCP_CAP_BASIC;
	bp->fw_tag = 2;
	bp->sma_op = &ocp_art_sma_op;

	ptp_ocp_sma_init(bp);

	err = ptp_ocp_register_mro50(bp);
	if (!err)
		err = ptp_ocp_init_clock(bp);

	return err;
}

static bool
ptp_ocp_allow_irq(struct ptp_ocp *bp, struct ocp_resource *r)
{
	bool allow = !r->irq_vec || r->irq_vec < bp->n_irqs;

	if (!allow)
		dev_err(&bp->pdev->dev, "irq %d out of range, skipping %s\n",
			r->irq_vec, r->name);
	return allow;
}

static struct ocp_resource *
ptp_ocp_get_resources(struct ptp_ocp *bp, kernel_ulong_t driver_data)
{
	struct ocp_driver_data *ocp_driver_data;

	ocp_driver_data = (struct ocp_driver_data *)driver_data;

	if (bp->pdev->msix_enabled && ocp_driver_data->ocp_resource_msix)
		return ocp_driver_data->ocp_resource_msix;

	return ocp_driver_data->ocp_resource_msi;
}

static int
ptp_ocp_register_resources(struct ptp_ocp *bp, kernel_ulong_t driver_data)
{
	struct ocp_resource *r, *table;
	int err;

	/* Get driver resources matching device revision */
	table = ptp_ocp_get_resources(bp, driver_data);

	for (r = table; r->setup; r++) {
		if (!ptp_ocp_allow_irq(bp, r))
			continue;
		err = r->setup(bp, r);
		if (err) {
			dev_err(&bp->pdev->dev,
				"Could not register %s: err %d, skipping.\n",
				r->name, err);
			continue;
		}
	}
	return 0;
}

static void
ptp_ocp_enable_fpga(u32 __iomem *reg, u32 bit, bool enable)
{
	u32 ctrl;
	bool on;

	ctrl = ioread32(reg);
	on = ctrl & bit;
	if (on ^ enable) {
		ctrl &= ~bit;
		ctrl |= enable ? bit : 0;
		iowrite32(ctrl, reg);
	}
}

static void
ptp_ocp_irig_out(struct ptp_ocp *bp, bool enable)
{
	return ptp_ocp_enable_fpga(&bp->irig_out->ctrl,
				   IRIG_M_CTRL_ENABLE, enable);
}

static void
ptp_ocp_irig_in(struct ptp_ocp *bp, bool enable)
{
	return ptp_ocp_enable_fpga(&bp->irig_in->ctrl,
				   IRIG_S_CTRL_ENABLE, enable);
}

static void
ptp_ocp_dcf_out(struct ptp_ocp *bp, bool enable)
{
	return ptp_ocp_enable_fpga(&bp->dcf_out->ctrl,
				   DCF_M_CTRL_ENABLE, enable);
}

static void
ptp_ocp_dcf_in(struct ptp_ocp *bp, bool enable)
{
	return ptp_ocp_enable_fpga(&bp->dcf_in->ctrl,
				   DCF_S_CTRL_ENABLE, enable);
}

static void
__handle_signal_outputs(struct ptp_ocp *bp, u32 val)
{
	ptp_ocp_irig_out(bp, val & 0x00100010);
	ptp_ocp_dcf_out(bp, val & 0x00200020);
}

static void
__handle_signal_inputs(struct ptp_ocp *bp, u32 val)
{
	ptp_ocp_irig_in(bp, val & 0x00100010);
	ptp_ocp_dcf_in(bp, val & 0x00200020);
}

static void
ptp_ocp_sma_fb_init(struct ptp_ocp *bp)
{
	u32 reg;
	int i;

	/* defaults */
	bp->sma[0].mode = SMA_MODE_IN;
	bp->sma[1].mode = SMA_MODE_IN;
	bp->sma[2].mode = SMA_MODE_OUT;
	bp->sma[3].mode = SMA_MODE_OUT;
	for (i = 0; i < 4; i++)
		bp->sma[i].default_fcn = i & 1;

	/* If no SMA1 map, the pin functions and directions are fixed. */
	if (!bp->sma_map1) {
		for (i = 0; i < 4; i++) {
			bp->sma[i].fixed_fcn = true;
			bp->sma[i].fixed_dir = true;
		}
		return;
	}

	/* If SMA2 GPIO output map is all 1, it is not present.
	 * This indicates the firmware has fixed direction SMA pins.
	 */
	reg = ioread32(&bp->sma_map2->gpio2);
	if (reg == 0xffffffff) {
		for (i = 0; i < 4; i++)
			bp->sma[i].fixed_dir = true;
	} else {
		reg = ioread32(&bp->sma_map1->gpio1);
		bp->sma[0].mode = reg & BIT(15) ? SMA_MODE_IN : SMA_MODE_OUT;
		bp->sma[1].mode = reg & BIT(31) ? SMA_MODE_IN : SMA_MODE_OUT;

		reg = ioread32(&bp->sma_map1->gpio2);
		bp->sma[2].mode = reg & BIT(15) ? SMA_MODE_OUT : SMA_MODE_IN;
		bp->sma[3].mode = reg & BIT(31) ? SMA_MODE_OUT : SMA_MODE_IN;
	}
}

static u32
ptp_ocp_sma_fb_get(struct ptp_ocp *bp, int sma_nr)
{
	u32 __iomem *gpio;
	u32 shift;

	if (bp->sma[sma_nr - 1].fixed_fcn)
		return bp->sma[sma_nr - 1].default_fcn;

	if (bp->sma[sma_nr - 1].mode == SMA_MODE_IN)
		gpio = sma_nr > 2 ? &bp->sma_map2->gpio1 : &bp->sma_map1->gpio1;
	else
		gpio = sma_nr > 2 ? &bp->sma_map1->gpio2 : &bp->sma_map2->gpio2;
	shift = sma_nr & 1 ? 0 : 16;

	return (ioread32(gpio) >> shift) & 0xffff;
}

static int
ptp_ocp_sma_fb_set_output(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	u32 reg, mask, shift;
	unsigned long flags;
	u32 __iomem *gpio;

	gpio = sma_nr > 2 ? &bp->sma_map1->gpio2 : &bp->sma_map2->gpio2;
	shift = sma_nr & 1 ? 0 : 16;

	mask = 0xffff << (16 - shift);

	spin_lock_irqsave(&bp->lock, flags);

	reg = ioread32(gpio);
	reg = (reg & mask) | (val << shift);

	__handle_signal_outputs(bp, reg);

	iowrite32(reg, gpio);

	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static int
ptp_ocp_sma_fb_set_inputs(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	u32 reg, mask, shift;
	unsigned long flags;
	u32 __iomem *gpio;

	gpio = sma_nr > 2 ? &bp->sma_map2->gpio1 : &bp->sma_map1->gpio1;
	shift = sma_nr & 1 ? 0 : 16;

	mask = 0xffff << (16 - shift);

	spin_lock_irqsave(&bp->lock, flags);

	reg = ioread32(gpio);
	reg = (reg & mask) | (val << shift);

	__handle_signal_inputs(bp, reg);

	iowrite32(reg, gpio);

	spin_unlock_irqrestore(&bp->lock, flags);

	return 0;
}

static const struct ocp_sma_op ocp_fb_sma_op = {
	.tbl		= { ptp_ocp_sma_in, ptp_ocp_sma_out },
	.init		= ptp_ocp_sma_fb_init,
	.get		= ptp_ocp_sma_fb_get,
	.set_inputs	= ptp_ocp_sma_fb_set_inputs,
	.set_output	= ptp_ocp_sma_fb_set_output,
};

static void
ptp_ocp_art_sma_init(struct ptp_ocp *bp)
{
	u32 reg;
	int i;

	/* defaults */
	bp->sma[0].mode = SMA_MODE_IN;
	bp->sma[1].mode = SMA_MODE_IN;
	bp->sma[2].mode = SMA_MODE_OUT;
	bp->sma[3].mode = SMA_MODE_OUT;

	bp->sma[0].default_fcn = 0x08;	/* IN: 10Mhz */
	bp->sma[1].default_fcn = 0x01;	/* IN: PPS1 */
	bp->sma[2].default_fcn = 0x10;	/* OUT: 10Mhz */
	bp->sma[3].default_fcn = 0x02;	/* OUT: PHC */

	/* If no SMA map, the pin functions and directions are fixed. */
	if (!bp->art_sma) {
		for (i = 0; i < 4; i++) {
			bp->sma[i].fixed_fcn = true;
			bp->sma[i].fixed_dir = true;
		}
		return;
	}

	for (i = 0; i < 4; i++) {
		reg = ioread32(&bp->art_sma->map[i].gpio);

		switch (reg & 0xff) {
		case 0:
			bp->sma[i].fixed_fcn = true;
			bp->sma[i].fixed_dir = true;
			break;
		case 1:
		case 8:
			bp->sma[i].mode = SMA_MODE_IN;
			break;
		default:
			bp->sma[i].mode = SMA_MODE_OUT;
			break;
		}
	}
}

static u32
ptp_ocp_art_sma_get(struct ptp_ocp *bp, int sma_nr)
{
	if (bp->sma[sma_nr - 1].fixed_fcn)
		return bp->sma[sma_nr - 1].default_fcn;

	return ioread32(&bp->art_sma->map[sma_nr - 1].gpio) & 0xff;
}

/* note: store 0 is considered invalid. */
static int
ptp_ocp_art_sma_set(struct ptp_ocp *bp, int sma_nr, u32 val)
{
	u32 __iomem *gpio;
	u32 reg;

	val &= SMA_SELECT_MASK;
	if (hweight32(val) > 1)
		return -EINVAL;

	gpio = &bp->art_sma->map[sma_nr - 1].gpio;

	reg = ioread32(gpio);
	if (((reg >> 16) & val) == 0)
		return -EOPNOTSUPP;

	reg = (reg & 0xff00) | (val & 0xff);
	iowrite32(reg, gpio);

	return 0;
}

static const struct ocp_sma_op ocp_art_sma_op = {
	.tbl		= { ptp_ocp_art_sma_in, ptp_ocp_art_sma_out },
	.init		= ptp_ocp_art_sma_init,
	.get		= ptp_ocp_art_sma_get,
	.set_inputs	= ptp_ocp_art_sma_set,
	.set_output	= ptp_ocp_art_sma_set,
};

#ifndef to_ext_attr
#define to_ext_attr(x) container_of(x, struct dev_ext_attribute, attr)
#endif

static ssize_t
ptp_ocp_tty_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "ttyS%d", bp->port[(uintptr_t)ea->var].line);
}

static umode_t
ptp_ocp_timecard_tty_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct ptp_ocp *bp = dev_get_drvdata(kobj_to_dev(kobj));
	struct ptp_ocp_serial_port *port;
	struct device_attribute *dattr;
	struct dev_ext_attribute *ea;

	if (strncmp(attr->name, "tty", 3))
		return attr->mode;

	dattr = container_of(attr, struct device_attribute, attr);
	ea = container_of(dattr, struct dev_ext_attribute, attr);
	port = &bp->port[(uintptr_t)ea->var];
	return port->line == -1 ? 0 : 0444;
}

#define EXT_TTY_ATTR_RO(_name, _val)			\
	struct dev_ext_attribute dev_attr_tty##_name =	\
		{ __ATTR(tty##_name, 0444, ptp_ocp_tty_show, NULL), (void *)_val }

static EXT_TTY_ATTR_RO(GNSS, PORT_GNSS);
static EXT_TTY_ATTR_RO(GNSS2, PORT_GNSS2);
static EXT_TTY_ATTR_RO(MAC, PORT_MAC);
static EXT_TTY_ATTR_RO(NMEA, PORT_NMEA);
static struct attribute *ptp_ocp_timecard_tty_attrs[] = {
	&dev_attr_ttyGNSS.attr.attr,
	&dev_attr_ttyGNSS2.attr.attr,
	&dev_attr_ttyMAC.attr.attr,
	&dev_attr_ttyNMEA.attr.attr,
	NULL,
};

static const struct attribute_group ptp_ocp_timecard_tty_group = {
	.name = "tty",
	.attrs = ptp_ocp_timecard_tty_attrs,
	.is_visible = ptp_ocp_timecard_tty_is_visible,
};

static ssize_t
ptp_ocp_show_output(const struct ocp_selector *tbl, u32 val, char *buf,
		    int def_val)
{
	const char *name;
	ssize_t count;

	count = sysfs_emit(buf, "OUT: ");
	name = ptp_ocp_select_name_from_val(tbl, val);
	if (!name)
		name = ptp_ocp_select_name_from_val(tbl, def_val);
	count += sysfs_emit_at(buf, count, "%s\n", name);
	return count;
}

static ssize_t
ptp_ocp_show_inputs(const struct ocp_selector *tbl, u32 val, char *buf,
		    int def_val)
{
	const char *name;
	ssize_t count;
	int i;

	count = sysfs_emit(buf, "IN: ");
	for (i = 0; tbl[i].name; i++) {
		if (val & tbl[i].value) {
			name = tbl[i].name;
			count += sysfs_emit_at(buf, count, "%s ", name);
		}
	}
	if (!val && def_val >= 0) {
		name = ptp_ocp_select_name_from_val(tbl, def_val);
		count += sysfs_emit_at(buf, count, "%s ", name);
	}
	if (count)
		count--;
	count += sysfs_emit_at(buf, count, "\n");
	return count;
}

static int
sma_parse_inputs(const struct ocp_selector * const tbl[], const char *buf,
		 enum ptp_ocp_sma_mode *mode)
{
	int idx, count, dir;
	char **argv;
	int ret;

	argv = argv_split(GFP_KERNEL, buf, &count);
	if (!argv)
		return -ENOMEM;

	ret = -EINVAL;
	if (!count)
		goto out;

	idx = 0;
	dir = *mode == SMA_MODE_IN ? 0 : 1;
	if (!strcasecmp("IN:", argv[0])) {
		dir = 0;
		idx++;
	}
	if (!strcasecmp("OUT:", argv[0])) {
		dir = 1;
		idx++;
	}
	*mode = dir == 0 ? SMA_MODE_IN : SMA_MODE_OUT;

	ret = 0;
	for (; idx < count; idx++)
		ret |= ptp_ocp_select_val_from_name(tbl[dir], argv[idx]);
	if (ret < 0)
		ret = -EINVAL;

out:
	argv_free(argv);
	return ret;
}

static ssize_t
ptp_ocp_sma_show(struct ptp_ocp *bp, int sma_nr, char *buf,
		 int default_in_val, int default_out_val)
{
	struct ptp_ocp_sma_connector *sma = &bp->sma[sma_nr - 1];
	const struct ocp_selector * const *tbl;
	u32 val;

	tbl = bp->sma_op->tbl;
	val = ptp_ocp_sma_get(bp, sma_nr) & SMA_SELECT_MASK;

	if (sma->mode == SMA_MODE_IN) {
		if (sma->disabled)
			val = SMA_DISABLE;
		return ptp_ocp_show_inputs(tbl[0], val, buf, default_in_val);
	}

	return ptp_ocp_show_output(tbl[1], val, buf, default_out_val);
}

static ssize_t
sma1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_sma_show(bp, 1, buf, 0, 1);
}

static ssize_t
sma2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_sma_show(bp, 2, buf, -1, 1);
}

static ssize_t
sma3_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_sma_show(bp, 3, buf, -1, 0);
}

static ssize_t
sma4_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_sma_show(bp, 4, buf, -1, 1);
}

static int
ptp_ocp_sma_store(struct ptp_ocp *bp, const char *buf, int sma_nr)
{
	struct ptp_ocp_sma_connector *sma = &bp->sma[sma_nr - 1];
	enum ptp_ocp_sma_mode mode;
	int val;

	mode = sma->mode;
	val = sma_parse_inputs(bp->sma_op->tbl, buf, &mode);
	if (val < 0)
		return val;

	if (sma->fixed_dir && (mode != sma->mode || val & SMA_DISABLE))
		return -EOPNOTSUPP;

	if (sma->fixed_fcn) {
		if (val != sma->default_fcn)
			return -EOPNOTSUPP;
		return 0;
	}

	sma->disabled = !!(val & SMA_DISABLE);

	if (mode != sma->mode) {
		if (mode == SMA_MODE_IN)
			ptp_ocp_sma_set_output(bp, sma_nr, 0);
		else
			ptp_ocp_sma_set_inputs(bp, sma_nr, 0);
		sma->mode = mode;
	}

	if (!sma->fixed_dir)
		val |= SMA_ENABLE;		/* add enable bit */

	if (sma->disabled)
		val = 0;

	if (mode == SMA_MODE_IN)
		val = ptp_ocp_sma_set_inputs(bp, sma_nr, val);
	else
		val = ptp_ocp_sma_set_output(bp, sma_nr, val);

	return val;
}

static ssize_t
sma1_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_sma_store(bp, buf, 1);
	return err ? err : count;
}

static ssize_t
sma2_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_sma_store(bp, buf, 2);
	return err ? err : count;
}

static ssize_t
sma3_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_sma_store(bp, buf, 3);
	return err ? err : count;
}

static ssize_t
sma4_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_sma_store(bp, buf, 4);
	return err ? err : count;
}
static DEVICE_ATTR_RW(sma1);
static DEVICE_ATTR_RW(sma2);
static DEVICE_ATTR_RW(sma3);
static DEVICE_ATTR_RW(sma4);

static ssize_t
available_sma_inputs_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_select_table_show(bp->sma_op->tbl[0], buf);
}
static DEVICE_ATTR_RO(available_sma_inputs);

static ssize_t
available_sma_outputs_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return ptp_ocp_select_table_show(bp->sma_op->tbl[1], buf);
}
static DEVICE_ATTR_RO(available_sma_outputs);

#define EXT_ATTR_RO(_group, _name, _val)				\
	struct dev_ext_attribute dev_attr_##_group##_val##_##_name =	\
		{ __ATTR_RO(_name), (void *)_val }
#define EXT_ATTR_RW(_group, _name, _val)				\
	struct dev_ext_attribute dev_attr_##_group##_val##_##_name =	\
		{ __ATTR_RW(_name), (void *)_val }
#define to_ext_attr(x) container_of(x, struct dev_ext_attribute, attr)

/* period [duty [phase [polarity]]] */
static ssize_t
signal_store(struct device *dev, struct device_attribute *attr,
	     const char *buf, size_t count)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	struct ptp_ocp_signal s = { };
	int gen = (uintptr_t)ea->var;
	int argc, err;
	char **argv;

	argv = argv_split(GFP_KERNEL, buf, &argc);
	if (!argv)
		return -ENOMEM;

	err = -EINVAL;
	s.duty = bp->signal[gen].duty;
	s.phase = bp->signal[gen].phase;
	s.period = bp->signal[gen].period;
	s.polarity = bp->signal[gen].polarity;
	s.repeat_count = bp->signal[gen].repeat_count;
	s.cable_delay = bp->signal[gen].cable_delay;

	switch (argc) {
	case 4:
		argc--;
		err = kstrtobool(argv[argc], &s.polarity);
		if (err)
			goto out;
		fallthrough;
	case 3:
		argc--;
		err = kstrtou64(argv[argc], 0, &s.phase);
		if (err)
			goto out;
		fallthrough;
	case 2:
		argc--;
		err = kstrtoint(argv[argc], 0, &s.duty);
		if (err)
			goto out;
		fallthrough;
	case 1:
		argc--;
		err = kstrtou64(argv[argc], 0, &s.period);
		if (err)
			goto out;
		break;
	default:
		goto out;
	}

	err = ptp_ocp_signal_set(bp, gen, &s);
	if (err)
		goto out;

	err = ptp_ocp_signal_enable(bp->signal_out[gen],
				    NULL, gen, s.period != 0);

out:
	argv_free(argv);
	return err ? err : count;
}

static ssize_t
signal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	struct ptp_ocp_signal *signal;
	struct timespec64 ts;
	ssize_t count;
	int i;

	i = (uintptr_t)ea->var;
	signal = &bp->signal[i];

	count = sysfs_emit(buf, "%llu %d %llu %d", signal->period,
			   signal->duty, signal->phase, signal->polarity);

	ts = ktime_to_timespec64(signal->start);
	count += sysfs_emit_at(buf, count, " %ptT TAI\n", &ts);

	return count;
}
static EXT_ATTR_RW(signal, signal, 0);
static EXT_ATTR_RW(signal, signal, 1);
static EXT_ATTR_RW(signal, signal, 2);
static EXT_ATTR_RW(signal, signal, 3);

static ssize_t
duty_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%d\n", bp->signal[i].duty);
}
static EXT_ATTR_RO(signal, duty, 0);
static EXT_ATTR_RO(signal, duty, 1);
static EXT_ATTR_RO(signal, duty, 2);
static EXT_ATTR_RO(signal, duty, 3);

static ssize_t
period_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%llu\n", bp->signal[i].period);
}
static EXT_ATTR_RO(signal, period, 0);
static EXT_ATTR_RO(signal, period, 1);
static EXT_ATTR_RO(signal, period, 2);
static EXT_ATTR_RO(signal, period, 3);

static ssize_t
phase_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%llu\n", bp->signal[i].phase);
}
static EXT_ATTR_RO(signal, phase, 0);
static EXT_ATTR_RO(signal, phase, 1);
static EXT_ATTR_RO(signal, phase, 2);
static EXT_ATTR_RO(signal, phase, 3);

static ssize_t
polarity_show(struct device *dev, struct device_attribute *attr,
	      char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%d\n", bp->signal[i].polarity);
}
static EXT_ATTR_RO(signal, polarity, 0);
static EXT_ATTR_RO(signal, polarity, 1);
static EXT_ATTR_RO(signal, polarity, 2);
static EXT_ATTR_RO(signal, polarity, 3);

static void
ptp_ocp_signal_latch_if_running(struct signal_reg __iomem *reg)
{
	u32 ctrl;

	ctrl = ioread32(&reg->enable);
	if (ctrl & SIGNAL_CTRL_ENABLE)
		iowrite32(ctrl | SIGNAL_CTRL_VALUES_VALID, &reg->enable);
}

static ssize_t
repeat_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;
	struct signal_reg __iomem *reg;

	if (!bp->signal_out[i])
		return -EOPNOTSUPP;
	reg = bp->signal_out[i]->mem;
	if (!ptp_ocp_core_version_at_least(&reg->version,
						SIGNAL_VERSION_CURRENT_MAP))
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n", bp->signal[i].repeat_count);
}

static ssize_t
repeat_count_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;
	struct signal_reg __iomem *reg;
	unsigned long flags;
	u32 repeat_count;
	int err;

	if (!bp->signal_out[i])
		return -EOPNOTSUPP;
	reg = bp->signal_out[i]->mem;
	if (!ptp_ocp_core_version_at_least(&reg->version,
						SIGNAL_VERSION_CURRENT_MAP))
		return -EOPNOTSUPP;
	err = kstrtou32(buf, 0, &repeat_count);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	bp->signal[i].repeat_count = repeat_count;
	iowrite32(repeat_count, &reg->repeat_count);
	ptp_ocp_signal_latch_if_running(reg);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static EXT_ATTR_RW(signal, repeat_count, 0);
static EXT_ATTR_RW(signal, repeat_count, 1);
static EXT_ATTR_RW(signal, repeat_count, 2);
static EXT_ATTR_RW(signal, repeat_count, 3);

static ssize_t
cable_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;
	struct signal_reg __iomem *reg;

	if (!bp->signal_out[i])
		return -EOPNOTSUPP;
	reg = bp->signal_out[i]->mem;
	if (!ptp_ocp_core_version_at_least(&reg->version,
						SIGNAL_VERSION_CABLE_DELAY))
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n", bp->signal[i].cable_delay);
}

static ssize_t
cable_delay_store(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;
	struct signal_reg __iomem *reg;
	unsigned long flags;
	u16 cable_delay;
	int err;

	if (!bp->signal_out[i])
		return -EOPNOTSUPP;
	reg = bp->signal_out[i]->mem;
	if (!ptp_ocp_core_version_at_least(&reg->version,
						SIGNAL_VERSION_CABLE_DELAY))
		return -EOPNOTSUPP;
	err = kstrtou16(buf, 0, &cable_delay);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	bp->signal[i].cable_delay = cable_delay;
	iowrite32(cable_delay, &reg->cable_delay);
	ptp_ocp_signal_latch_if_running(reg);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static EXT_ATTR_RW(signal, cable_delay, 0);
static EXT_ATTR_RW(signal, cable_delay, 1);
static EXT_ATTR_RW(signal, cable_delay, 2);
static EXT_ATTR_RW(signal, cable_delay, 3);

static ssize_t
running_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;

	return sysfs_emit(buf, "%d\n", bp->signal[i].running);
}
static EXT_ATTR_RO(signal, running, 0);
static EXT_ATTR_RO(signal, running, 1);
static EXT_ATTR_RO(signal, running, 2);
static EXT_ATTR_RO(signal, running, 3);

static ssize_t
start_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int i = (uintptr_t)ea->var;
	struct timespec64 ts;

	ts = ktime_to_timespec64(bp->signal[i].start);
	return sysfs_emit(buf, "%llu.%lu\n", ts.tv_sec, ts.tv_nsec);
}
static EXT_ATTR_RO(signal, start, 0);
static EXT_ATTR_RO(signal, start, 1);
static EXT_ATTR_RO(signal, start, 2);
static EXT_ATTR_RO(signal, start, 3);

static ssize_t
seconds_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int idx = (uintptr_t)ea->var;
	u32 val;
	int err;

	err = kstrtou32(buf, 0, &val);
	if (err)
		return err;
	if (val > 0xff)
		return -EINVAL;

	if (val)
		val = (val << 8) | 0x1;

	iowrite32(val, &bp->freq_in[idx]->ctrl);

	return count;
}

static ssize_t
seconds_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int idx = (uintptr_t)ea->var;
	u32 val;

	val = ioread32(&bp->freq_in[idx]->ctrl);
	if (val & 1)
		val = (val >> 8) & 0xff;
	else
		val = 0;

	return sysfs_emit(buf, "%u\n", val);
}
static EXT_ATTR_RW(freq, seconds, 0);
static EXT_ATTR_RW(freq, seconds, 1);
static EXT_ATTR_RW(freq, seconds, 2);
static EXT_ATTR_RW(freq, seconds, 3);

static ssize_t
frequency_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int idx = (uintptr_t)ea->var;
	u32 val;

	val = ioread32(&bp->freq_in[idx]->status);
	if (val & FREQ_STATUS_ERROR)
		return sysfs_emit(buf, "error\n");
	if (val & FREQ_STATUS_OVERRUN)
		return sysfs_emit(buf, "overrun\n");
	if (val & FREQ_STATUS_VALID)
		return sysfs_emit(buf, "%lu\n", val & FREQ_STATUS_MASK);
	return 0;
}
static EXT_ATTR_RO(freq, frequency, 0);
static EXT_ATTR_RO(freq, frequency, 1);
static EXT_ATTR_RO(freq, frequency, 2);
static EXT_ATTR_RO(freq, frequency, 3);

static ssize_t
serialnum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!bp->has_eeprom_data)
		ptp_ocp_read_eeprom(bp);

	return sysfs_emit(buf, "%pM\n", bp->serial);
}
static DEVICE_ATTR_RO(serialnum);

static ssize_t
gnss_sync_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	ssize_t ret;

	if (bp->gnss_lost)
		ret = sysfs_emit(buf, "LOST @ %ptT\n", &bp->gnss_lost);
	else
		ret = sysfs_emit(buf, "SYNC\n");

	return ret;
}
static DEVICE_ATTR_RO(gnss_sync);

static ssize_t
utc_tai_offset_show(struct device *dev,
		    struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", bp->utc_tai_offset);
}

static ssize_t
utc_tai_offset_store(struct device *dev,
		     struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;
	u32 val;

	err = kstrtou32(buf, 0, &val);
	if (err)
		return err;
	if (val > TIMECODE_COR_MAG_MASK)
		return -ERANGE;

	ptp_ocp_utc_distribute(bp, val);

	return count;
}
static DEVICE_ATTR_RW(utc_tai_offset);

static int
ptp_ocp_pps_require_version(struct pps_reg __iomem *reg, u32 minimum)
{
	if (!reg || !ptp_ocp_core_version_at_least(&reg->version, minimum))
		return -EOPNOTSUPP;

	return 0;
}

static void
ptp_ocp_pps_config_write(struct pps_reg __iomem *reg,
			 u32 __iomem *config_reg, u32 value)
{
	u32 ctrl;

	ctrl = ioread32(&reg->ctrl);
	if (ctrl & BIT(0))
		iowrite32(ctrl & ~BIT(0), &reg->ctrl);
	iowrite32(value, config_reg);
	if (ctrl & BIT(0))
		iowrite32(ctrl, &reg->ctrl);
}

static ssize_t
external_pps_polarity_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_ext,
					  PPS_MASTER_VERSION_POLARITY);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->pps_to_ext->polarity) & 1U);
}

static ssize_t
external_pps_polarity_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	bool polarity;
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_ext,
					  PPS_MASTER_VERSION_POLARITY);
	if (err)
		return err;
	err = kstrtobool(buf, &polarity);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	ptp_ocp_pps_config_write(bp->pps_to_ext,
				 &bp->pps_to_ext->polarity, polarity);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(external_pps_polarity);

static ssize_t
internal_pps_polarity_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_clk,
					  PPS_SLAVE_VERSION_POLARITY);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->pps_to_clk->polarity) & 1U);
}

static ssize_t
internal_pps_polarity_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	bool polarity;
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_clk,
					  PPS_SLAVE_VERSION_POLARITY);
	if (err)
		return err;
	err = kstrtobool(buf, &polarity);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	ptp_ocp_pps_config_write(bp->pps_to_clk,
				 &bp->pps_to_clk->polarity, polarity);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(internal_pps_polarity);

static ssize_t
external_pps_pulse_width_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_ext,
					  PPS_VERSION_PULSE_WIDTH);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->pps_to_ext->pulse_width));
}

static ssize_t
external_pps_pulse_width_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 pulse_width;
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_ext,
					  PPS_VERSION_PULSE_WIDTH);
	if (err)
		return err;
	err = kstrtou32(buf, 0, &pulse_width);
	if (err)
		return err;
	if (pulse_width < PPS_MASTER_PULSE_WIDTH_MIN ||
	    pulse_width > PPS_MASTER_PULSE_WIDTH_MAX)
		return -ERANGE;

	spin_lock_irqsave(&bp->lock, flags);
	ptp_ocp_pps_config_write(bp->pps_to_ext,
				 &bp->pps_to_ext->pulse_width, pulse_width);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(external_pps_pulse_width);

static u32
ptp_ocp_pps_cable_delay_mask(struct pps_reg __iomem *reg)
{
	return ptp_ocp_core_version_at_least(
			&reg->version, PPS_VERSION_WIDE_CABLE_DELAY) ?
		PPS_CABLE_DELAY_MAG_MASK : PPS_CABLE_DELAY_LEGACY_MASK;
}

static s32
ptp_ocp_pps_cable_delay_decode(u32 reg, u32 magnitude_mask)
{
	s32 delay = reg & magnitude_mask;

	return reg & PPS_CABLE_DELAY_SIGN ? -delay : delay;
}

static int
ptp_ocp_pps_cable_delay_encode(s32 delay, u32 magnitude_mask, u32 *reg)
{
	u32 magnitude;

	if (delay < -(s32)magnitude_mask || delay > (s32)magnitude_mask)
		return -ERANGE;

	magnitude = delay < 0 ? -delay : delay;
	*reg = magnitude | (delay < 0 ? PPS_CABLE_DELAY_SIGN : 0);
	return 0;
}

static ssize_t
external_pps_cable_delay_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	s32 val;
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_ext,
					  PPS_MASTER_VERSION_CABLE_DELAY);
	if (err)
		return err;

	val = ptp_ocp_pps_cable_delay_decode(
		ioread32(&bp->pps_to_ext->cable_delay),
		ptp_ocp_pps_cable_delay_mask(bp->pps_to_ext));
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t
external_pps_cable_delay_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 reg;
	s32 val;
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_ext,
					  PPS_MASTER_VERSION_CABLE_DELAY);
	if (err)
		return err;
	err = kstrtos32(buf, 0, &val);
	if (err)
		return err;
	err = ptp_ocp_pps_cable_delay_encode(
		val, ptp_ocp_pps_cable_delay_mask(bp->pps_to_ext), &reg);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	ptp_ocp_pps_config_write(bp->pps_to_ext,
				 &bp->pps_to_ext->cable_delay, reg);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(external_pps_cable_delay);

static ssize_t
internal_pps_cable_delay_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	s32 val;
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_clk,
					  FPGA_CORE_VERSION(1, 0));
	if (err)
		return err;

	val = ptp_ocp_pps_cable_delay_decode(
		ioread32(&bp->pps_to_clk->cable_delay),
		ptp_ocp_pps_cable_delay_mask(bp->pps_to_clk));
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t
internal_pps_cable_delay_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 reg;
	s32 val;
	int err;

	err = ptp_ocp_pps_require_version(bp->pps_to_clk,
					  FPGA_CORE_VERSION(1, 0));
	if (err)
		return err;
	err = kstrtos32(buf, 0, &val);
	if (err)
		return err;
	err = ptp_ocp_pps_cable_delay_encode(
		val, ptp_ocp_pps_cable_delay_mask(bp->pps_to_clk), &reg);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	ptp_ocp_pps_config_write(bp->pps_to_clk,
				 &bp->pps_to_clk->cable_delay, reg);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(internal_pps_cable_delay);

static ssize_t
holdover_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val = 0;

	if (bp->pps_select)
		val = ioread32(&bp->pps_select->gpio2) & 0x3;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t
holdover_store(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 val, reg;
	int err;

	if (!bp->pps_select)
		return -EOPNOTSUPP;

	err = kstrtou32(buf, 0, &val);
	if (err)
		return err;

	if (val > 3)
		return -EINVAL;

	spin_lock_irqsave(&bp->lock, flags);
	reg = ioread32(&bp->pps_select->gpio2);
	reg = (reg & ~0x3) | val;
	iowrite32(val, &bp->pps_select->gpio2);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(holdover);

static struct ocp_resource ocp_mac_resource[] = {
	[0] = {
		OCP_SERIAL_RESOURCE(port[PORT_MAC]),
		.offset = 0x00180000 + 0x1000, .irq_vec = 5,
		.extra = &(struct ptp_ocp_serial_port) {
			.baud = 57600,
		},

	},
	[1] = {
		OCP_I2C_RESOURCE(i2c_mac),
		.offset = 0x00200000, .size = 0x10000, .irq_vec = 5,
		.extra = &(struct ptp_ocp_i2c_info) {
			.name = "xiic-i2c",
			.fixed_rate = 50000000,
		}
	},
};

/* change selection */
static int
mac_mode_select(struct ptp_ocp *bp, bool use_i2c)
{
	struct ocp_resource *r;
	int err;

	if (bp->i2c_mac) {
		platform_device_unregister(bp->i2c_mac);
		bp->i2c_mac = NULL;
	}

	if (bp->port[PORT_MAC].line != -1) {
		serial8250_unregister_port(bp->port[PORT_MAC].line);
		bp->port[PORT_MAC].line = -1;
	}

	r = &ocp_mac_resource[use_i2c];

	err = r->setup(bp, r);
	if (err)
		dev_err(&bp->pdev->dev,
			"Could not register %s: err %d, skipping.\n",
			r->name, err);

	return err;
}

static bool
mac_mode_selected(struct ptp_ocp *bp, bool use_i2c)
{
	if (use_i2c)
		return bp->i2c_mac != NULL;

	return bp->port[PORT_MAC].line != -1;
}

static ssize_t
mac_i2c_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	bool i2c_mode, uart_mode;
	int val;

	i2c_mode = mac_mode_selected(bp, true);
	uart_mode = mac_mode_selected(bp, false);
	if (!i2c_mode && !uart_mode)
		val = -1;
	else
		val = i2c_mode;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t
mac_i2c_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	bool use_i2c;
	u32 reg;
	int err;

	err = kstrtobool(buf, &use_i2c);
	if (err)
		return err;

	/* already selected this mode */
	if (mac_mode_selected(bp, use_i2c))
		goto out;

	/* select correct mode */
	spin_lock_irqsave(&bp->lock, flags);
	reg = ioread32(&bp->ext_ctrl->gpio2);
	reg = (reg &~ BIT(31)) | (use_i2c ? BIT(31) : 0);
	iowrite32(reg, &bp->ext_ctrl->gpio2);
	spin_unlock_irqrestore(&bp->lock, flags);

	err = mac_mode_select(bp, use_i2c);
	if (err)
		return err;

out:
	return count;
}
static DEVICE_ATTR_RW(mac_i2c);

static ssize_t
ts_window_adjust_show(struct device *dev,
		      struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", bp->ts_window_adjust);
}

static ssize_t
ts_window_adjust_store(struct device *dev,
		       struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;
	u32 val;

	err = kstrtou32(buf, 0, &val);
	if (err)
		return err;

	bp->ts_window_adjust = val;

	return count;
}
static DEVICE_ATTR_RW(ts_window_adjust);

static ssize_t
irig_input_cable_delay_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!bp->irig_in)
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->irig_in->cable_delay) &
			  IRIG_CABLE_DELAY_MASK);
}

static ssize_t
irig_input_cable_delay_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 ctrl;
	u16 delay;
	int err;

	if (!bp->irig_in)
		return -EOPNOTSUPP;

	err = kstrtou16(buf, 0, &delay);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->irig_in->ctrl);
	if (ctrl & IRIG_S_CTRL_ENABLE)
		iowrite32(ctrl & ~IRIG_S_CTRL_ENABLE, &bp->irig_in->ctrl);
	iowrite32(delay, &bp->irig_in->cable_delay);
	if (ctrl & IRIG_S_CTRL_ENABLE)
		iowrite32(ctrl, &bp->irig_in->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(irig_input_cable_delay);

static ssize_t
dcf_input_air_delay_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!bp->dcf_in)
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->dcf_in->air_delay) &
			  DCF_AIR_DELAY_MASK);
}

static ssize_t
dcf_input_air_delay_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 ctrl, delay;
	int err;

	if (!bp->dcf_in)
		return -EOPNOTSUPP;

	err = kstrtou32(buf, 0, &delay);
	if (err)
		return err;
	if (delay > DCF_AIR_DELAY_MASK)
		return -ERANGE;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->dcf_in->ctrl);
	if (ctrl & DCF_S_CTRL_ENABLE)
		iowrite32(ctrl & ~DCF_S_CTRL_ENABLE, &bp->dcf_in->ctrl);
	iowrite32(delay, &bp->dcf_in->air_delay);
	if (ctrl & DCF_S_CTRL_ENABLE)
		iowrite32(ctrl, &bp->dcf_in->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(dcf_input_air_delay);

static ssize_t
dcf_input_bit_position_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!bp->dcf_in)
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->dcf_in->bit_position) &
			  DCF_BIT_POSITION_MASK);
}
static DEVICE_ATTR_RO(dcf_input_bit_position);

enum timecode_error_index {
	TIMECODE_ERROR_IRIG_OUTPUT,
	TIMECODE_ERROR_IRIG_INPUT,
	TIMECODE_ERROR_DCF_OUTPUT,
	TIMECODE_ERROR_DCF_INPUT,
};

static u32 __iomem *
ptp_ocp_timecode_status_reg(struct ptp_ocp *bp, unsigned int index)
{
	u32 __iomem *status, *version;

	switch (index) {
	case TIMECODE_ERROR_IRIG_OUTPUT:
		if (!bp->irig_out)
			return NULL;
		status = &bp->irig_out->status;
		version = &bp->irig_out->version;
		break;
	case TIMECODE_ERROR_IRIG_INPUT:
		if (!bp->irig_in)
			return NULL;
		status = &bp->irig_in->status;
		version = &bp->irig_in->version;
		break;
	case TIMECODE_ERROR_DCF_OUTPUT:
		if (!bp->dcf_out)
			return NULL;
		status = &bp->dcf_out->status;
		version = &bp->dcf_out->version;
		break;
	case TIMECODE_ERROR_DCF_INPUT:
		if (!bp->dcf_in)
			return NULL;
		status = &bp->dcf_in->status;
		version = &bp->dcf_in->version;
		break;
	default:
		return NULL;
	}

	return ptp_ocp_core_version_at_least(version,
					     TIMECODE_VERSION_STATUS) ?
		status : NULL;
}

static ssize_t
timecode_error_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 __iomem *status;

	status = ptp_ocp_timecode_status_reg(bp, (uintptr_t)ea->var);
	if (!status)
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n",
			  !!(ioread32(status) & TIMECODE_STATUS_ERROR));
}

static ssize_t
timecode_error_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct dev_ext_attribute *ea = to_ext_attr(attr);
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 __iomem *status;
	bool clear;
	int err;

	status = ptp_ocp_timecode_status_reg(bp, (uintptr_t)ea->var);
	if (!status)
		return -EOPNOTSUPP;

	err = kstrtobool(buf, &clear);
	if (err)
		return err;
	if (!clear)
		return -EINVAL;

	spin_lock_irqsave(&bp->lock, flags);
	iowrite32(TIMECODE_STATUS_ERROR, status);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}

#define TIMECODE_ERROR_ATTR(_name, _index)				\
	struct dev_ext_attribute dev_attr_##_name = {			\
		__ATTR(_name, 0644, timecode_error_show,		\
		       timecode_error_store), (void *)_index		\
	}

/* These status bits are sticky; write 1 to the corresponding file to clear. */
static TIMECODE_ERROR_ATTR(irig_output_error, TIMECODE_ERROR_IRIG_OUTPUT);
static TIMECODE_ERROR_ATTR(irig_input_error, TIMECODE_ERROR_IRIG_INPUT);
static TIMECODE_ERROR_ATTR(dcf_output_error, TIMECODE_ERROR_DCF_OUTPUT);
static TIMECODE_ERROR_ATTR(dcf_input_error, TIMECODE_ERROR_DCF_INPUT);

static bool
ptp_ocp_irig_master_core(struct ptp_ocp *bp, u32 minimum)
{
	return bp->irig_out &&
		ptp_ocp_core_version_at_least(&bp->irig_out->version, minimum);
}

static bool
ptp_ocp_irig_master_feature(struct ptp_ocp *bp, u32 minimum)
{
	return bp->irig_out && ptp_ocp_irig_optional_features_enabled(bp) &&
		ptp_ocp_core_version_at_least(&bp->irig_out->version, minimum);
}

static bool
ptp_ocp_irig_slave_core(struct ptp_ocp *bp, u32 minimum)
{
	return bp->irig_in &&
		ptp_ocp_core_version_at_least(&bp->irig_in->version, minimum);
}

static bool
ptp_ocp_irig_slave_feature(struct ptp_ocp *bp, u32 minimum)
{
	return bp->irig_in && ptp_ocp_irig_optional_features_enabled(bp) &&
		ptp_ocp_core_version_at_least(&bp->irig_in->version, minimum);
}

/* Caller holds bp->lock.  Preserve reserved bits and the prior enable state. */
static int
ptp_ocp_irig_ctrl_update_locked(u32 __iomem *ctrl_reg, u32 mask, u32 value)
{
	u32 configured, ctrl, readback;

	ctrl = ioread32(ctrl_reg);
	configured = (ctrl & ~(IRIG_CTRL_ENABLE | mask)) | (value & mask);
	iowrite32(ctrl & ~IRIG_CTRL_ENABLE, ctrl_reg);
	iowrite32(configured, ctrl_reg);
	if (ctrl & IRIG_CTRL_ENABLE)
		iowrite32(configured | IRIG_CTRL_ENABLE, ctrl_reg);

	readback = ioread32(ctrl_reg);
	if ((readback & (IRIG_CTRL_ENABLE | mask)) ==
	    ((configured & mask) | (ctrl & IRIG_CTRL_ENABLE)))
		return 0;

	/* Best-effort rollback uses the same required disabled write sequence. */
	iowrite32(readback & ~IRIG_CTRL_ENABLE, ctrl_reg);
	iowrite32(ctrl & ~IRIG_CTRL_ENABLE, ctrl_reg);
	if (ctrl & IRIG_CTRL_ENABLE)
		iowrite32(ctrl, ctrl_reg);
	return -EIO;
}

static ssize_t
irig_output_mode_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 ctrl;

	if (!ptp_ocp_irig_master_core(bp,
				      IRIG_MASTER_VERSION_MODE_CONTROL))
		return -EOPNOTSUPP;
	ctrl = ioread32(&bp->irig_out->ctrl);
	return sysfs_emit(buf, "%u\n",
			  (ctrl & IRIG_CTRL_MODE_MASK) >> IRIG_CTRL_MODE_SHIFT);
}

static ssize_t
irig_output_mode_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u8 mode;
	int err;

	if (!ptp_ocp_irig_master_core(bp,
				      IRIG_MASTER_VERSION_MODE_CONTROL))
		return -EOPNOTSUPP;
	err = kstrtou8(buf, 0, &mode);
	if (err)
		return err;
	if (mode > 2)
		return -EINVAL;

	spin_lock_irqsave(&bp->lock, flags);
	err = ptp_ocp_irig_ctrl_update_locked(&bp->irig_out->ctrl,
		IRIG_CTRL_MODE_MASK, (u32)mode << IRIG_CTRL_MODE_SHIFT);
	spin_unlock_irqrestore(&bp->lock, flags);

	return err ? err : count;
}
static DEVICE_ATTR_RW(irig_output_mode);

static ssize_t
irig_input_mode_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 ctrl;

	if (!ptp_ocp_irig_slave_core(bp,
				     IRIG_SLAVE_VERSION_MODE_CONTROL))
		return -EOPNOTSUPP;
	ctrl = ioread32(&bp->irig_in->ctrl);
	return sysfs_emit(buf, "%u\n",
			  (ctrl & IRIG_CTRL_MODE_MASK) >> IRIG_CTRL_MODE_SHIFT);
}

static ssize_t
irig_input_mode_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u8 mode;
	int err;

	if (!ptp_ocp_irig_slave_core(bp,
				     IRIG_SLAVE_VERSION_MODE_CONTROL))
		return -EOPNOTSUPP;
	err = kstrtou8(buf, 0, &mode);
	if (err)
		return err;
	if (mode > 2)
		return -EINVAL;

	spin_lock_irqsave(&bp->lock, flags);
	err = ptp_ocp_irig_ctrl_update_locked(&bp->irig_in->ctrl,
		IRIG_CTRL_MODE_MASK, (u32)mode << IRIG_CTRL_MODE_SHIFT);
	spin_unlock_irqrestore(&bp->lock, flags);

	return err ? err : count;
}
static DEVICE_ATTR_RW(irig_input_mode);

static ssize_t
irig_output_control_bits_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 control_bits;

	if (!ptp_ocp_irig_master_feature(bp,
					 IRIG_MASTER_VERSION_MODE_CONTROL))
		return -EOPNOTSUPP;

	control_bits = ioread32(&bp->irig_out->control_bits);
	return sysfs_emit(buf, "0x%07x\n",
			  control_bits & IRIG_CONTROL_BITS_MASK);
}

static ssize_t
irig_output_control_bits_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 control_bits, ctrl, old_control_bits, readback;
	int err;

	if (!ptp_ocp_irig_master_feature(bp,
					 IRIG_MASTER_VERSION_MODE_CONTROL))
		return -EOPNOTSUPP;

	err = kstrtou32(buf, 0, &control_bits);
	if (err)
		return err;
	if (control_bits & ~IRIG_CONTROL_BITS_MASK)
		return -ERANGE;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->irig_out->ctrl);
	old_control_bits = ioread32(&bp->irig_out->control_bits);
	iowrite32(ctrl & ~IRIG_M_CTRL_ENABLE, &bp->irig_out->ctrl);
	iowrite32(control_bits, &bp->irig_out->control_bits);
	if (ctrl & IRIG_M_CTRL_ENABLE)
		iowrite32(ctrl, &bp->irig_out->ctrl);
	readback = ioread32(&bp->irig_out->control_bits);
	if ((readback & IRIG_CONTROL_BITS_MASK) != control_bits) {
		iowrite32(ctrl & ~IRIG_M_CTRL_ENABLE, &bp->irig_out->ctrl);
		iowrite32(old_control_bits, &bp->irig_out->control_bits);
		if (ctrl & IRIG_M_CTRL_ENABLE)
			iowrite32(ctrl, &bp->irig_out->ctrl);
		err = -EIO;
	}
	spin_unlock_irqrestore(&bp->lock, flags);

	return err ? err : count;
}
static DEVICE_ATTR_RW(irig_output_control_bits);

static ssize_t
irig_b_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val;

	if (!ptp_ocp_irig_master_core(bp,
				      IRIG_MASTER_VERSION_MODE_CONTROL))
		return -EOPNOTSUPP;

	val = ioread32(&bp->irig_out->ctrl);
	val = (val & IRIG_CTRL_CODE_MASK) >> IRIG_CTRL_CODE_SHIFT;
	return sysfs_emit(buf, "%u\n", val);
}

static ssize_t
irig_b_mode_store(struct device *dev,
		  struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	int err;
	u8 val;

	if (!ptp_ocp_irig_master_core(bp,
				      IRIG_MASTER_VERSION_MODE_CONTROL))
		return -EOPNOTSUPP;

	err = kstrtou8(buf, 0, &val);
	if (err)
		return err;
	if (val > 7)
		return -EINVAL;

	spin_lock_irqsave(&bp->lock, flags);
	err = ptp_ocp_irig_ctrl_update_locked(&bp->irig_out->ctrl,
		IRIG_CTRL_MODE_MASK | IRIG_CTRL_CODE_MASK,
		IRIG_CTRL_MODE_B | ((u32)val << IRIG_CTRL_CODE_SHIFT));
	spin_unlock_irqrestore(&bp->lock, flags);

	return err ? err : count;
}
static DEVICE_ATTR_RW(irig_b_mode);

static ssize_t
irig_output_am_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!ptp_ocp_irig_master_feature(bp, IRIG_MASTER_VERSION_AM))
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n",
			  !!(ioread32(&bp->irig_out->ctrl) & IRIG_CTRL_AM));
}

static ssize_t
irig_output_am_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	bool am;
	int err;

	if (!ptp_ocp_irig_master_feature(bp, IRIG_MASTER_VERSION_AM))
		return -EOPNOTSUPP;
	err = kstrtobool(buf, &am);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	err = ptp_ocp_irig_ctrl_update_locked(&bp->irig_out->ctrl,
		IRIG_CTRL_AM, am ? IRIG_CTRL_AM : 0);
	spin_unlock_irqrestore(&bp->lock, flags);

	return err ? err : count;
}
static DEVICE_ATTR_RW(irig_output_am);

static ssize_t
irig_input_code_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 ctrl;

	if (!ptp_ocp_irig_slave_feature(bp,
					IRIG_SLAVE_VERSION_CODE_YEAR))
		return -EOPNOTSUPP;
	ctrl = ioread32(&bp->irig_in->ctrl);
	return sysfs_emit(buf, "%u\n",
			  (ctrl & IRIG_CTRL_CODE_MASK) >> IRIG_CTRL_CODE_SHIFT);
}

static ssize_t
irig_input_code_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u8 code;
	int err;

	if (!ptp_ocp_irig_slave_feature(bp,
					IRIG_SLAVE_VERSION_CODE_YEAR))
		return -EOPNOTSUPP;
	err = kstrtou8(buf, 0, &code);
	if (err)
		return err;
	if (code > 7)
		return -EINVAL;

	spin_lock_irqsave(&bp->lock, flags);
	err = ptp_ocp_irig_ctrl_update_locked(&bp->irig_in->ctrl,
		IRIG_CTRL_CODE_MASK, (u32)code << IRIG_CTRL_CODE_SHIFT);
	spin_unlock_irqrestore(&bp->lock, flags);

	return err ? err : count;
}
static DEVICE_ATTR_RW(irig_input_code);

static ssize_t
irig_input_manual_year_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!ptp_ocp_irig_slave_feature(bp,
					IRIG_SLAVE_VERSION_CODE_YEAR))
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->irig_in->year) & IRIG_YEAR_MASK);
}

static ssize_t
irig_input_manual_year_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 configured, ctrl, old_year, readback, year;
	int err;

	if (!ptp_ocp_irig_slave_feature(bp,
					IRIG_SLAVE_VERSION_CODE_YEAR))
		return -EOPNOTSUPP;
	err = kstrtou32(buf, 0, &year);
	if (err)
		return err;
	if (year < IRIG_YEAR_MIN || year > IRIG_YEAR_MAX)
		return -ERANGE;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->irig_in->ctrl);
	old_year = ioread32(&bp->irig_in->year) & IRIG_YEAR_MASK;
	configured = ctrl & ~(IRIG_S_CTRL_ENABLE |
			      IRIG_SLAVE_CTRL_YEAR_VALID);
	iowrite32(configured, &bp->irig_in->ctrl);
	iowrite32(year, &bp->irig_in->year);
	readback = ioread32(&bp->irig_in->year) & IRIG_YEAR_MASK;
	if (readback != year) {
		err = -EIO;
		goto rollback;
	}

	/* YEAR_VALID is a self-clearing strobe that latches the new year. */
	iowrite32(configured | IRIG_SLAVE_CTRL_YEAR_VALID,
		  &bp->irig_in->ctrl);
	(void)ioread32(&bp->irig_in->ctrl); /* flush the strobe before restore */
	if (ctrl & IRIG_S_CTRL_ENABLE)
		iowrite32(configured | IRIG_S_CTRL_ENABLE,
			  &bp->irig_in->ctrl);
	else
		iowrite32(configured, &bp->irig_in->ctrl);
	readback = ioread32(&bp->irig_in->ctrl);
	if ((readback & IRIG_S_CTRL_ENABLE) ==
	    (ctrl & IRIG_S_CTRL_ENABLE)) {
		spin_unlock_irqrestore(&bp->lock, flags);
		return count;
	}

	err = -EIO;
rollback:
	iowrite32(configured, &bp->irig_in->ctrl);
	iowrite32(old_year, &bp->irig_in->year);
	iowrite32(configured | IRIG_SLAVE_CTRL_YEAR_VALID,
		  &bp->irig_in->ctrl);
	(void)ioread32(&bp->irig_in->ctrl);
	iowrite32(ctrl & ~IRIG_SLAVE_CTRL_YEAR_VALID, &bp->irig_in->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);
	return err;
}
static DEVICE_ATTR_RW(irig_input_manual_year);

static ssize_t
irig_input_am_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!ptp_ocp_irig_slave_feature(bp, IRIG_SLAVE_VERSION_AM))
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n",
			  !!(ioread32(&bp->irig_in->ctrl) & IRIG_CTRL_AM));
}

static ssize_t
irig_input_am_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	bool am;
	int err;

	if (!ptp_ocp_irig_slave_feature(bp, IRIG_SLAVE_VERSION_AM))
		return -EOPNOTSUPP;
	err = kstrtobool(buf, &am);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	err = ptp_ocp_irig_ctrl_update_locked(&bp->irig_in->ctrl,
		IRIG_CTRL_AM, am ? IRIG_CTRL_AM : 0);
	spin_unlock_irqrestore(&bp->lock, flags);

	return err ? err : count;
}
static DEVICE_ATTR_RW(irig_input_am);

static ssize_t
optional_image_contract_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 actual, expected = 0;
	bool targeted;

	if (!bp->image)
		return -EOPNOTSUPP;
	actual = ioread32(&bp->image->version);
	targeted = ptp_ocp_optional_image_contract_expected(bp, &expected);

	return sysfs_emit(buf,
		"pci=%s actual=0x%08x expected=0x%08x targeted=%u match=%u loader=%u\n",
		pci_name(bp->pdev), actual, expected, targeted ? 1U : 0U,
		ptp_ocp_optional_image_contract_matches(bp) ? 1U : 0U,
		bp->fw_loader ? 1U : 0U);
}
static DEVICE_ATTR_RO(optional_image_contract);

static ssize_t
clock_source_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	const char *p;
	u32 select;

	select = ioread32(&bp->reg->select);
	p = ptp_ocp_select_name_from_val(ptp_ocp_clock, select >> 16);

	return sysfs_emit(buf, "%s\n", p);
}

static ssize_t
clock_source_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	int val;

	val = ptp_ocp_select_val_from_name(ptp_ocp_clock, buf);
	if (val < 0)
		return val;

	spin_lock_irqsave(&bp->lock, flags);
	iowrite32(val, &bp->reg->select);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(clock_source);

static ssize_t
available_clock_sources_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return ptp_ocp_select_table_show(ptp_ocp_clock, buf);
}
static DEVICE_ATTR_RO(available_clock_sources);

static ssize_t
clock_status_drift_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val;
	int res;

	if (!ptp_ocp_clock_optional_registers_enabled(bp))
		return -EOPNOTSUPP;

	val = ioread32(&bp->reg->status_drift);
	res = (val & ~INT_MAX) ? -1 : 1;
	res *= (val & INT_MAX);
	return sysfs_emit(buf, "%d\n", res);
}
static DEVICE_ATTR_RO(clock_status_drift);

static ssize_t
clock_status_offset_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val;
	int res;

	if (!ptp_ocp_clock_optional_registers_enabled(bp))
		return -EOPNOTSUPP;

	val = ioread32(&bp->reg->status_offset);
	res = (val & ~INT_MAX) ? -1 : 1;
	res *= (val & INT_MAX);
	return sysfs_emit(buf, "%d\n", res);
}
static DEVICE_ATTR_RO(clock_status_offset);

static int
ptp_ocp_nmea_require_version(struct ptp_ocp *bp, u32 minimum,
			     u32 *version)
{
	u32 value;

	if (!bp->nmea_out)
		return -EOPNOTSUPP;
	value = ioread32(&bp->nmea_out->version);
	if (!value || value == U32_MAX || value < minimum)
		return -EOPNOTSUPP;
	if (version)
		*version = value;
	return 0;
}

static u32
ptp_ocp_nmea_message_mask(u32 version)
{
	u32 mask = TOD_MASTER_DISABLE_ZDA;

	if (version >= TOD_MASTER_VERSION_RMC)
		mask |= TOD_MASTER_DISABLE_RMC;
	if (version >= TOD_MASTER_VERSION_UTC)
		mask |= TOD_MASTER_DISABLE_UTC;
	return mask;
}

static int
ptp_ocp_nmea_ctrl_update_locked(struct ptp_ocp *bp, u32 mask, u32 value)
{
	u32 configured, ctrl, expected;

	ctrl = ioread32(&bp->nmea_out->ctrl);
	configured = (ctrl & ~(TOD_MASTER_CTRL_ENABLE | mask)) |
		     (value & mask);
	expected = configured | (ctrl & TOD_MASTER_CTRL_ENABLE);
	iowrite32(ctrl & ~TOD_MASTER_CTRL_ENABLE, &bp->nmea_out->ctrl);
	iowrite32(configured, &bp->nmea_out->ctrl);
	if (ctrl & TOD_MASTER_CTRL_ENABLE)
		iowrite32(expected, &bp->nmea_out->ctrl);
	ctrl = ioread32(&bp->nmea_out->ctrl);
	return (ctrl & (mask | TOD_MASTER_CTRL_ENABLE)) ==
	       (expected & (mask | TOD_MASTER_CTRL_ENABLE)) ? 0 : -EIO;
}

static ssize_t
nmea_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL);
	if (err)
		return err;
	return sysfs_emit(buf, "%u\n",
			  !!(ioread32(&bp->nmea_out->ctrl) &
			     TOD_MASTER_CTRL_ENABLE));
}

static ssize_t
nmea_enable_store(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	bool enable;
	u32 ctrl, readback;
	int err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL);
	if (err)
		return err;
	err = kstrtobool(buf, &enable);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->nmea_out->ctrl);
	ctrl = enable ? ctrl | TOD_MASTER_CTRL_ENABLE :
			ctrl & ~TOD_MASTER_CTRL_ENABLE;
	iowrite32(ctrl, &bp->nmea_out->ctrl);
	readback = ioread32(&bp->nmea_out->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);
	if (!!(readback & TOD_MASTER_CTRL_ENABLE) != enable)
		return -EIO;
	return count;
}
static DEVICE_ATTR_RW(nmea_enable);

static ssize_t
nmea_uart_polarity_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_nmea_require_version(bp,
		TOD_MASTER_VERSION_POLARITY, NULL);
	if (err)
		return err;
	/* Raw FPGA convention: one is normal, zero is inverted. */
	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->nmea_out->uart_polarity) & 1U);
}

static ssize_t
nmea_uart_polarity_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	bool polarity;
	u32 ctrl, readback_ctrl, readback_value, value;
	int err;

	err = ptp_ocp_nmea_require_version(bp,
		TOD_MASTER_VERSION_POLARITY, NULL);
	if (err)
		return err;
	err = kstrtobool(buf, &polarity);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->nmea_out->ctrl);
	iowrite32(ctrl & ~TOD_MASTER_CTRL_ENABLE, &bp->nmea_out->ctrl);
	value = ioread32(&bp->nmea_out->uart_polarity);
	value = (value & ~1U) | polarity;
	iowrite32(value, &bp->nmea_out->uart_polarity);
	if (ctrl & TOD_MASTER_CTRL_ENABLE)
		iowrite32(ctrl, &bp->nmea_out->ctrl);
	readback_value = ioread32(&bp->nmea_out->uart_polarity);
	readback_ctrl = ioread32(&bp->nmea_out->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);
	if ((readback_value & 1U) != polarity ||
	    (readback_ctrl & TOD_MASTER_CTRL_ENABLE) !=
	    (ctrl & TOD_MASTER_CTRL_ENABLE))
		return -EIO;
	return count;
}
static DEVICE_ATTR_RW(nmea_uart_polarity);

static ssize_t
nmea_baud_rate_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	const char *name;
	u32 selector;
	int err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL);
	if (err)
		return err;
	selector = ioread32(&bp->nmea_out->uart_baud) & TOD_UART_BAUD_MASK;
	name = ptp_ocp_select_name_from_val(ptp_ocp_tod_baud_rates, selector);
	return sysfs_emit(buf, "%s\n", name ? name : "UNKNOWN");
}

static ssize_t
nmea_baud_rate_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 ctrl, readback_ctrl, readback_value, value;
	int err, selector;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL);
	if (err)
		return err;
	selector = ptp_ocp_select_val_from_name(ptp_ocp_tod_baud_rates, buf);
	if (selector < 0)
		return selector;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->nmea_out->ctrl);
	iowrite32(ctrl & ~TOD_MASTER_CTRL_ENABLE, &bp->nmea_out->ctrl);
	value = ioread32(&bp->nmea_out->uart_baud);
	value = (value & ~TOD_UART_BAUD_MASK) |
		((u32)selector & TOD_UART_BAUD_MASK);
	iowrite32(value, &bp->nmea_out->uart_baud);
	if (ctrl & TOD_MASTER_CTRL_ENABLE)
		iowrite32(ctrl, &bp->nmea_out->ctrl);
	readback_value = ioread32(&bp->nmea_out->uart_baud);
	readback_ctrl = ioread32(&bp->nmea_out->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);
	if ((readback_value & TOD_UART_BAUD_MASK) != (u32)selector ||
	    (readback_ctrl & TOD_MASTER_CTRL_ENABLE) !=
	    (ctrl & TOD_MASTER_CTRL_ENABLE))
		return -EIO;
	return count;
}
static DEVICE_ATTR_RW(nmea_baud_rate);

static ssize_t
available_nmea_baud_rates_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL);
	if (err)
		return err;
	return ptp_ocp_select_table_show(ptp_ocp_tod_baud_rates, buf);
}
static DEVICE_ATTR_RO(available_nmea_baud_rates);

static ssize_t
nmea_errors_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	int err;

	err = ptp_ocp_nmea_require_version(bp,
		TOD_MASTER_VERSION_STATUS, NULL);
	if (err)
		return err;
	return sysfs_emit(buf, "0x%x\n",
			  ioread32(&bp->nmea_out->status) &
			  TOD_MASTER_STATUS_ERROR);
}

static ssize_t
nmea_errors_store(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 errors;
	int err;

	err = ptp_ocp_nmea_require_version(bp,
		TOD_MASTER_VERSION_STATUS, NULL);
	if (err)
		return err;
	err = kstrtou32(buf, 0, &errors);
	if (err)
		return err;
	if (errors != TOD_MASTER_STATUS_ERROR)
		return -EINVAL;

	spin_lock_irqsave(&bp->lock, flags);
	iowrite32(errors, &bp->nmea_out->status);
	spin_unlock_irqrestore(&bp->lock, flags);
	return count;
}
static DEVICE_ATTR_RW(nmea_errors);

static ssize_t
nmea_correction_seconds_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 value;
	int correction, err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL);
	if (err)
		return err;
	value = ioread32(&bp->nmea_out->adj_sec);
	correction = value & INT_MAX;
	if (value & BIT(31))
		correction = -correction;
	return sysfs_emit(buf, "%d\n", correction);
}

static ssize_t
nmea_correction_seconds_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 ctrl, encoded, magnitude, readback_ctrl, readback_value;
	int correction, err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL);
	if (err)
		return err;
	err = kstrtoint(buf, 0, &correction);
	if (err)
		return err;
	if (correction == INT_MIN)
		return -ERANGE;
	magnitude = correction < 0 ? -correction : correction;
	encoded = magnitude | (correction < 0 ? BIT(31) : 0);

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->nmea_out->ctrl);
	iowrite32(ctrl & ~TOD_MASTER_CTRL_ENABLE, &bp->nmea_out->ctrl);
	iowrite32(encoded, &bp->nmea_out->adj_sec);
	if (ctrl & TOD_MASTER_CTRL_ENABLE)
		iowrite32(ctrl, &bp->nmea_out->ctrl);
	readback_value = ioread32(&bp->nmea_out->adj_sec);
	readback_ctrl = ioread32(&bp->nmea_out->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);
	if (readback_value != encoded ||
	    (readback_ctrl & TOD_MASTER_CTRL_ENABLE) !=
	    (ctrl & TOD_MASTER_CTRL_ENABLE))
		return -EIO;
	return count;
}
static DEVICE_ATTR_RW(nmea_correction_seconds);

static ssize_t
nmea_local_offset_minutes_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 hours, minutes, value;
	int result;

	if (ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL))
		return -EOPNOTSUPP;
	value = ioread32(&bp->nmea_out->local_offset);
	hours = (value & TOD_MASTER_LOCAL_HOUR_MASK) >>
		TOD_MASTER_LOCAL_HOUR_SHIFT;
	minutes = value & TOD_MASTER_LOCAL_MINUTE_MASK;
	if (hours > 13 || minutes > 59)
		return -EIO;
	result = hours * 60 + minutes;
	if (value & TOD_MASTER_LOCAL_SIGN)
		result = -result;
	return sysfs_emit(buf, "%d\n", result);
}

static ssize_t
nmea_local_offset_minutes_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 ctrl, magnitude, readback_ctrl, readback_value, value;
	int minutes, err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), NULL);
	if (err)
		return err;
	err = kstrtoint(buf, 0, &minutes);
	if (err)
		return err;
	if (minutes < -TOD_MASTER_LOCAL_MAX_MINUTES ||
	    minutes > TOD_MASTER_LOCAL_MAX_MINUTES)
		return -ERANGE;
	magnitude = minutes < 0 ? -minutes : minutes;
	value = ((magnitude / 60) << TOD_MASTER_LOCAL_HOUR_SHIFT) |
		(magnitude % 60) |
		(minutes < 0 ? TOD_MASTER_LOCAL_SIGN : 0);
	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->nmea_out->ctrl);
	iowrite32(ctrl & ~TOD_MASTER_CTRL_ENABLE, &bp->nmea_out->ctrl);
	iowrite32(value, &bp->nmea_out->local_offset);
	if (ctrl & TOD_MASTER_CTRL_ENABLE)
		iowrite32(ctrl, &bp->nmea_out->ctrl);
	readback_value = ioread32(&bp->nmea_out->local_offset);
	readback_ctrl = ioread32(&bp->nmea_out->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);
	if (readback_value != value ||
	    (readback_ctrl & TOD_MASTER_CTRL_ENABLE) !=
	    (ctrl & TOD_MASTER_CTRL_ENABLE))
		return -EIO;
	return count;
}
static DEVICE_ATTR_RW(nmea_local_offset_minutes);

static ssize_t
nmea_gnss_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	const char *name;
	u32 value;

	if (ptp_ocp_nmea_require_version(bp, TOD_MASTER_VERSION_GNSS, NULL))
		return -EOPNOTSUPP;
	value = (ioread32(&bp->nmea_out->ctrl) & TOD_MASTER_CTRL_GNSS_MASK) >>
		TOD_MASTER_CTRL_GNSS_SHIFT;
	name = ptp_ocp_select_name_from_val(ptp_ocp_nmea_gnss, value);
	return sysfs_emit(buf, "%s\n", name ? name : "UNKNOWN");
}

static ssize_t
nmea_gnss_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	int err, value;

	err = ptp_ocp_nmea_require_version(bp, TOD_MASTER_VERSION_GNSS, NULL);
	if (err)
		return err;
	value = ptp_ocp_select_val_from_name(ptp_ocp_nmea_gnss, buf);
	if (value < 0)
		return value;
	spin_lock_irqsave(&bp->lock, flags);
	err = ptp_ocp_nmea_ctrl_update_locked(bp,
		TOD_MASTER_CTRL_GNSS_MASK,
		(u32)value << TOD_MASTER_CTRL_GNSS_SHIFT);
	spin_unlock_irqrestore(&bp->lock, flags);
	return err ? err : count;
}
static DEVICE_ATTR_RW(nmea_gnss);

static ssize_t
available_nmea_gnss_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (ptp_ocp_nmea_require_version(bp, TOD_MASTER_VERSION_GNSS, NULL))
		return -EOPNOTSUPP;
	return ptp_ocp_select_table_show(ptp_ocp_nmea_gnss, buf);
}
static DEVICE_ATTR_RO(available_nmea_gnss);

static ssize_t
nmea_message_disable_mask_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 ctrl, mask, version;
	int err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), &version);
	if (err)
		return err;
	mask = ptp_ocp_nmea_message_mask(version);
	ctrl = ioread32(&bp->nmea_out->ctrl);
	return sysfs_emit(buf, "0x%02x\n",
		(ctrl >> TOD_MASTER_CTRL_MESSAGE_SHIFT) & mask);
}

static ssize_t
nmea_message_disable_mask_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 mask, register_mask, version;
	u8 value;
	int err;

	err = ptp_ocp_nmea_require_version(bp, FPGA_CORE_VERSION(1, 0), &version);
	if (err)
		return err;
	err = kstrtou8(buf, 0, &value);
	if (err)
		return err;
	mask = ptp_ocp_nmea_message_mask(version);
	if (value & ~mask)
		return -EOPNOTSUPP;
	register_mask = mask << TOD_MASTER_CTRL_MESSAGE_SHIFT;
	spin_lock_irqsave(&bp->lock, flags);
	err = ptp_ocp_nmea_ctrl_update_locked(bp, register_mask,
		(u32)value << TOD_MASTER_CTRL_MESSAGE_SHIFT);
	spin_unlock_irqrestore(&bp->lock, flags);
	return err ? err : count;
}
static DEVICE_ATTR_RW(nmea_message_disable_mask);

static bool
ptp_ocp_tod_protocol_supported(u32 version, int protocol)
{
	if (!version || version == U32_MAX)
		return false;

	switch (protocol) {
	case 0: /* NMEA */
		return true;
	case 1: /* UBX */
		return version >= TOD_VERSION_UBX;
	case 2: /* TSIP */
		return version >= TOD_VERSION_TSIP;
	case 3: /* ESIP */
		return version >= TOD_VERSION_ESIP;
	case 4: /* PFEC */
		return version >= TOD_VERSION_PFEC;
	default:
		return false;
	}
}

static void
ptp_ocp_tod_ctrl_update_locked(struct ptp_ocp *bp, u32 mask, u32 value)
{
	u32 configured, ctrl;

	ctrl = ioread32(&bp->tod->ctrl);
	configured = (ctrl & ~(TOD_CTRL_ENABLE | mask)) | (value & mask);

	/* Configuration fields may only change while the core is disabled. */
	iowrite32(ctrl & ~TOD_CTRL_ENABLE, &bp->tod->ctrl);
	iowrite32(configured, &bp->tod->ctrl);
	if (ctrl & TOD_CTRL_ENABLE)
		iowrite32(configured | TOD_CTRL_ENABLE, &bp->tod->ctrl);
}

static int ptp_ocp_tod_message_mask(u32 version, u32 ctrl,
				    u32 *register_mask);

static ssize_t
tod_protocol_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	const char *p;
	u32 select;

	if (!bp->tod)
		return -EOPNOTSUPP;

	select = ioread32(&bp->tod->ctrl);
	select = (select >> TOD_CTRL_PROTOCOL_SHIFT) & TOD_CTRL_PROTOCOL_MASK;
	p = ptp_ocp_select_name_from_val(ptp_ocp_tod_protocol, select);

	return sysfs_emit(buf, "%s\n", p ? p : "UNKNOWN");
}

static ssize_t
tod_protocol_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 ctrl, message_mask, protocol_mask, version;
	int val;

	if (!bp->tod)
		return -EOPNOTSUPP;

	val = ptp_ocp_select_val_from_name(ptp_ocp_tod_protocol, buf);
	if (val < 0)
		return val;
	version = ioread32(&bp->tod->version);
	if (!ptp_ocp_tod_protocol_supported(version, val))
		return -EOPNOTSUPP;

	protocol_mask = TOD_CTRL_PROTOCOL_MASK << TOD_CTRL_PROTOCOL_SHIFT;
	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->tod->ctrl);
	ctrl = (ctrl & ~protocol_mask) |
		((u32)val << TOD_CTRL_PROTOCOL_SHIFT);
	if (ptp_ocp_tod_message_mask(version, ctrl, &message_mask)) {
		spin_unlock_irqrestore(&bp->lock, flags);
		return -EOPNOTSUPP;
	}
	/* Do not carry message-disable bits that the new parser cannot use. */
	ptp_ocp_tod_ctrl_update_locked(bp,
		protocol_mask | TOD_CTRL_DISABLE_MESSAGE_MASK,
		(ctrl & protocol_mask) | (ctrl & message_mask));
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(tod_protocol);

static ssize_t
available_tod_protocols_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	ssize_t count = 0;
	u32 version;
	int i;

	if (!bp->tod)
		return -EOPNOTSUPP;
	version = ioread32(&bp->tod->version);

	for (i = 0; ptp_ocp_tod_protocol[i].name; i++) {
		if (!ptp_ocp_tod_protocol_supported(
				version, ptp_ocp_tod_protocol[i].value))
			continue;
		count += sysfs_emit_at(buf, count, "%s ",
				       ptp_ocp_tod_protocol[i].name);
	}
	if (count)
		count--;
	count += sysfs_emit_at(buf, count, "\n");
	return count;
}
static DEVICE_ATTR_RO(available_tod_protocols);

static ssize_t
tod_gnss_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	const char *name;
	u32 select;

	if (!bp->tod ||
	    !ptp_ocp_core_version_at_least(&bp->tod->version,
					    TOD_VERSION_GNSS_SELECT))
		return -EOPNOTSUPP;

	select = ioread32(&bp->tod->ctrl);
	select = (select >> TOD_CTRL_GNSS_SHIFT) & TOD_CTRL_GNSS_MASK;
	name = ptp_ocp_select_name_from_val(ptp_ocp_tod_gnss, select);

	return sysfs_emit(buf, "%s\n", name ? name : "UNKNOWN");
}

static ssize_t
tod_gnss_store(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	int select;

	if (!bp->tod ||
	    !ptp_ocp_core_version_at_least(&bp->tod->version,
					    TOD_VERSION_GNSS_SELECT))
		return -EOPNOTSUPP;

	select = ptp_ocp_select_val_from_name(ptp_ocp_tod_gnss, buf);
	if (select < 0)
		return select;

	spin_lock_irqsave(&bp->lock, flags);
	ptp_ocp_tod_ctrl_update_locked(bp,
		TOD_CTRL_GNSS_MASK << TOD_CTRL_GNSS_SHIFT,
		(u32)select << TOD_CTRL_GNSS_SHIFT);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(tod_gnss);

static ssize_t
available_tod_gnss_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!bp->tod ||
	    !ptp_ocp_core_version_at_least(&bp->tod->version,
					    TOD_VERSION_GNSS_SELECT))
		return -EOPNOTSUPP;

	return ptp_ocp_select_table_show(ptp_ocp_tod_gnss, buf);
}
static DEVICE_ATTR_RO(available_tod_gnss);

static int
ptp_ocp_tod_message_mask(u32 version, u32 ctrl, u32 *register_mask)
{
	u32 mask, protocol;

	protocol = (ctrl >> TOD_CTRL_PROTOCOL_SHIFT) & TOD_CTRL_PROTOCOL_MASK;
	if (!ptp_ocp_tod_protocol_supported(version, protocol))
		return -EOPNOTSUPP;

	switch (protocol) {
	case 0: /* NMEA: GxGNS/status message arrived in version 2.0. */
		mask = version >= TOD_VERSION_NMEA_STATUS_MESSAGE ?
			TOD_MESSAGE_NMEA_FULL_MASK : TOD_MESSAGE_NMEA_BASE_MASK;
		break;
	case 1: /* UBX GNSS telemetry messages arrived in version 1.7. */
		mask = version >= TOD_VERSION_GNSS_TELEMETRY ?
			TOD_MESSAGE_UBX_FULL_MASK : TOD_MESSAGE_UBX_BASE_MASK;
		break;
	case 2:
		mask = TOD_MESSAGE_TSIP_MASK;
		break;
	case 3:
		mask = TOD_MESSAGE_ESIP_MASK;
		break;
	case 4:
		mask = TOD_MESSAGE_PFEC_MASK;
		break;
	default:
		return -EOPNOTSUPP;
	}

	*register_mask = mask << TOD_CTRL_DISABLE_MESSAGE_SHIFT;
	return 0;
}

static ssize_t
tod_message_disable_mask_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 ctrl, mask, version;
	int err;

	if (!bp->tod)
		return -EOPNOTSUPP;

	version = ioread32(&bp->tod->version);
	ctrl = ioread32(&bp->tod->ctrl);
	err = ptp_ocp_tod_message_mask(version, ctrl, &mask);
	if (err)
		return err;

	return sysfs_emit(buf, "0x%02x\n",
			  (ctrl & mask) >> TOD_CTRL_DISABLE_MESSAGE_SHIFT);
}

static ssize_t
tod_message_disable_mask_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 ctrl, mask, version;
	u8 disable_mask;
	int err;

	if (!bp->tod)
		return -EOPNOTSUPP;

	err = kstrtou8(buf, 0, &disable_mask);
	if (err)
		return err;
	version = ioread32(&bp->tod->version);

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->tod->ctrl);
	err = ptp_ocp_tod_message_mask(version, ctrl, &mask);
	if (err) {
		spin_unlock_irqrestore(&bp->lock, flags);
		return err;
	}
	if (disable_mask & ~(mask >> TOD_CTRL_DISABLE_MESSAGE_SHIFT)) {
		spin_unlock_irqrestore(&bp->lock, flags);
		return -EOPNOTSUPP;
	}
	ptp_ocp_tod_ctrl_update_locked(
		bp, TOD_CTRL_DISABLE_MESSAGE_MASK,
		(u32)disable_mask << TOD_CTRL_DISABLE_MESSAGE_SHIFT);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(tod_message_disable_mask);

static ssize_t
tod_uart_polarity_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	if (!bp->tod ||
	    !ptp_ocp_core_version_at_least(&bp->tod->version,
					    TOD_VERSION_POLARITY))
		return -EOPNOTSUPP;

	return sysfs_emit(buf, "%u\n",
			  ioread32(&bp->tod->uart_polarity) & 1U);
}

static ssize_t
tod_uart_polarity_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	bool polarity;
	u32 ctrl;
	int err;

	if (!bp->tod ||
	    !ptp_ocp_core_version_at_least(&bp->tod->version,
					    TOD_VERSION_POLARITY))
		return -EOPNOTSUPP;

	err = kstrtobool(buf, &polarity);
	if (err)
		return err;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->tod->ctrl);
	iowrite32(ctrl & ~TOD_CTRL_ENABLE, &bp->tod->ctrl);
	iowrite32(polarity, &bp->tod->uart_polarity);
	if (ctrl & TOD_CTRL_ENABLE)
		iowrite32(ctrl, &bp->tod->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(tod_uart_polarity);

static u32
ptp_ocp_tod_error_mask(u32 version)
{
	return version >= TOD_VERSION_ALL_ERRORS ? TOD_STATUS_ERROR_MASK :
		TOD_STATUS_PARSE_ERROR;
}

static ssize_t
tod_errors_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 mask, version;

	if (!bp->tod)
		return -EOPNOTSUPP;
	version = ioread32(&bp->tod->version);
	if (!version || version == U32_MAX || version < TOD_VERSION_STATUS)
		return -EOPNOTSUPP;
	mask = ptp_ocp_tod_error_mask(version);

	return sysfs_emit(buf, "0x%x\n", ioread32(&bp->tod->status) & mask);
}

static ssize_t
tod_errors_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 errors, mask, version;
	int err;

	if (!bp->tod)
		return -EOPNOTSUPP;
	version = ioread32(&bp->tod->version);
	if (!version || version == U32_MAX || version < TOD_VERSION_STATUS)
		return -EOPNOTSUPP;

	err = kstrtou32(buf, 0, &errors);
	if (err)
		return err;
	mask = ptp_ocp_tod_error_mask(version);
	if (!errors || (errors & ~mask))
		return -EINVAL;

	/* The requested sticky status bits are write-one-to-clear. */
	spin_lock_irqsave(&bp->lock, flags);
	iowrite32(errors, &bp->tod->status);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(tod_errors);

static ssize_t
tod_baud_rate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	const char *p;
	u32 select;

	select = ioread32(&bp->tod->uart_baud);
	select = (select >> TOD_UART_BAUD_SHIFT) & TOD_UART_BAUD_MASK;
	p = ptp_ocp_select_name_from_val(ptp_ocp_tod_baud_rates, select);

	return sysfs_emit(buf, "%s\n", p);
}

static ssize_t
tod_baud_rate_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	u32 uart_baud_reg, ctrl_reg, enable;
	int val;

	val = ptp_ocp_select_val_from_name(ptp_ocp_tod_baud_rates, buf);
	if (val < 0)
		return val;

	spin_lock_irqsave(&bp->lock, flags);
	ctrl_reg = ioread32(&bp->tod->ctrl);
	enable = ctrl_reg & TOD_CTRL_ENABLE;
	ctrl_reg &= ~TOD_CTRL_ENABLE;
	iowrite32(ctrl_reg, &bp->tod->ctrl);

	uart_baud_reg = ioread32(&bp->tod->uart_baud);
	uart_baud_reg &= ~(TOD_UART_BAUD_MASK << TOD_UART_BAUD_SHIFT);
	uart_baud_reg |= (val & TOD_UART_BAUD_MASK) << TOD_UART_BAUD_SHIFT;
	iowrite32(uart_baud_reg, &bp->tod->uart_baud);
	if (enable)
		iowrite32(ctrl_reg | TOD_CTRL_ENABLE, &bp->tod->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(tod_baud_rate);

static ssize_t
available_tod_baud_rates_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return ptp_ocp_select_table_show(ptp_ocp_tod_baud_rates, buf);
}
static DEVICE_ATTR_RO(available_tod_baud_rates);

static ssize_t
tod_correction_show(struct device *dev,
		    struct device_attribute *attr, char *buf)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	u32 val;
	int res;

	val = ioread32(&bp->tod->adj_sec);
	res = (val & ~INT_MAX) ? -1 : 1;
	res *= (val & INT_MAX);
	return sysfs_emit(buf, "%d\n", res);
}

static ssize_t
tod_correction_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);
	unsigned long flags;
	s32 correction;
	u32 ctrl, magnitude, val;
	int err;

	err = kstrtos32(buf, 0, &correction);
	if (err)
		return err;
	if (correction == INT_MIN)
		return -ERANGE;
	magnitude = correction < 0 ? -correction : correction;
	val = magnitude | (correction < 0 ? BIT(31) : 0);

	spin_lock_irqsave(&bp->lock, flags);
	ctrl = ioread32(&bp->tod->ctrl);
	if (ctrl & TOD_CTRL_ENABLE)
		iowrite32(ctrl & ~TOD_CTRL_ENABLE, &bp->tod->ctrl);
	iowrite32(val, &bp->tod->adj_sec);
	if (ctrl & TOD_CTRL_ENABLE)
		iowrite32(ctrl, &bp->tod->ctrl);
	spin_unlock_irqrestore(&bp->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(tod_correction);

#define _DEVICE_SIGNAL_GROUP_ATTRS(_nr)					\
	static struct attribute *fb_timecard_signal##_nr##_attrs[] = {	\
		&dev_attr_signal##_nr##_signal.attr.attr,		\
		&dev_attr_signal##_nr##_duty.attr.attr,			\
		&dev_attr_signal##_nr##_phase.attr.attr,		\
		&dev_attr_signal##_nr##_period.attr.attr,		\
		&dev_attr_signal##_nr##_polarity.attr.attr,		\
		&dev_attr_signal##_nr##_repeat_count.attr.attr,		\
		&dev_attr_signal##_nr##_cable_delay.attr.attr,		\
		&dev_attr_signal##_nr##_running.attr.attr,		\
		&dev_attr_signal##_nr##_start.attr.attr,		\
		NULL,							\
	}

#define DEVICE_SIGNAL_GROUP(_name, _nr)					\
	_DEVICE_SIGNAL_GROUP_ATTRS(_nr);				\
	static const struct attribute_group				\
			fb_timecard_signal##_nr##_group = {		\
		.name = #_name,						\
		.attrs = fb_timecard_signal##_nr##_attrs,		\
}

DEVICE_SIGNAL_GROUP(gen1, 0);
DEVICE_SIGNAL_GROUP(gen2, 1);
DEVICE_SIGNAL_GROUP(gen3, 2);
DEVICE_SIGNAL_GROUP(gen4, 3);

#define _DEVICE_FREQ_GROUP_ATTRS(_nr)					\
	static struct attribute *fb_timecard_freq##_nr##_attrs[] = {	\
		&dev_attr_freq##_nr##_seconds.attr.attr,		\
		&dev_attr_freq##_nr##_frequency.attr.attr,		\
		NULL,							\
	}

#define DEVICE_FREQ_GROUP(_name, _nr)					\
	_DEVICE_FREQ_GROUP_ATTRS(_nr);					\
	static const struct attribute_group				\
			fb_timecard_freq##_nr##_group = {		\
		.name = #_name,						\
		.attrs = fb_timecard_freq##_nr##_attrs,			\
}

DEVICE_FREQ_GROUP(freq1, 0);
DEVICE_FREQ_GROUP(freq2, 1);
DEVICE_FREQ_GROUP(freq3, 2);
DEVICE_FREQ_GROUP(freq4, 3);

static int
config_flash_rw(struct ptp_ocp *bp, bool write, char *buf,
		loff_t off, size_t count)
{
	struct erase_info erase;
	struct mtd_info *mtd;
	size_t base, blksz;
	struct device *dev;
	size_t len, rv;
	int err;

	base = bp->config_start;
	blksz = 4096;

	if (off % blksz)
		return -EINVAL;

	dev = ptp_ocp_find_flash(bp);
	if (!dev) {
		dev_err(&bp->pdev->dev, "Can't find Flash SPI adapter\n");
		return -ENODEV;
	}
	mtd = dev_get_drvdata(dev);

	while (count) {
		len = min_t(size_t, count, blksz);

		if (write) {
			erase.addr = base + off;
			erase.len = blksz;

			err = mtd_erase(mtd, &erase);
			if (err)
				goto out;

			err = mtd_write(mtd, base + off, len, &rv, buf + off);
		} else {
			err = mtd_read(mtd, base + off, len, &rv, buf + off);
		}

		if (err)
			goto out;

		off += blksz;
		count -= len;
	}

out:
	put_device(dev);
	return err;
}

static ssize_t
config_read(struct file *filp, struct kobject *kobj,
	    OCP_BIN_ATTR_CONST struct bin_attribute *bin_attr, char *buf,
	    loff_t off, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(kobj_to_dev(kobj));
	size_t size = OCP_CONFIG_SIZE;
	int err;

	if (off > size)
		return 0;

	if (off + count > size)
		count = size - off;

	err = config_flash_rw(bp, false, buf, off, count);
	if (err)
		return err;

	return count;
}

static ssize_t
config_write(struct file *filp, struct kobject *kobj,
	     OCP_BIN_ATTR_CONST struct bin_attribute *bin_attr, char *buf,
	     loff_t off, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(kobj_to_dev(kobj));
	size_t size = OCP_CONFIG_SIZE;
	int err;

	if (off > size)
		return 0;

	if (off + count > size)
		count = size - off;

	err = config_flash_rw(bp, true, buf, off, count);
	if (err)
		return err;

	return count;
}
static BIN_ATTR_RW(config, OCP_CONFIG_SIZE);

static ssize_t
disciplining_config_read(struct file *filp, struct kobject *kobj,
	    OCP_BIN_ATTR_CONST struct bin_attribute *bin_attr, char *buf,
	    loff_t off, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(kobj_to_dev(kobj));
	size_t size = OCP_ART_CONFIG_SIZE;
	struct nvmem_device *nvmem;
	ssize_t err;

	nvmem = ptp_ocp_nvmem_device_get(bp, NULL);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	if (off > size) {
		err = 0;
		goto out;
	}

	if (off + count > size)
		count = size - off;

	// the configuration is in the very beginning of the EEPROM
	err = nvmem_device_read(nvmem, off, count, buf);
	if (err != count) {
		err = -EFAULT;
		goto out;
	}

out:
	ptp_ocp_nvmem_device_put(&nvmem);

	return err;
}

static ssize_t
disciplining_config_write(struct file *filp, struct kobject *kobj,
	     OCP_BIN_ATTR_CONST struct bin_attribute *bin_attr, char *buf,
	     loff_t off, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(kobj_to_dev(kobj));
	struct nvmem_device *nvmem;
	ssize_t err;

	/* Allow write of the whole area only */
	if (off || count != OCP_ART_CONFIG_SIZE)
		return -EFAULT;

	nvmem = ptp_ocp_nvmem_device_get(bp, NULL);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);


	err = nvmem_device_write(nvmem, 0x00, count, buf);
	if (err != count)
		err = -EFAULT;

	ptp_ocp_nvmem_device_put(&nvmem);

	return err;
}
static BIN_ATTR_RW(disciplining_config, OCP_ART_CONFIG_SIZE);

static ssize_t
temperature_table_read(struct file *filp, struct kobject *kobj,
	    OCP_BIN_ATTR_CONST struct bin_attribute *bin_attr, char *buf,
	    loff_t off, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(kobj_to_dev(kobj));
	size_t size = OCP_ART_TEMP_TABLE_SIZE;
	struct nvmem_device *nvmem;
	ssize_t err;

	nvmem = ptp_ocp_nvmem_device_get(bp, NULL);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	if (off > size) {
		err = 0;
		goto out;
	}

	if (off + count > size)
		count = size - off;

	// the configuration is in the very beginning of the EEPROM
	err = nvmem_device_read(nvmem, 0x90 + off, count, buf);
	if (err != count) {
		err = -EFAULT;
		goto out;
	}

out:
	ptp_ocp_nvmem_device_put(&nvmem);

	return err;
}

static ssize_t
temperature_table_write(struct file *filp, struct kobject *kobj,
	     OCP_BIN_ATTR_CONST struct bin_attribute *bin_attr, char *buf,
	     loff_t off, size_t count)
{
	struct ptp_ocp *bp = dev_get_drvdata(kobj_to_dev(kobj));
	struct nvmem_device *nvmem;
	ssize_t err;

	/* Allow write of the whole area only */
	if (off || count != OCP_ART_TEMP_TABLE_SIZE)
		return -EFAULT;

	nvmem = ptp_ocp_nvmem_device_get(bp, NULL);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);


	err = nvmem_device_write(nvmem, 0x90, count, buf);
	if (err != count)
		err = -EFAULT;

	ptp_ocp_nvmem_device_put(&nvmem);

	return err;
}
static BIN_ATTR_RW(temperature_table, OCP_ART_TEMP_TABLE_SIZE);

static OCP_BIN_ATTR_CONST struct bin_attribute *OCP_BIN_ATTR_CONST bin_timecard_attrs[] = {
        &bin_attr_config,
        NULL,
};

static struct attribute *fb_timecard_attrs[] = {
	&dev_attr_serialnum.attr,
	&dev_attr_gnss_sync.attr,
	&dev_attr_optional_image_contract.attr,
	&dev_attr_clock_source.attr,
	&dev_attr_available_clock_sources.attr,
	&dev_attr_external_pps_polarity.attr,
	&dev_attr_internal_pps_polarity.attr,
	&dev_attr_external_pps_pulse_width.attr,
	&dev_attr_external_pps_cable_delay.attr,
	&dev_attr_internal_pps_cable_delay.attr,
	&dev_attr_holdover.attr,
	&dev_attr_mac_i2c.attr,
	&dev_attr_sma1.attr,
	&dev_attr_sma2.attr,
	&dev_attr_sma3.attr,
	&dev_attr_sma4.attr,
	&dev_attr_available_sma_inputs.attr,
	&dev_attr_available_sma_outputs.attr,
	&dev_attr_clock_status_drift.attr,
	&dev_attr_clock_status_offset.attr,
	&dev_attr_irig_output_mode.attr,
	&dev_attr_irig_input_mode.attr,
	&dev_attr_irig_output_control_bits.attr,
	&dev_attr_irig_b_mode.attr,
	&dev_attr_irig_output_am.attr,
	&dev_attr_irig_input_code.attr,
	&dev_attr_irig_input_manual_year.attr,
	&dev_attr_irig_input_am.attr,
	&dev_attr_irig_input_cable_delay.attr,
	&dev_attr_dcf_input_air_delay.attr,
	&dev_attr_dcf_input_bit_position.attr,
	&dev_attr_irig_output_error.attr.attr,
	&dev_attr_irig_input_error.attr.attr,
	&dev_attr_dcf_output_error.attr.attr,
	&dev_attr_dcf_input_error.attr.attr,
	&dev_attr_utc_tai_offset.attr,
	&dev_attr_nmea_enable.attr,
	&dev_attr_nmea_uart_polarity.attr,
	&dev_attr_nmea_baud_rate.attr,
	&dev_attr_available_nmea_baud_rates.attr,
	&dev_attr_nmea_errors.attr,
	&dev_attr_nmea_correction_seconds.attr,
	&dev_attr_nmea_local_offset_minutes.attr,
	&dev_attr_nmea_gnss.attr,
	&dev_attr_available_nmea_gnss.attr,
	&dev_attr_nmea_message_disable_mask.attr,
	&dev_attr_ts_window_adjust.attr,
	&dev_attr_tod_protocol.attr,
	&dev_attr_available_tod_protocols.attr,
	&dev_attr_tod_gnss.attr,
	&dev_attr_available_tod_gnss.attr,
	&dev_attr_tod_message_disable_mask.attr,
	&dev_attr_tod_uart_polarity.attr,
	&dev_attr_tod_errors.attr,
	&dev_attr_tod_baud_rate.attr,
	&dev_attr_available_tod_baud_rates.attr,
	&dev_attr_tod_correction.attr,
	NULL,
};

static const struct attribute_group fb_timecard_group = {
	.attrs = fb_timecard_attrs,
	.bin_attrs = bin_timecard_attrs,
};

static const struct ocp_attr_group fb_timecard_groups[] = {
	{ .cap = OCP_CAP_BASIC,	    .group = &fb_timecard_group },
	{ .cap = OCP_CAP_BASIC,	    .group = &ptp_ocp_timecard_tty_group },
	{ .cap = OCP_CAP_SIGNAL,    .group = &fb_timecard_signal0_group },
	{ .cap = OCP_CAP_SIGNAL,    .group = &fb_timecard_signal1_group },
	{ .cap = OCP_CAP_SIGNAL,    .group = &fb_timecard_signal2_group },
	{ .cap = OCP_CAP_SIGNAL,    .group = &fb_timecard_signal3_group },
	{ .cap = OCP_CAP_FREQ,	    .group = &fb_timecard_freq0_group },
	{ .cap = OCP_CAP_FREQ,	    .group = &fb_timecard_freq1_group },
	{ .cap = OCP_CAP_FREQ,	    .group = &fb_timecard_freq2_group },
	{ .cap = OCP_CAP_FREQ,	    .group = &fb_timecard_freq3_group },
	{ },
};

static struct attribute *art_timecard_attrs[] = {
	&dev_attr_serialnum.attr,
	&dev_attr_clock_source.attr,
	&dev_attr_utc_tai_offset.attr,
	&dev_attr_ts_window_adjust.attr,
	&dev_attr_sma1.attr,
	&dev_attr_sma2.attr,
	&dev_attr_sma3.attr,
	&dev_attr_sma4.attr,
	&dev_attr_available_sma_inputs.attr,
	&dev_attr_available_sma_outputs.attr,
	NULL,
};

static OCP_BIN_ATTR_CONST struct bin_attribute *OCP_BIN_ATTR_CONST bin_art_timecard_attrs[] = {
		&bin_attr_disciplining_config,
		&bin_attr_temperature_table,
		NULL,
};

static const struct attribute_group art_timecard_group = {
	.attrs = art_timecard_attrs,
	.bin_attrs = bin_art_timecard_attrs,
};

static const struct ocp_attr_group art_timecard_groups[] = {
	{ .cap = OCP_CAP_BASIC,	    .group = &art_timecard_group },
	{ .cap = OCP_CAP_BASIC,	    .group = &ptp_ocp_timecard_tty_group },
	{ },
};

static void
gpio_input_map(char *buf, struct ptp_ocp *bp, u16 map[][2], u16 bit,
	       const char *def)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (bp->sma[i].mode != SMA_MODE_IN)
			continue;
		if (map[i][0] & (1 << bit)) {
			sprintf(buf, "sma%d", i + 1);
			return;
		}
	}
	if (!def)
		def = "----";
	strcpy(buf, def);
}

static void
gpio_output_map(char *buf, struct ptp_ocp *bp, u16 map[][2], u16 bit)
{
	char *ans = buf;
	int i;

	strcpy(ans, "----");
	for (i = 0; i < 4; i++) {
		if (bp->sma[i].mode != SMA_MODE_OUT)
			continue;
		if (map[i][1] & (1 << bit))
			ans += sprintf(ans, "sma%d ", i + 1);
	}
}

static void
_signal_summary_show(struct seq_file *s, struct ptp_ocp *bp, int nr)
{
	struct signal_reg __iomem *reg;
	struct ptp_ocp_signal *signal;
	char label[8];
	bool on;
	u32 val;

	if (!bp->signal_out[nr])
		return;

	reg = bp->signal_out[nr]->mem;
	signal = &bp->signal[nr];

	if (!signal)
		return;
	if (!ptp_ocp_core_version_at_least(&reg->version,
					   SIGNAL_VERSION_CURRENT_MAP)) {
		sprintf(label, "GEN%d", nr + 1);
		seq_printf(s, "%7s: unsupported register layout (< 1.3)\n",
			   label);
		return;
	}

	on = signal->running;
	sprintf(label, "GEN%d", nr + 1);
	seq_printf(s, "%7s: %s, period:%llu duty:%d%% phase:%llu pol:%d",
		   label, on ? " ON" : "OFF",
		   signal->period, signal->duty, signal->phase,
		   signal->polarity);

	val = ioread32(&reg->enable);
	seq_printf(s, " [%x", val);
	val = ioread32(&reg->status);
	seq_printf(s, " %x]", val);

	seq_printf(s, " start:%llu\n", signal->start);
}

static void
_frequency_summary_show(struct seq_file *s, int nr,
			struct frequency_reg __iomem *reg)
{
	char label[8];
	bool on;
	u32 val;

	if (!reg)
		return;

	sprintf(label, "FREQ%d", nr + 1);
	val = ioread32(&reg->ctrl);
	on = val & 1;
	val = (val >> 8) & 0xff;
	seq_printf(s, "%7s: %s, sec:%u",
		   label,
		   on ? " ON" : "OFF",
		   val);

	val = ioread32(&reg->status);
	if (val & FREQ_STATUS_ERROR)
		seq_printf(s, ", error");
	if (val & FREQ_STATUS_OVERRUN)
		seq_printf(s, ", overrun");
	if (val & FREQ_STATUS_VALID)
		seq_printf(s, ", freq %lu Hz", val & FREQ_STATUS_MASK);
	seq_printf(s, "  reg:%x\n", val);
}

static int
ptp_ocp_summary_show(struct seq_file *s, void *data)
{
	struct device *dev = s->private;
	struct ptp_system_timestamp sts;
	struct ts_reg __iomem *ts_reg;
	u32 __iomem *status_reg;
	char *buf, *src, *mac_src;
	char error[4];
	struct timespec64 ts;
	struct ptp_ocp *bp;
	u16 sma_val[4][2];
	u32 ctrl, val;
	bool on, map;
	int i;

	buf = (char *)__get_free_page(GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	bp = dev_get_drvdata(dev);

	seq_printf(s, "%7s: /dev/ptp%d\n", "PTP", ptp_clock_index(bp->ptp));
	for (i = 0; i < __PORT_COUNT; i++) {
		if (bp->port[i].line != -1)
			seq_printf(s, "%7s: /dev/ttyS%d\n", ptp_ocp_tty_port_name(i),
				   bp->port[i].line);
	}

	memset(sma_val, 0xff, sizeof(sma_val));
	if (bp->sma_map1) {
		u32 reg;

		reg = ioread32(&bp->sma_map1->gpio1);
		sma_val[0][0] = reg & 0xffff;
		sma_val[1][0] = reg >> 16;

		reg = ioread32(&bp->sma_map1->gpio2);
		sma_val[2][1] = reg & 0xffff;
		sma_val[3][1] = reg >> 16;

		reg = ioread32(&bp->sma_map2->gpio1);
		sma_val[2][0] = reg & 0xffff;
		sma_val[3][0] = reg >> 16;

		reg = ioread32(&bp->sma_map2->gpio2);
		sma_val[0][1] = reg & 0xffff;
		sma_val[1][1] = reg >> 16;
	}

	sma1_show(dev, NULL, buf);
	seq_printf(s, "   sma1: %04x,%04x %s",
		   sma_val[0][0], sma_val[0][1], buf);

	sma2_show(dev, NULL, buf);
	seq_printf(s, "   sma2: %04x,%04x %s",
		   sma_val[1][0], sma_val[1][1], buf);

	sma3_show(dev, NULL, buf);
	seq_printf(s, "   sma3: %04x,%04x %s",
		   sma_val[2][0], sma_val[2][1], buf);

	sma4_show(dev, NULL, buf);
	seq_printf(s, "   sma4: %04x,%04x %s",
		   sma_val[3][0], sma_val[3][1], buf);

	if (bp->ts0) {
		ts_reg = bp->ts0->mem;
		on = ioread32(&ts_reg->enable);
		src = "GNSS1";
		seq_printf(s, "%7s: %s, src: %s\n", "TS0",
			   on ? " ON" : "OFF", src);
	}

	if (bp->ts1) {
		ts_reg = bp->ts1->mem;
		on = ioread32(&ts_reg->enable);
		gpio_input_map(buf, bp, sma_val, 2, NULL);
		seq_printf(s, "%7s: %s, src: %s\n", "TS1",
			   on ? " ON" : "OFF", buf);
	}

	if (bp->ts2) {
		ts_reg = bp->ts2->mem;
		on = ioread32(&ts_reg->enable);
		gpio_input_map(buf, bp, sma_val, 3, NULL);
		seq_printf(s, "%7s: %s, src: %s\n", "TS2",
			   on ? " ON" : "OFF", buf);
	}

	if (bp->ts3) {
		ts_reg = bp->ts3->mem;
		on = ioread32(&ts_reg->enable);
		gpio_input_map(buf, bp, sma_val, 6, NULL);
		seq_printf(s, "%7s: %s, src: %s\n", "TS3",
			   on ? " ON" : "OFF", buf);
	}

	if (bp->ts4) {
		ts_reg = bp->ts4->mem;
		on = ioread32(&ts_reg->enable);
		gpio_input_map(buf, bp, sma_val, 7, NULL);
		seq_printf(s, "%7s: %s, src: %s\n", "TS4",
			   on ? " ON" : "OFF", buf);
	}

	if (bp->pps) {
		ts_reg = bp->pps->mem;
		src = "PHC";
		on = ioread32(&ts_reg->enable);
		map = !!(bp->pps_req_map & OCP_REQ_TIMESTAMP);
		seq_printf(s, "%7s: %s, src: %s\n", "TS5",
			   on && map ? " ON" : "OFF", src);

		map = !!(bp->pps_req_map & OCP_REQ_PPS);
		seq_printf(s, "%7s: %s, src: %s\n", "PPS",
			   on && map ? " ON" : "OFF", src);
	}

	if (bp->fw_cap & OCP_CAP_SIGNAL)
		for (i = 0; i < 4; i++)
			_signal_summary_show(s, bp, i);

	if (bp->fw_cap & OCP_CAP_FREQ)
		for (i = 0; i < 4; i++)
			_frequency_summary_show(s, i, bp->freq_in[i]);

	if (bp->irig_out) {
		ctrl = ioread32(&bp->irig_out->ctrl);
		on = ctrl & IRIG_M_CTRL_ENABLE;
		status_reg = ptp_ocp_timecode_status_reg(
			bp, TIMECODE_ERROR_IRIG_OUTPUT);
		if (status_reg)
			snprintf(error, sizeof(error), "%u",
				 !!(ioread32(status_reg) & TIMECODE_STATUS_ERROR));
		else
			strscpy(error, "n/a", sizeof(error));
		gpio_output_map(buf, bp, sma_val, 4);
		seq_printf(s,
			   "%7s: %s, error: %s, mode: %u, code: %u, out: %s\n",
			   "IRIG", on ? " ON" : "OFF", error,
			   (ctrl & IRIG_CTRL_MODE_MASK) >> IRIG_CTRL_MODE_SHIFT,
			   (ctrl & IRIG_CTRL_CODE_MASK) >> IRIG_CTRL_CODE_SHIFT,
			   buf);
	}

	if (bp->irig_in) {
		ctrl = ioread32(&bp->irig_in->ctrl);
		on = ctrl & IRIG_S_CTRL_ENABLE;
		status_reg = ptp_ocp_timecode_status_reg(
			bp, TIMECODE_ERROR_IRIG_INPUT);
		if (status_reg)
			snprintf(error, sizeof(error), "%u",
				 !!(ioread32(status_reg) & TIMECODE_STATUS_ERROR));
		else
			strscpy(error, "n/a", sizeof(error));
		gpio_input_map(buf, bp, sma_val, 4, NULL);
		seq_printf(s, "%7s: %s, error: %s, mode: %u, src: %s\n",
			   "IRIG in", on ? " ON" : "OFF", error,
			   (ctrl & IRIG_CTRL_MODE_MASK) >> IRIG_CTRL_MODE_SHIFT,
			   buf);
	}

	if (bp->dcf_out) {
		on = ioread32(&bp->dcf_out->ctrl) & DCF_M_CTRL_ENABLE;
		status_reg = ptp_ocp_timecode_status_reg(
			bp, TIMECODE_ERROR_DCF_OUTPUT);
		if (status_reg)
			snprintf(error, sizeof(error), "%u",
				 !!(ioread32(status_reg) & TIMECODE_STATUS_ERROR));
		else
			strscpy(error, "n/a", sizeof(error));
		gpio_output_map(buf, bp, sma_val, 5);
		seq_printf(s, "%7s: %s, error: %s, out: %s\n", "DCF",
			   on ? " ON" : "OFF", error, buf);
	}

	if (bp->dcf_in) {
		on = ioread32(&bp->dcf_in->ctrl) & DCF_S_CTRL_ENABLE;
		status_reg = ptp_ocp_timecode_status_reg(
			bp, TIMECODE_ERROR_DCF_INPUT);
		if (status_reg)
			snprintf(error, sizeof(error), "%u",
				 !!(ioread32(status_reg) & TIMECODE_STATUS_ERROR));
		else
			strscpy(error, "n/a", sizeof(error));
		gpio_input_map(buf, bp, sma_val, 5, NULL);
		seq_printf(s, "%7s: %s, error: %s, src: %s\n", "DCF in",
			   on ? " ON" : "OFF", error, buf);
	}

	if (bp->nmea_out) {
		on = ioread32(&bp->nmea_out->ctrl) & 1;
		if (ptp_ocp_core_version_at_least(&bp->nmea_out->version,
						  TOD_MASTER_VERSION_STATUS))
			snprintf(error, sizeof(error), "%u",
				 ioread32(&bp->nmea_out->status));
		else
			strscpy(error, "n/a", sizeof(error));
		seq_printf(s, "%7s: %s, error: %s\n", "NMEA",
			   on ? " ON" : "OFF", error);
	}

	/* compute src for PPS1, used below. */
	if (bp->pps_select) {
		src = &buf[80];
		mac_src = "GNSS1";

		val = ioread32(&bp->pps_select->gpio2) & 0x3;
		if (val == 0)
			val = ioread32(&bp->pps_select->gpio1) & 0x7;
		else if (val == 0x3)
			val = 0x4;

		if (val & 0x01) {
			gpio_input_map(src, bp, sma_val, 0, NULL);
			mac_src = src;
		} else if (val & 0x02)
			src = "MAC";
		else if (val & 0x04)
			src = "GNSS1";
		else {
			src = "----";
			mac_src = src;
		}
	} else {
		src = "?";
		mac_src = src;
	}
	seq_printf(s, "MAC PPS1 src: %s\n", mac_src);

	gpio_input_map(buf, bp, sma_val, 1, "GNSS2");
	seq_printf(s, "MAC PPS2 src: %s\n", buf);

	/* assumes automatic switchover/selection */
	val = ioread32(&bp->reg->select);
	switch (val >> 16) {
	case 0:
		sprintf(buf, "----");
		break;
	case 2:
		sprintf(buf, "IRIG");
		break;
	case 3:
		sprintf(buf, "%s via PPS1", src);
		break;
	case 6:
		sprintf(buf, "DCF");
		break;
	default:
		strcpy(buf, "unknown");
		break;
	}
	if (ptp_ocp_clock_has_status(bp)) {
		val = ioread32(&bp->reg->status);
		seq_printf(s, "%7s: %s, state: %s\n", "PHC src", buf,
			   val & OCP_STATUS_IN_SYNC ? "sync" : "unsynced");
	} else {
		seq_printf(s, "%7s: %s, state: unavailable (< 1.2)\n",
			   "PHC src", buf);
	}

	if (!ptp_ocp_gettimex(&bp->ptp_info, &ts, &sts)) {
		struct timespec64 sys_ts;
		s64 pre_ns, post_ns, ns;

		pre_ns = timespec64_to_ns(&sts.pre_ts);
		post_ns = timespec64_to_ns(&sts.post_ts);
		ns = (pre_ns + post_ns) / 2;
		ns += (s64)bp->utc_tai_offset * NSEC_PER_SEC;
		sys_ts = ns_to_timespec64(ns);

		seq_printf(s, "%7s: %lld.%ld == %ptT TAI\n", "PHC",
			   ts.tv_sec, ts.tv_nsec, &ts);
		seq_printf(s, "%7s: %lld.%ld == %ptT UTC offset %d\n", "SYS",
			   sys_ts.tv_sec, sys_ts.tv_nsec, &sys_ts,
			   bp->utc_tai_offset);
		seq_printf(s, "%7s: PHC:SYS offset: %lld  window: %lld\n", "",
			   timespec64_to_ns(&ts) - ns,
			   post_ns - pre_ns);
	}

	free_page((unsigned long)buf);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ptp_ocp_summary);

static int
ptp_ocp_tod_status_show(struct seq_file *s, void *data)
{
	struct device *dev = s->private;
	struct ptp_ocp *bp;
	u32 ctrl, expected = 0, val, version;
	int idx;

	bp = dev_get_drvdata(dev);
	version = ioread32(&bp->tod->version);
	if (!version || version == U32_MAX) {
		seq_puts(s, "TOD version register is invalid\n");
		return 0;
	}

	ctrl = ioread32(&bp->tod->ctrl);
	if (!(ctrl & TOD_CTRL_ENABLE)) {
		seq_printf(s, "TOD Slave disabled\n");
		return 0;
	}
	seq_printf(s, "TOD Slave enabled, Control Register 0x%08X\n", ctrl);

	idx = (ctrl >> TOD_CTRL_PROTOCOL_SHIFT) & TOD_CTRL_PROTOCOL_MASK;
	seq_printf(s, "Protocol %s\n", ptp_ocp_tod_proto_name(idx));

	if (version >= TOD_VERSION_GNSS_SELECT) {
		idx = (ctrl >> TOD_CTRL_GNSS_SHIFT) & TOD_CTRL_GNSS_MASK;
		seq_printf(s, "GNSS %s\n", ptp_ocp_tod_gnss_name(idx));
	} else {
		seq_puts(s, "GNSS selector unavailable (< 1.5)\n");
	}

	seq_printf(s, "TOD Version %d.%d.%d\n",
		version >> 24, (version >> 16) & 0xff, version & 0xffff);

	if (version >= TOD_VERSION_STATUS) {
		val = ioread32(&bp->tod->status);
		seq_printf(s, "Status register: 0x%08X\n", val);
	} else {
		seq_puts(s, "Status register unavailable (< 1.2)\n");
	}

	val = ioread32(&bp->tod->adj_sec);
	idx = (val & ~INT_MAX) ? -1 : 1;
	idx *= (val & INT_MAX);
	seq_printf(s, "Correction seconds: %d\n", idx);
	if (!tod_optional_telemetry) {
		seq_puts(s,
			 "Optional UTC/leap/GNSS/satellite telemetry disabled; "
			 "load with tod_optional_telemetry=1 and an exact image contract\n");
		return 0;
	}
	if (!ptp_ocp_optional_image_contract_matches(bp)) {
		(void)ptp_ocp_optional_image_contract_expected(bp, &expected);
		seq_printf(s,
			 "Optional image contract mismatch for %s (expected 0x%08x); telemetry remains gated\n",
			 pci_name(bp->pdev), expected);
		return 0;
	}
	if (!ptp_ocp_tod_optional_utc_enabled(bp)) {
		seq_puts(s,
			 "UTC/leap telemetry is not defined for this protocol/revision\n");
		return 0;
	}

	val = ioread32(&bp->tod->utc_status);
	seq_printf(s, "UTC status register: 0x%08X\n", val);
	seq_printf(s, "UTC offset: %d  valid:%d\n",
		val & TOD_STATUS_UTC_MASK, val & TOD_STATUS_UTC_VALID ? 1 : 0);
	seq_printf(s, "Leap second info valid:%d, Leap second announce %d\n",
		val & TOD_STATUS_LEAP_VALID ? 1 : 0,
		val & TOD_STATUS_LEAP_ANNOUNCE ? 1 : 0);

	val = ioread32(&bp->tod->leap);
	seq_printf(s, "Time to next leap second (in sec): %d\n", (s32) val);

	if (!ptp_ocp_tod_optional_gnss_enabled(bp)) {
		seq_puts(s,
			 "GNSS/satellite telemetry is not defined for this protocol/revision\n");
		return 0;
	}

	val = ioread32(&bp->tod->gnss_status);
	seq_printf(s, "GNSS status register: 0x%08X\n", val);

	idx = (val >> TOD_GNSS_ANT_STATUS_SHIFT) & TOD_GNSS_ANT_STATUS_MASK;
	seq_printf(s, "GNSS Antenna status: %s valid: %d\n",
		ptp_ocp_tod_gnss_ant_status(idx),
		val & TOD_GNSS_ANT_JAM_VAL ? 1 : 0);

	idx = (val >> TOD_GNSS_JAM_STATE_SHIFT) & TOD_GNSS_JAM_STATE_MASK;
	seq_printf(s, "GNSS Jam state: %s valid: %d\n",
		ptp_ocp_tod_gnss_jam_state(idx),
		val & TOD_GNSS_ANT_JAM_VAL ? 1 : 0);

	seq_printf(s, "GNSS Jam indication: %d valid: %d\n",
		(val >> TOD_GNSS_JAM_STRENGTH_SHIFT) & TOD_GNSS_JAM_STRENGTH_MASK,
		val & TOD_GNSS_ANT_JAM_VAL ? 1 : 0);

	idx = (val >> TOD_GNSS_FIX_SHIFT) & TOD_GNSS_FIX_MASK;
	seq_printf(s, "GNSS Fix: %s fix ok: %d valid: %d\n",
		ptp_ocp_tod_gnss_fix(idx),
		val & TOD_GNSS_FIX_OK ? 1 : 0,
		val & TOD_GNSS_FIX_SPOOF_VAL ? 1 : 0);

	idx = (val >> TOD_GNSS_SPOOF_STATE_SHIFT) & TOD_GNSS_SPOOF_STATE_MASK;
	seq_printf(s, "GNSS Spoof state: %s valid: %d\n",
		ptp_ocp_tod_gnss_spoof_state(idx),
		val & TOD_GNSS_FIX_SPOOF_VAL ? 1 : 0);

	val = ioread32(&bp->tod->num_sat);
	seq_printf(s, "Number of Satellites register: 0x%08X\n", val);

	seq_printf(s, "Number of seen satellites: %d valid: %d\n",
		(val >> TOD_SAT_SEEN_SHIFT) & TOD_SAT_SEEN_MASK,
		val & TOD_SAT_VAL ? 1 : 0);

	seq_printf(s, "Number of locked satellites: %d valid: %d\n",
		(val >> TOD_SAT_LOCKED_SHIFT) & TOD_SAT_LOCKED_MASK,
		val & TOD_SAT_VAL ? 1 : 0);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ptp_ocp_tod_status);

static struct dentry *ptp_ocp_debugfs_root;

static void
ptp_ocp_debugfs_add_device(struct ptp_ocp *bp)
{
	struct dentry *d;

	d = debugfs_create_dir(dev_name(&bp->dev), ptp_ocp_debugfs_root);
	bp->debug_root = d;
	debugfs_create_file("summary", 0444, bp->debug_root,
			    &bp->dev, &ptp_ocp_summary_fops);
	if (bp->tod)
		debugfs_create_file("tod_status", 0444, bp->debug_root,
				    &bp->dev, &ptp_ocp_tod_status_fops);
}

static void
ptp_ocp_debugfs_remove_device(struct ptp_ocp *bp)
{
	debugfs_remove_recursive(bp->debug_root);
}

static void
ptp_ocp_debugfs_init(void)
{
	ptp_ocp_debugfs_root = debugfs_create_dir("timecard", NULL);
}

static void
ptp_ocp_debugfs_fini(void)
{
	debugfs_remove_recursive(ptp_ocp_debugfs_root);
}

static void
ptp_ocp_dev_release(struct device *dev)
{
	struct ptp_ocp *bp = dev_get_drvdata(dev);

	mutex_lock(&ptp_ocp_lock);
	idr_remove(&ptp_ocp_idr, bp->id);
	mutex_unlock(&ptp_ocp_lock);
}

static int
ptp_ocp_device_init(struct ptp_ocp *bp, struct pci_dev *pdev)
{
	int i, err;

	mutex_lock(&ptp_ocp_lock);
	err = idr_alloc(&ptp_ocp_idr, bp, 0, 0, GFP_KERNEL);
	mutex_unlock(&ptp_ocp_lock);
	if (err < 0) {
		dev_err(&pdev->dev, "idr_alloc failed: %d\n", err);
		return err;
	}
	bp->id = err;

	bp->ptp_info = ptp_ocp_clock_info;
	spin_lock_init(&bp->lock);
	mutex_init(&bp->mutex);
	for (i = 0; i < __PORT_COUNT; i++)
		bp->port[i].line = -1;
	bp->pdev = pdev;

	device_initialize(&bp->dev);
	dev_set_name(&bp->dev, "ocp%d", bp->id);
	bp->dev.class = &timecard_class;
	bp->dev.parent = &pdev->dev;
	bp->dev.release = ptp_ocp_dev_release;
	dev_set_drvdata(&bp->dev, bp);

	err = device_add(&bp->dev);
	if (err) {
		dev_err(&bp->dev, "device add failed: %d\n", err);
		goto out;
	}

	pci_set_drvdata(pdev, bp);

	return 0;

out:
	ptp_ocp_dev_release(&bp->dev);
	put_device(&bp->dev);
	return err;
}

static void
ptp_ocp_symlink(struct ptp_ocp *bp, struct device *child, const char *link)
{
	struct device *dev = &bp->dev;

	if (sysfs_create_link(&dev->kobj, &child->kobj, link))
		dev_err(dev, "%s symlink failed\n", link);
}

static void
ptp_ocp_link_child(struct ptp_ocp *bp, const char *name, const char *link)
{
	struct device *dev, *child;

	dev = &bp->pdev->dev;

	child = device_find_child_by_name(dev, name);
	if (!child) {
		dev_err(dev, "Could not find device %s\n", name);
		return;
	}

	ptp_ocp_symlink(bp, child, link);
	put_device(child);
}

static int
ptp_ocp_complete(struct ptp_ocp *bp)
{
	struct pps_device *pps;
	char buf[32];
	int i, err;

	sprintf(buf, "ptp%d", ptp_clock_index(bp->ptp));
	ptp_ocp_link_child(bp, buf, "ptp");

	pps = pps_lookup_dev(bp->ptp);
	if (pps)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(10, 3))
		ptp_ocp_symlink(bp, &pps->dev, "pps");
#else
		ptp_ocp_symlink(bp, pps->dev, "pps");
#endif

	if (bp->mro50.name)
		ptp_ocp_symlink(bp, bp->mro50.this_device, "mro50");

	for (i = 0; bp->attr_tbl[i].cap; i++) {
		if (!(bp->attr_tbl[i].cap & bp->fw_cap))
			continue;
		err = sysfs_create_group(&bp->dev.kobj, bp->attr_tbl[i].group);
		if (err)
			return err;
	}

	ptp_ocp_debugfs_add_device(bp);

	return 0;
}

static void
ptp_ocp_phc_info(struct ptp_ocp *bp)
{
	struct timespec64 ts;
	u32 version, select;
	bool sync;

	version = ioread32(&bp->reg->version);
	select = ioread32(&bp->reg->select);
	dev_info(&bp->pdev->dev, "Version %d.%d.%d, clock %s, device ptp%d\n",
		 version >> 24, (version >> 16) & 0xff, version & 0xffff,
		 ptp_ocp_select_name_from_val(ptp_ocp_clock, select >> 16),
		 ptp_clock_index(bp->ptp));

	sync = ptp_ocp_clock_has_status(bp) &&
		(ioread32(&bp->reg->status) & OCP_STATUS_IN_SYNC);
	if (!ptp_ocp_gettimex(&bp->ptp_info, &ts, NULL))
		dev_info(&bp->pdev->dev, "Time: %lld.%ld, %s\n",
			 ts.tv_sec, ts.tv_nsec,
			 ptp_ocp_clock_has_status(bp) ?
				(sync ? "in-sync" : "UNSYNCED") :
				"sync-status unavailable");
}

static void
ptp_ocp_serial_info(struct device *dev, const char *name, int port, int baud)
{
	if (port != -1)
		dev_info(dev, "%5s: /dev/ttyS%-2d @ %6d\n", name, port, baud);
}

static void
ptp_ocp_disable_ptm(struct ptp_ocp *bp)
{
	struct ptm_reg __iomem *reg = bp->ptm;
	u32 status;
	int count;

	/* disable PTM control*/
	iowrite32(0, &reg->ctrl);
	count = 100;
	do {
		status = ioread32(&reg->status);
		if (!status)
			break;
		count--;
	} while (count > 0);

	if (!count) {
		dev_err(&bp->pdev->dev, "PTM not disabled: Status = 0x%X \n", status);
	}
}

static void
ptp_ocp_enable_ptm(struct ptp_ocp *bp)
{
	struct ptm_reg __iomem *reg = bp->ptm;
	u32 status;
	int count, try = 2;

	/* enable PTM control and start first request */
	/* prepare T1 & T4 for next request */

	while (try--) {
		iowrite32(PTM_CONTROL_ENABLE | PTM_CONTROL_TRIGGER, &reg->ctrl);
		count = 100;
		do {
			status = ioread32(&reg->status);
			if ((status & PTM_STATUS_BUSY) == 0)
				break;
			count--;
		} while (count > 0);

		if (count)
			continue;

		dev_err(&bp->pdev->dev, "Enable and trigger PTM failed: Status = 0x%X, try: %d\n", status, 2 - try);
  	}
	bp->ptm_t4_prev = (((u64) ioread32(&reg->t4_time[0]) << 32) |
		(ioread32(&reg->t4_time[1]) & 0xffffffff));

	bp->ptm_t1_prev = (((u64) ioread32(&reg->t1_time[0]) << 32) |
		(ioread32(&reg->t1_time[1]) & 0xffffffff));
}

static void
ptp_ocp_info(struct ptp_ocp *bp)
{
	struct device *dev = &bp->pdev->dev;
	int i;

	ptp_ocp_phc_info(bp);

	for (i = 0; i < __PORT_COUNT; i++) {
		/* The ToD Master is synthesis optional.  Do not read its baud
		 * register merely to decorate the probe log. */
		if (i == PORT_NMEA)
			bp->port[i].baud = -1;
		ptp_ocp_serial_info(dev, ptp_ocp_tty_port_name(i), bp->port[i].line,
				    bp->port[i].baud);
	}
}

static void
ptp_ocp_detach_sysfs(struct ptp_ocp *bp)
{
	struct device *dev = &bp->dev;
	int i;

	sysfs_remove_link(&dev->kobj, "ttyGNSS");
	sysfs_remove_link(&dev->kobj, "ttyGNSS2");
	sysfs_remove_link(&dev->kobj, "ttyMAC");
	sysfs_remove_link(&dev->kobj, "ttyNMEA");
	sysfs_remove_link(&dev->kobj, "ptp");
	sysfs_remove_link(&dev->kobj, "pps");
	sysfs_remove_link(&dev->kobj, "mro50");
	if (bp->attr_tbl)
		for (i = 0; bp->attr_tbl[i].cap; i++)
			sysfs_remove_group(&dev->kobj, bp->attr_tbl[i].group);
}

static void
ptp_ocp_detach(struct ptp_ocp *bp)
{
	int i;

	ptp_ocp_debugfs_remove_device(bp);
	ptp_ocp_detach_sysfs(bp);
	if (timer_pending(&bp->watchdog))
		timer_delete_sync(&bp->watchdog);
	if (bp->ts0)
		ptp_ocp_unregister_ext(bp->ts0);
	if (bp->ts1)
		ptp_ocp_unregister_ext(bp->ts1);
	if (bp->ts2)
		ptp_ocp_unregister_ext(bp->ts2);
	if (bp->ts3)
		ptp_ocp_unregister_ext(bp->ts3);
	if (bp->ts4)
		ptp_ocp_unregister_ext(bp->ts4);
	if (bp->pps)
		ptp_ocp_unregister_ext(bp->pps);
	for (i = 0; i < 4; i++)
		if (bp->signal_out[i])
			ptp_ocp_unregister_ext(bp->signal_out[i]);
	for (i = 0; i < __PORT_COUNT; i++)
		if (bp->port[i].line != -1)
			serial8250_unregister_port(bp->port[i].line);
	if (bp->spi_flash)
		platform_device_unregister(bp->spi_flash);
	if (bp->i2c_ctrl)
		platform_device_unregister(bp->i2c_ctrl);
	if (bp->i2c_mac)
		platform_device_unregister(bp->i2c_mac);
	if (bp->i2c_clk)
		clk_hw_unregister_fixed_rate(bp->i2c_clk);
	if (bp->mro50.name)
		ptp_ocp_unregister_mro50(&bp->mro50);
	if (bp->n_irqs)
		pci_free_irq_vectors(bp->pdev);
	if (bp->ptp)
		ptp_clock_unregister(bp->ptp);
	
	kfree(bp->ptp_info.pin_config);
	device_unregister(&bp->dev);
}

/* XXX out of tree hack - if this symbol is defined, kernel is using new API */
#ifdef DEVLINK_PARAM_GENERIC_ENABLE_ETH_NAME
#define DEVLINK_NEW_API 1
#endif

static inline struct devlink *
ptp_ocp_devlink_alloc(const struct devlink_ops *ops, size_t priv_size,
		      struct device *dev)
{
#ifdef DEVLINK_NEW_API
	return devlink_alloc(ops, priv_size, dev);
#else
	return devlink_alloc(ops, priv_size);
#endif
}

static inline void
ptp_ocp_devlink_register(struct devlink *devlink, struct device *dev)
{
#ifdef DEVLINK_NEW_API
	devlink_register(devlink);
#else
	devlink_register(devlink, dev);
#endif
}

static int
ptp_ocp_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct devlink *devlink;
	struct ptp_ocp *bp;
	int err;

	devlink = ptp_ocp_devlink_alloc(&ptp_ocp_devlink_ops, sizeof(*bp),
					&pdev->dev);
	if (!devlink) {
		dev_err(&pdev->dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device\n");
		goto out_free;
	}

	bp = devlink_priv(devlink);
	err = ptp_ocp_device_init(bp, pdev);
	if (err)
		goto out_disable;

	/* compat mode.
	 * Older FPGA firmware only returns 2 irq's.
	 * allow this - if not all of the IRQ's are returned, skip the
	 * extra devices and just register the clock.
	 */
	err = pci_alloc_irq_vectors(pdev, 1, 64, PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(&pdev->dev, "alloc_irq_vectors err: %d\n", err);
		goto out;
	} else {
		dev_info(&pdev->dev, "MSI/MSI-X info: %d\n", err);
	}

	bp->n_irqs = err;
	pci_set_master(pdev);

	err = ptp_ocp_register_resources(bp, id->driver_data);
	if (err)
		goto out;

	bp->ptp = ptp_clock_register(&bp->ptp_info, &pdev->dev);
	if (IS_ERR(bp->ptp)) {
		err = PTR_ERR(bp->ptp);
		dev_err(&pdev->dev, "ptp_clock_register: %d\n", err);
		bp->ptp = NULL;
		goto out;
	}

	err = ptp_ocp_complete(bp);
	if (err)
		goto out;

	if (bp->ptm) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(10, 3))
		err = pci_enable_ptm(bp->pdev);
#else
		err = pci_enable_ptm(bp->pdev, NULL);
#endif
		if (err)
			goto out;
		ptp_ocp_disable_ptm(bp);
		ptp_ocp_enable_ptm(bp);
	} else {
		bp->ptp_info.getcrosststamp = NULL;
	}
	ptp_ocp_info(bp);
	ptp_ocp_devlink_register(devlink, &pdev->dev);

	if(bp->pdev->msix_enabled) {
		/* Enable MSI-X Irq in LitePCIe*/
		iowrite32(0xFF, &bp->msi->enable);
		dev_info(&pdev->dev, "Enabled MSI-X 0xFF\n");
	}

	return 0;

out:
	ptp_ocp_detach(bp);
	pci_set_drvdata(pdev, NULL);
out_disable:
	pci_disable_device(pdev);
out_free:
	devlink_free(devlink);
	return err;
}

static void
ptp_ocp_remove(struct pci_dev *pdev)
{
	struct ptp_ocp *bp = pci_get_drvdata(pdev);
	struct devlink *devlink = priv_to_devlink(bp);

	devlink_unregister(devlink);
	ptp_ocp_detach(bp);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);

	devlink_free(devlink);
}

static struct pci_driver ptp_ocp_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= ptp_ocp_pcidev_id,
	.probe		= ptp_ocp_probe,
	.remove		= ptp_ocp_remove,
};

static int
ptp_ocp_i2c_notifier_call(struct notifier_block *nb,
			  unsigned long action, void *data)
{
	struct device *dev, *child = data;
	struct ptp_ocp *bp;
	bool add;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
	case BUS_NOTIFY_DEL_DEVICE:
		add = action == BUS_NOTIFY_ADD_DEVICE;
		break;
	default:
		return 0;
	}

	if (!i2c_verify_adapter(child))
		return 0;

	dev = child;
	while ((dev = dev->parent))
		if (dev->driver && !strcmp(dev->driver->name, KBUILD_MODNAME))
			goto found;
	return 0;

found:
	bp = dev_get_drvdata(dev);
	if (add) {
		if (bp->i2c_count++ == 0)
			ptp_ocp_symlink(bp, child, "i2c");
	} else {
		if (--bp->i2c_count == 0)
			sysfs_remove_link(&bp->dev.kobj, "i2c");
	}

	return 0;
}

static struct notifier_block ptp_ocp_i2c_notifier = {
	.notifier_call = ptp_ocp_i2c_notifier_call,
};

static int __init
ptp_ocp_init(void)
{
	const char *what;
	int err;

	ptp_ocp_debugfs_init();

	what = "timecard class";
	err = class_register(&timecard_class);
	if (err)
		goto out;

	what = "i2c notifier";
	err = bus_register_notifier(&i2c_bus_type, &ptp_ocp_i2c_notifier);
	if (err)
		goto out_notifier;

	what = "ptp_ocp driver";
	err = pci_register_driver(&ptp_ocp_driver);
	if (err)
		goto out_register;

	return 0;

out_register:
	bus_unregister_notifier(&i2c_bus_type, &ptp_ocp_i2c_notifier);
out_notifier:
	class_unregister(&timecard_class);
out:
	ptp_ocp_debugfs_fini();
	pr_err(KBUILD_MODNAME ": failed to register %s: %d\n", what, err);
	return err;
}

static void __exit
ptp_ocp_fini(void)
{
	bus_unregister_notifier(&i2c_bus_type, &ptp_ocp_i2c_notifier);
	pci_unregister_driver(&ptp_ocp_driver);
	class_unregister(&timecard_class);
	ptp_ocp_debugfs_fini();
}

module_init(ptp_ocp_init);
module_exit(ptp_ocp_fini);

MODULE_DESCRIPTION("OpenCompute TimeCard driver");
MODULE_LICENSE("GPL v2");
