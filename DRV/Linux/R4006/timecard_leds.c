// SPDX-License-Identifier: GPL-2.0
/*
 * OCP Time Card PCA9546 mux and IS32FL3207 multicolor LED driver.
 *
 * The card exposes a PCA9546 switch on the primary AXI I2C controller.
 * Channel 1 contains the IS32FL3207 used by the GNSS and SMA RGB LEDs.
 */

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/led-class-multicolor.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>

#define TIMECARD_PCI_VENDOR_FACEBOOK  0x1d9b
#define TIMECARD_PCI_VENDOR_CELESTICA 0x18d4

#define TIMECARD_MUX_CHANNELS       4
#define TIMECARD_LED_MUX_CHANNEL    1
#define TIMECARD_EEPROM_ADDRESS     0x50
#define TIMECARD_BOARD_ID_OFFSET    0x43

#define IS32FL3207_REG_CONTROL      0x00
#define IS32FL3207_REG_PWM_BASE     0x01
#define IS32FL3207_REG_PWM_UPDATE   0x49
#define IS32FL3207_REG_SCALE_BASE   0x4a
#define IS32FL3207_REG_GLOBAL_CURR  0x6e
#define IS32FL3207_REG_OSC_PHASE    0x70
#define IS32FL3207_REG_OPEN_SHORT   0x71
#define IS32FL3207_REG_FAULT_BASE   0x72
#define IS32FL3207_REG_SPREAD       0x78
#define IS32FL3207_REG_RESET        0x7f

#define IS32FL3207_CONTROL_ENABLE   BIT(0)
#define IS32FL3207_GLOBAL_CURRENT   128
#define IS32FL3207_OUTPUTS_PER_LED  3
#define IS32FL3207_PWM_STRIDE       6
#define IS32FL3207_MAX_LEDS         6
#define TIMECARD_MAX_SENSORS        5

struct timecard_sensor_desc {
	const char *type;
	u8 channel;
	u8 address;
};

struct timecard_led_desc {
	const char *name;
	u8 physical_index;
};

struct timecard_led_board_data {
	u8 num_leds;
	u8 preferred_address;
	bool probe_alternate_addresses;
	bool swap_red_green;
	struct timecard_led_desc leds[IS32FL3207_MAX_LEDS];
	u8 num_sensors;
	struct timecard_sensor_desc sensors[TIMECARD_MAX_SENSORS];
};

struct timecard_led_chip;

struct timecard_led {
	struct led_classdev_mc mc_cdev;
	struct mc_subled subleds[3];
	struct timecard_led_chip *chip;
	const struct timecard_led_desc *desc;
	char *class_name;
};

struct timecard_led_chip {
	struct i2c_client *client;
	const struct timecard_led_board_data *board;
	struct regmap *regmap;
	struct mutex lock; /* Serialize register programming and PWM updates. */
	struct timecard_led leds[];
};

struct timecard_mux {
	struct i2c_client *client;
	struct i2c_client *led_client;
	struct i2c_client *sensor_clients[TIMECARD_MAX_SENSORS];
	u8 num_sensor_clients;
	u8 last_channel;
};

static const struct timecard_led_board_data timecard_celestica_leds = {
	.num_leds = 5,
	.preferred_address = 0x34,
	.probe_alternate_addresses = false,
	.swap_red_green = true,
	.leds = {
		{ "gnss1", 4 },
		/* R4006 front-panel SMA order differs from Windows IO1-IO4. */
		{ "sma1",  2 },
		{ "sma2",  3 },
		{ "sma3",  0 },
		{ "sma4",  1 },
	},
	.num_sensors = 5,
	.sensors = {
		{ "lm75b",    0, 0x48 },
		{ "lm75b",    0, 0x49 },
		{ "lm75b",    0, 0x4a },
		{ "sht3x",     1, 0x44 },
		{ "icp10100",  2, 0x63 },
	},
};

static const struct timecard_led_board_data timecard_fb_msix_leds = {
	.num_leds = 6,
	.preferred_address = 0x37,
	.probe_alternate_addresses = true,
	.swap_red_green = false,
	.leds = {
		{ "gnss1", 0 },
		{ "gnss2", 1 },
		{ "sma1",  2 },
		{ "sma2",  3 },
		{ "sma3",  4 },
		{ "sma4",  5 },
	},
};

static const struct timecard_led_board_data timecard_fb_msi_leds = {
	.num_leds = 6,
	.preferred_address = 0x37,
	.probe_alternate_addresses = true,
	.swap_red_green = true,
	.leds = {
		{ "gnss1", 4 },
		{ "gnss2", 5 },
		{ "sma1",  2 },
		{ "sma2",  3 },
		{ "sma3",  0 },
		{ "sma4",  1 },
	},
};

static const struct regmap_config is32fl3207_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = IS32FL3207_REG_RESET,
	.cache_type = REGCACHE_NONE,
};

static struct pci_dev *timecard_find_pci_parent(struct device *dev)
{
	while (dev) {
		if (dev_is_pci(dev))
			return to_pci_dev(dev);
		dev = dev->parent;
	}

	return NULL;
}

static bool timecard_has_r4006_eeprom(struct i2c_adapter *adapter)
{
	static const u8 r4006_id[] = { 'R', '4', '0', '0', '6' };
	u8 offset = TIMECARD_BOARD_ID_OFFSET;
	u8 board_id[ARRAY_SIZE(r4006_id)];
	struct i2c_msg messages[] = {
		{
			.addr = TIMECARD_EEPROM_ADDRESS,
			.len = sizeof(offset),
			.buf = &offset,
		},
		{
			.addr = TIMECARD_EEPROM_ADDRESS,
			.flags = I2C_M_RD,
			.len = sizeof(board_id),
			.buf = board_id,
		},
	};

	return i2c_transfer(adapter, messages, ARRAY_SIZE(messages)) ==
		ARRAY_SIZE(messages) &&
		!memcmp(board_id, r4006_id, sizeof(r4006_id));
}

static const struct timecard_led_board_data *
timecard_select_board_data(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pci_dev *pdev = timecard_find_pci_parent(dev);

	/* Production R4006 cards use Celestica wiring with a generic FB FPGA ID. */
	if (timecard_has_r4006_eeprom(client->adapter)) {
		dev_info(dev, "selected R4006 LED wiring from board EEPROM\n");
		return &timecard_celestica_leds;
	}

	if (!pdev)
		return NULL;

	if (pdev->vendor == TIMECARD_PCI_VENDOR_CELESTICA)
		return &timecard_celestica_leds;

	if (pdev->vendor == TIMECARD_PCI_VENDOR_FACEBOOK)
		return pdev->msix_enabled ? &timecard_fb_msix_leds :
					    &timecard_fb_msi_leds;

	return NULL;
}

static char *timecard_led_prefix(struct device *dev)
{
	struct pci_dev *pdev = timecard_find_pci_parent(dev);

	if (!pdev)
		return devm_kstrdup(dev, "timecard", GFP_KERNEL);

	return devm_kasprintf(dev, GFP_KERNEL, "timecard-%04x-%02x-%02x-%u",
			      pci_domain_nr(pdev->bus), pdev->bus->number,
			      PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
}

static int is32fl3207_write_led_locked(struct timecard_led *led,
				       u8 red, u8 green, u8 blue)
{
	struct timecard_led_chip *chip = led->chip;
	u8 values[IS32FL3207_PWM_STRIDE] = { 0 };
	u8 base = IS32FL3207_REG_PWM_BASE +
		  led->desc->physical_index * IS32FL3207_PWM_STRIDE;
	int ret;

	if (chip->board->swap_red_green) {
		values[0] = green;
		values[2] = red;
	} else {
		values[0] = red;
		values[2] = green;
	}
	values[4] = blue;

	ret = regmap_bulk_write(chip->regmap, base, values, sizeof(values));
	if (ret)
		return ret;

	return regmap_write(chip->regmap, IS32FL3207_REG_PWM_UPDATE, 0);
}

static int timecard_led_brightness_set(struct led_classdev *cdev,
				       enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct timecard_led *led = container_of(mc_cdev, struct timecard_led,
						mc_cdev);
	struct timecard_led_chip *chip = led->chip;
	u8 red, green, blue;
	int ret;

	led_mc_calc_color_components(mc_cdev, brightness);
	red = min_t(unsigned int, led->subleds[0].brightness, 255);
	green = min_t(unsigned int, led->subleds[1].brightness, 255);
	blue = min_t(unsigned int, led->subleds[2].brightness, 255);

	mutex_lock(&chip->lock);
	ret = is32fl3207_write_led_locked(led, red, green, blue);
	mutex_unlock(&chip->lock);

	return ret;
}

static int is32fl3207_initialize(struct timecard_led_chip *chip)
{
	u8 scaling[IS32FL3207_OUTPUTS_PER_LED] = { 0xff, 0xff, 0xff };
	u8 off[IS32FL3207_PWM_STRIDE] = { 0 };
	unsigned int i;
	int ret;

	ret = regmap_write(chip->regmap, IS32FL3207_REG_CONTROL,
			   IS32FL3207_CONTROL_ENABLE);
	if (ret)
		return ret;

	ret = regmap_write(chip->regmap, IS32FL3207_REG_GLOBAL_CURR,
			   IS32FL3207_GLOBAL_CURRENT);
	if (ret)
		return ret;

	/* Keep electrical-test DC mode disabled during normal LED operation. */
	ret = regmap_write(chip->regmap, IS32FL3207_REG_SPREAD, 0);
	if (ret)
		return ret;

	for (i = 0; i < chip->board->num_leds; i++) {
		u8 physical = chip->board->leds[i].physical_index;

		ret = regmap_bulk_write(chip->regmap,
					IS32FL3207_REG_SCALE_BASE +
					physical * IS32FL3207_OUTPUTS_PER_LED,
					scaling, sizeof(scaling));
		if (ret)
			return ret;

		ret = regmap_bulk_write(chip->regmap,
					IS32FL3207_REG_PWM_BASE +
					physical * IS32FL3207_PWM_STRIDE,
					off, sizeof(off));
		if (ret)
			return ret;
	}

	return regmap_write(chip->regmap, IS32FL3207_REG_PWM_UPDATE, 0);
}

static int is32fl3207_probe(struct i2c_client *client)
{
	const struct timecard_led_board_data *board = dev_get_platdata(&client->dev);
	struct timecard_led_chip *chip;
	char *prefix;
	unsigned int i;
	int ret;

	if (!board || !board->num_leds ||
	    board->num_leds > IS32FL3207_MAX_LEDS)
		return -EINVAL;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	chip = devm_kzalloc(&client->dev,
			    struct_size(chip, leds, board->num_leds),
			     GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->board = board;
	mutex_init(&chip->lock);
	chip->regmap = devm_regmap_init_i2c(client, &is32fl3207_regmap_config);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(chip->regmap),
				     "failed to create register map\n");

	i2c_set_clientdata(client, chip);
	prefix = timecard_led_prefix(&client->dev);
	if (!prefix)
		return -ENOMEM;

	ret = is32fl3207_initialize(chip);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to initialize IS32FL3207\n");

	for (i = 0; i < board->num_leds; i++) {
		struct timecard_led *led = &chip->leds[i];
		struct led_classdev *cdev = &led->mc_cdev.led_cdev;

		led->chip = chip;
		led->desc = &board->leds[i];
		led->class_name = devm_kasprintf(&client->dev, GFP_KERNEL,
						 "%s:rgb:indicator-%s",
						 prefix, led->desc->name);
		if (!led->class_name)
			return -ENOMEM;

		led->subleds[0].color_index = LED_COLOR_ID_RED;
		led->subleds[1].color_index = LED_COLOR_ID_GREEN;
		led->subleds[2].color_index = LED_COLOR_ID_BLUE;
		led->subleds[0].intensity = 255;
		led->subleds[1].intensity = 255;
		led->subleds[2].intensity = 255;

		led->mc_cdev.num_colors = ARRAY_SIZE(led->subleds);
		led->mc_cdev.subled_info = led->subleds;
		cdev->name = led->class_name;
		cdev->max_brightness = 255;
		cdev->brightness = LED_OFF;
		cdev->brightness_set_blocking = timecard_led_brightness_set;
		cdev->flags = LED_CORE_SUSPENDRESUME;

		ret = devm_led_classdev_multicolor_register(&client->dev,
							    &led->mc_cdev);
		if (ret)
			return dev_err_probe(&client->dev, ret,
						     "failed to register %s LED\n",
						     led->desc->name);
	}

	dev_info(&client->dev, "registered %u Time Card RGB LEDs at 0x%02x\n",
		 board->num_leds, client->addr);
	return 0;
}

static void is32fl3207_remove(struct i2c_client *client)
{
	struct timecard_led_chip *chip = i2c_get_clientdata(client);
	unsigned int i;

	mutex_lock(&chip->lock);
	for (i = 0; i < chip->board->num_leds; i++)
		is32fl3207_write_led_locked(&chip->leds[i], 0, 0, 0);
	mutex_unlock(&chip->lock);
}

static const struct i2c_device_id is32fl3207_ids[] = {
	{ "is32fl3207" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, is32fl3207_ids);

static struct i2c_driver is32fl3207_driver = {
	.driver = {
		.name = "is32fl3207",
	},
	.probe = is32fl3207_probe,
	.remove = is32fl3207_remove,
	.id_table = is32fl3207_ids,
};

static int timecard_mux_reg_write(struct i2c_mux_core *muxc, u8 value)
{
	struct timecard_mux *mux = i2c_mux_priv(muxc);
	union i2c_smbus_data dummy;
	int ret;

	ret = __i2c_smbus_xfer(muxc->parent, mux->client->addr,
			       mux->client->flags, I2C_SMBUS_WRITE, value,
				I2C_SMBUS_BYTE, &dummy);
	if (!ret)
		mux->last_channel = value;

	return ret;
}

static int timecard_mux_select(struct i2c_mux_core *muxc, u32 channel)
{
	struct timecard_mux *mux = i2c_mux_priv(muxc);
	u8 value = BIT(channel);

	if (mux->last_channel == value)
		return 0;

	return timecard_mux_reg_write(muxc, value);
}

static int timecard_mux_deselect(struct i2c_mux_core *muxc, u32 channel)
{
	return timecard_mux_reg_write(muxc, 0);
}

static int timecard_detect_led_address(struct i2c_adapter *adapter,
				       const struct timecard_led_board_data *board)
{
	static const u8 alternate_addresses[] = { 0x37, 0x36, 0x35, 0x34 };
	union i2c_smbus_data control;
	union i2c_smbus_data global_current;
	unsigned int i, count;
	const u8 *addresses;
	u8 preferred;
	int ret;

	preferred = board->preferred_address;
	addresses = board->probe_alternate_addresses ? alternate_addresses :
							  &preferred;
	count = board->probe_alternate_addresses ? ARRAY_SIZE(alternate_addresses) : 1;

	for (i = 0; i < count; i++) {
		ret = i2c_smbus_xfer(adapter, addresses[i], 0, I2C_SMBUS_READ,
				     IS32FL3207_REG_CONTROL,
				     I2C_SMBUS_BYTE_DATA, &control);
		if (ret)
			continue;

		ret = i2c_smbus_xfer(adapter, addresses[i], 0, I2C_SMBUS_READ,
				     IS32FL3207_REG_GLOBAL_CURR,
				     I2C_SMBUS_BYTE_DATA, &global_current);
		if (ret)
			continue;

		if (!(control.byte & 0x88))
			return addresses[i];
	}

	return -ENODEV;
}

static void
timecard_register_sensor_clients(struct i2c_mux_core *muxc,
				 struct timecard_mux *mux,
				 const struct timecard_led_board_data *board)
{
	unsigned int i;

	for (i = 0; i < board->num_sensors; i++) {
		const struct timecard_sensor_desc *sensor = &board->sensors[i];
		struct i2c_adapter *sensor_adapter;
		struct i2c_board_info info = { };
		struct i2c_client *sensor_client;

		if (sensor->channel >= TIMECARD_MUX_CHANNELS) {
			dev_warn(&mux->client->dev,
				 "invalid mux channel %u for sensor %s\n",
				 sensor->channel, sensor->type);
			continue;
		}

		strscpy(info.type, sensor->type, sizeof(info.type));
		info.addr = sensor->address;
		sensor_adapter = muxc->adapter[sensor->channel];
		sensor_client = i2c_new_client_device(sensor_adapter, &info);
		if (IS_ERR(sensor_client)) {
			dev_warn(&mux->client->dev,
				 "failed to register %s at channel %u address 0x%02x: %ld\n",
				 sensor->type, sensor->channel, sensor->address,
				 PTR_ERR(sensor_client));
			continue;
		}

		mux->sensor_clients[mux->num_sensor_clients++] = sensor_client;
		dev_info(&mux->client->dev,
			 "registered %s on mux channel %u at 0x%02x\n",
			 sensor->type, sensor->channel, sensor->address);
	}
}

static void timecard_unregister_sensor_clients(struct timecard_mux *mux)
{
	while (mux->num_sensor_clients) {
		struct i2c_client *sensor_client;

		sensor_client = mux->sensor_clients[--mux->num_sensor_clients];
		i2c_unregister_device(sensor_client);
	}
}

static int timecard_mux_probe(struct i2c_client *client)
{
	const struct timecard_led_board_data *board;
	struct i2c_adapter *led_adapter;
	struct i2c_board_info led_info = {
		I2C_BOARD_INFO("is32fl3207", 0),
	};
	struct i2c_mux_core *muxc;
	struct timecard_mux *mux;
	int address;
	int channel;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE))
		return -EOPNOTSUPP;

	board = timecard_select_board_data(client);
	if (!board)
		return -ENODEV;

	muxc = i2c_mux_alloc(client->adapter, &client->dev,
			     TIMECARD_MUX_CHANNELS, sizeof(*mux), 0,
			     timecard_mux_select, timecard_mux_deselect);
	if (!muxc)
		return -ENOMEM;

	mux = i2c_mux_priv(muxc);
	mux->client = client;
	i2c_set_clientdata(client, muxc);

	ret = i2c_smbus_write_byte(client, 0);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "PCA9546 did not acknowledge\n");

	for (channel = 0; channel < TIMECARD_MUX_CHANNELS; channel++) {
		ret = i2c_mux_add_adapter(muxc, 0, channel);
		if (ret)
			goto err_del_adapters;
	}

	led_adapter = muxc->adapter[TIMECARD_LED_MUX_CHANNEL];
	address = timecard_detect_led_address(led_adapter, board);
	if (address < 0) {
		dev_warn(&client->dev,
			 "IS32FL3207 not detected on mux channel %u\n",
			 TIMECARD_LED_MUX_CHANNEL);
	} else {
		led_info.addr = address;
		led_info.platform_data = (void *)board;
		mux->led_client = i2c_new_client_device(led_adapter, &led_info);
		if (IS_ERR(mux->led_client)) {
			ret = PTR_ERR(mux->led_client);
			mux->led_client = NULL;
			goto err_del_adapters;
		}
	}

	timecard_register_sensor_clients(muxc, mux, board);

	dev_info(&client->dev,
		 "registered Time Card PCA9546 peripheral topology\n");
	return 0;

err_del_adapters:
	i2c_mux_del_adapters(muxc);
	return ret;
}

static void timecard_mux_remove(struct i2c_client *client)
{
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);
	struct timecard_mux *mux = i2c_mux_priv(muxc);

	timecard_unregister_sensor_clients(mux);
	if (mux->led_client)
		i2c_unregister_device(mux->led_client);
	i2c_mux_del_adapters(muxc);
	i2c_smbus_write_byte(client, 0);
}

static const struct i2c_device_id timecard_mux_ids[] = {
	{ "timecard-led-mux" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, timecard_mux_ids);

static struct i2c_driver timecard_mux_driver = {
	.driver = {
		.name = "timecard-led-mux",
	},
	.probe = timecard_mux_probe,
	.remove = timecard_mux_remove,
	.id_table = timecard_mux_ids,
};

static int __init timecard_leds_init(void)
{
	int ret;

	ret = i2c_add_driver(&is32fl3207_driver);
	if (ret)
		return ret;

	ret = i2c_add_driver(&timecard_mux_driver);
	if (ret)
		i2c_del_driver(&is32fl3207_driver);

	return ret;
}

static void __exit timecard_leds_exit(void)
{
	i2c_del_driver(&timecard_mux_driver);
	i2c_del_driver(&is32fl3207_driver);
}

module_init(timecard_leds_init);
module_exit(timecard_leds_exit);

MODULE_AUTHOR("Time Appliances Project");
MODULE_DESCRIPTION("OCP Time Card I2C mux, sensor, and multicolor LED driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: i2c-mux led-class-multicolor lm75 sht3x icp10100");
