obj-m += i2c-xiic.o
obj-m += at24.o
obj-m += led-class-multicolor.o
obj-m += timecard_leds.o
obj-m += sht3x.o
obj-m += icp10100.o

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
INSTALL_MOD_DIR ?= updates/timecard

.PHONY: all clean install

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

install: all
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install \
		INSTALL_MOD_DIR=$(INSTALL_MOD_DIR)
