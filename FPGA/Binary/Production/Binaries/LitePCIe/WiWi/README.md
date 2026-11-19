# WiWi Production Time Card image (LitePCIe)

WiWi variant of the Production Time Card image with the LitePCIe core. It is built from the same design as the [Celestica LitePCIe image](..). The only difference is the PCI vendor ID (and the vendor ID in the flash update header). The bitstreams differ only in the PCIe configuration bits and the CRC.

| Item | Value |
|------|-------|
| PCI ID | 2163:1008 |
| PCI Revision ID | 02 (LitePCIe / MSI-X, see [PCI Revision ID](../../../../README.md#pci-revision-id)) |
| PCI subsystem ID | 10ee:0007 (Xilinx default) |
| Image ID (BAR0 + 0x0202_0000) | 0x00008011 (golden image: 0x80110000) |
| FPGA | xc7a100tfgg484 |
| Tool | Vivado 2023.2 |
| Build date | 2026-11-19 |

## Files

| File | Use | SHA-256 |
|------|-----|---------|
| Factory_TimeCardWiwi.bin | Load the SPI flash via JTAG (golden image + update image) | a731e384d3239c160766e0e6e14b26d23a4a06cba52dd121120c5426c727b0b7 |
| Factory_TimeCardWiwi.prm | Flash layout of the factory image | 291da693a287e9eda26a7b97983fbe5e5fcc12cd12f15d1b3cc9f7eb4ff40a5b |
| TimeCardWiwi.bit | Configure the FPGA via JTAG (volatile) | b333ed2d768bcd5d1cb138bffaca14f3686586dcadac11a13b4fe626af7ca810 |
| TimeCardWiwi.bin | Update image, SPI flash @ 0x00400000 | fe80fa13ac68cc9567cd2fb3f99867ed5ac20a818b5535b322cc808ba47526ea |
| TimeCardWiwi_Wiwi.bin | Update image with header for `devlink dev flash` (vendor 0x2163, device 0x1008) | 1aacc54055e640839fa2ad3d0deb20aec5e806c9e6d1797e0b5d747cc714af91 |

## Flash layout

* 0x0000_0000: golden image. It starts the update image via MultiBoot (WBSTAR 0x0040_0000, IPROG).
* 0x0040_0000: update image.

The FPGA falls back to the golden image if the update image fails to configure: CRC error, IDCODE error, or configuration watchdog timeout. A fallback is visible as image ID 0x80110000 and in the FPGA BOOTSTS register. An update image that configures but does not work as intended does not trigger a fallback.

Both images capture the SPI flash data on the falling CCLK edge (`BITSTREAM.CONFIG.SPI_FALL_EDGE YES`). With the earlier rising-edge capture, some hosts that reconfigure the FPGA on a warm restart saw a CRC error on the update image and a fallback to the golden image.

## Changes in image 0x8011

* SPI flash data is captured on the falling CCLK edge in the golden and the update image (fixes the golden fallback on host warm restart).
* PTM: the PTM configuration write gets a Completion (fix from image 0x8010, first public release).
* PCI identity, register map, and flash layout are unchanged from image 0x800f.

An update via `devlink` writes the update image only. The golden image on the card is kept, so the falling-edge capture of the golden image takes effect only after a full flash with Factory_TimeCardWiwi.bin.

## Firmware upgrade

```
$ cp TimeCardWiwi_Wiwi.bin /lib/firmware
$ devlink dev flash pci/<bdf> file TimeCardWiwi_Wiwi.bin
```

Flash updates require the patched spi-xilinx driver from [DRV/Linux](../../../../../../DRV/Linux) (`0002-spi-xilinx-Inhibit-transmitter-modern-kernels.patch`).

## Known issues

* The GNSS2 UART is present but not connected to any pins.
* The flash soft reset in the startup configuration targets a wrong address. As a result, the first SPI flash command after power-up fails, and the JEDEC ID reads as ffffffffffff unless the driver resets the flash before identification.

## License

See [LICENSE.md](../../LICENSE.md). This image uses the [LitePCIe](https://github.com/enjoy-digital/litepcie) core instead of the AXI Memory Mapped to PCI Express core.
