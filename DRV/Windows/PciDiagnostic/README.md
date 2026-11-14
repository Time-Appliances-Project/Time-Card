# Time Card PCI diagnostic

Windows driver author: Ahmad Byagowi

This lab-only KMDF driver isolates PCI bus rebalance behavior from the
functional Time Card driver. It binds only the WiWi revision 02 hardware
ID. Its service and binary are separate from `timecard` so Meta and Orolia
can continue using the functional driver.

The source creates a KMDF device and supplies no hardware callbacks. It
maps no BARs, accesses no registers or PCI configuration, creates no
interrupt objects, performs no DMA, exposes no user interface, and
enumerates no child devices. Windows and the PCI bus still handle the
normal PnP and power requests. The card's firmware remains active.

The card cannot provide timing services while bound to this driver. This
package is not a release, signing submission, certification result, or
substitute for the functional Time Card HLK run. Restore the functional
driver after the comparison and record the exact binding for each test.

A failing rebalance with this driver would show that functional driver
register access and interrupt handling are not required for that failure.
It would not, by itself, distinguish FPGA behavior from a platform issue.
