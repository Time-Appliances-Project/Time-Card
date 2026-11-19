# Production Time Card image (LitePCIe)

Production Time Card image with the LitePCIe core (MSI-X, PTM). The [WiWi variant](WiWi) is built from the same design and differs only in the PCI vendor ID.

| Item | Value |
|------|-------|
| PCI ID | 18d4:1008 |
| PCI Revision ID | 02 (LitePCIe / MSI-X, see [PCI Revision ID](../../../README.md#pci-revision-id)) |
| PCI subsystem ID | 10ee:0007 (Xilinx default) |
| Image ID (BAR0 + 0x0202_0000) | 0x00008011 (golden image: 0x80110000) |
| FPGA | xc7a100tfgg484 |
| Tool | Vivado 2023.2 |
| Build date | 2026-11-18 |

## Files

| File | Use | SHA-256 |
|------|-----|---------|
| Factory_TimeCardProduction.bin | Load the SPI flash via JTAG (golden image + update image) | 13f416b3b99ce9afc53ada4ef5d037b834113c8ac569105365bf798f2818d9dc |
| Factory_TimeCardProduction.prm | Flash layout of the factory image | 6bcbcf87a3d462af6427a03b2e471368768797d13ff7618eddcfb38d5c27b283 |
| TimeCardProduction.bit | Configure the FPGA via JTAG (volatile) | e9cd6f6f758f2296b064cd2f2dd54e541da3013fcb716d568dcc7e528251fb8b |
| TimeCardProduction.bin | Update image, SPI flash @ 0x00400000 | ea3c5e55fceed421f665e4c4ee96670ecbe9865001e1dc6875dfae3d7b030e6e |
| TimeCardProduction_Celestica.bin | Update image with header for `devlink dev flash` (vendor 0x18d4, device 0x1008) | f9978edb7f463db724729a26da40155decb70381cd16fe855cce9ca8513a51bd |

## Flash layout

* 0x0000_0000: golden image. It starts the update image via MultiBoot (WBSTAR 0x0040_0000, IPROG).
* 0x0040_0000: update image.

The FPGA falls back to the golden image if the update image fails to configure: CRC error, IDCODE error, or configuration watchdog timeout. A fallback is visible as image ID 0x80110000 and in the FPGA BOOTSTS register. An update image that configures but does not work as intended does not trigger a fallback.

Both images capture the SPI flash data on the falling CCLK edge (`BITSTREAM.CONFIG.SPI_FALL_EDGE YES`). With the earlier rising-edge capture, some hosts that reconfigure the FPGA on a warm restart saw a CRC error on the update image and a fallback to the golden image.

## Changes in image 0x8011

* SPI flash data is captured on the falling CCLK edge in the golden and the update image (fixes the golden fallback on host warm restart).
* PTM: the PTM configuration write gets a Completion.
* PCI identity, register map, and flash layout are unchanged from image 0x800f.

An update via `devlink` writes the update image only. The golden image on the card is kept, so the falling-edge capture of the golden image takes effect only after a full flash with Factory_TimeCardProduction.bin.

## Firmware upgrade

```
$ cp TimeCardProduction_Celestica.bin /lib/firmware
$ devlink dev flash pci/<bdf> file TimeCardProduction_Celestica.bin
```

Flash updates require the patched spi-xilinx driver from [DRV/Linux](../../../../../DRV/Linux) (`0002-spi-xilinx-Inhibit-transmitter-modern-kernels.patch`).

## Known issues

* The GNSS2 UART is present but not connected to any pins.
* The flash soft reset in the startup configuration targets a wrong address. As a result, the first SPI flash command after power-up fails, and the JEDEC ID reads as ffffffffffff unless the driver resets the flash before identification.

## License

See [LICENSE.md](../LICENSE.md). This image uses the [LitePCIe](https://github.com/enjoy-digital/litepcie) core instead of the AXI Memory Mapped to PCI Express core.
