# FPGA Binary Sources
The FPGA design based on NetTimeLogic Cores is available as binary-only (bitstream) specifically for the Timecard, which however can be used free of charge without restrictions.

The binaries are for two different hardware versions available:

- **Production** 

- **SOM** (System On Module)


More details can be found in the subfolders.


## PCI Revision ID

For a given PCI device (vendor ID and device ID), the PCI Revision ID identifies the hardware interface that software can rely on:

* Compatible firmware builds keep the same PCI Revision ID. Compatibility covers register offsets and semantics, access widths and byte order, interrupt assignments and enable behavior, and the set of usable peripherals.
* A change that breaks any of these gets a new PCI Revision ID before release.
* Adding a peripheral at a previously unused address or interrupt input does not break compatibility and keeps the revision.
* The image ID (version register at AXI offset 0x0002_0000, BAR0 + 0x0202_0000 on LitePCIe images) is a build number only. It is not a compatibility check.
* Drivers should reject PCI Revision IDs they do not recognize.

| PCI ID | Revision | Interface | Binaries |
|--------|----------|-----------|----------|
| 18d4:1008 | 02 | LitePCIe / MSI-X | [Production/Binaries/LitePCIe](Production/Binaries/LitePCIe) |
| 2163:1008 | 02 | LitePCIe / MSI-X | [Production/Binaries/LitePCIe/WiWi](Production/Binaries/LitePCIe/WiWi) |
| 1d9b:0400 | 02 | LitePCIe / MSI-X | [SOM LitePCIe](../../SOM/FPGA/Binaries/LitePCIe) |

LitePCIe / MSI-X interface (revision 02):

* The AXI register map starts at BAR0 + 0x0200_0000. The LitePCIe CSRs are below that: MSI enable mask at 0x1800, MSI-X PBA at 0x1808, MSI-X table at 0x2000.
* Only aligned 32-bit accesses are supported.
* FPGA interrupt input n is signaled as MSI-X vector n + 32.
* The MSI enable register at BAR0 + 0x1800 is a per-input mask: bit n enables interrupt input n. It must be set before any sub-device that relies on interrupts is probed.
