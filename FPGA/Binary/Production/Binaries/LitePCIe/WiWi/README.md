# WiWi Production Time Card image (LitePCIe)

WiWi variant of the Production Time Card image with the LitePCIe core. It is built from the same design as the [Celestica LitePCIe image](..). The only difference is the PCI vendor ID (and the vendor ID in the flash update header). The bitstreams differ only in the PCIe configuration bits and the CRC.

| Item | Value |
|------|-------|
| PCI ID | 2163:1008 |
| PCI Revision ID | 02 (LitePCIe / MSI-X, see [PCI Revision ID](../../../../README.md#pci-revision-id)) |
| PCI subsystem ID | 10ee:0007 (Xilinx default) |
| Image ID (BAR0 + 0x0202_0000) | 0x0000800f |
| FPGA | xc7a100tfgg484 |
| Tool | Vivado 2023.2 |
| Build date | 2026-11-12 |

## Files

| File | Use | SHA-256 |
|------|-----|---------|
| Factory_TimeCardWiwi.bin | Load the SPI flash via JTAG (golden image + update image) | c0c3977caaf9205355468283bda15ffbc076acd3185cb29b50265040e9d12f47 |
| Factory_TimeCardWiwi.prm | Flash layout of the factory image | c54694caf540189824633143156fe51c77f8ca8ee1e587ef3c95397fa9f9793c |
| TimeCardWiwi.bit | Configure the FPGA via JTAG (volatile) | a05b7ba7d6f5883456b6d4d3c49af7e138abe8dd8e11d509efaea6246a325381 |
| TimeCardWiwi.bin | Update image, SPI flash @ 0x00400000 | 7b5cf6a4c0eaa9d2072bed1c677c52c2b6362c2830a98fcb157895602cdee1a6 |
| TimeCardWiwi_Wiwi.bin | Update image with header for `devlink dev flash` (vendor 0x2163, device 0x1008) | e6d79d576d2b0d882d74de1918b9ce749fa7c30cc38d8ed746f445cc49580e91 |

## Flash layout

* 0x0000_0000: golden image. It starts the update image via MultiBoot (WBSTAR 0x0040_0000, IPROG) and falls back to the golden image via the configuration watchdog if the update image fails.
* 0x0040_0000: update image.

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
