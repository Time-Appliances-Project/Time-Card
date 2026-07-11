# Driver

Driver is based on a kernel module for CentOS and Ubunutu. 
Kernel 5.12+ is recommended

## Instruction
Make sure vt-d option is enabled in BIOS.   
Run the remake followed by modprobe ptp_ocp

## Modern kernels (6.x and later)

`ptp_ocp.c` carries version guards so it builds from 5.x up to current
kernels (verified against 7.0). The guarded APIs are: the PTM
cross-timestamp interface (`system_counterval_t.use_nsecs` with
`CSID_X86_ART` replaces `convert_art_ns_to_tsc()` on >= 6.10), timer APIs
(`timer_delete_sync`, `timer_container_of`), const-ified
`struct bin_attribute` callbacks and groups, the const
`device_find_child()` match signature, and the embedded PPS device.

### PTM (Precision Time Measurement)

With the LitePCIe-with-PTM gateware (release `TimeCardPTM_V31` or later)
the driver enables PCIe PTM at probe and implements `getcrosststamp()`
through hardware PTM dialogs, so `PTP_SYS_OFFSET_PRECISE` works and
`phc2sys`/`chrony` pick it up automatically — this removes the PCIe
read-asymmetry from PHC comparisons. The root port above the card must
advertise the PTM Root capability. Verify after boot:

```
lspci -vvv -s <card> | grep -A3 "Precision Time"   # Requester+ ... Enabled+
dmesg | grep "PTM enabled"
```

### Flashing note (spi-xilinx)

Updating the gateware via `devlink dev flash` needs the spi-xilinx fix in
`0002-spi-xilinx-Inhibit-transmitter-modern-kernels.patch` (a rebase of
the original 0001 patch, which no longer applies to current kernels).
Without it, erases succeed but page-program writes time out and the flash
chip locks up mid-update. Build the patched `spi-xilinx.ko` and install it
under `/lib/modules/$(uname -r)/updates/` before flashing.

## Outcome
```
$ ls -g /sys/class/timecard/ocp0/
total 0
-r--r--r--. 1 root 4096 Sep  8 18:20 available_clock_sources
-r--r--r--. 1 root 4096 Sep  8 18:20 available_sma_inputs
-r--r--r--. 1 root 4096 Sep  8 18:20 available_sma_outputs
-rw-r--r--. 1 root 4096 Sep  8 18:20 clock_source
lrwxrwxrwx. 1 root    0 Sep  8 18:20 device -> ../../../0000:02:00.0
-rw-r--r--. 1 root 4096 Sep  8 18:20 external_pps_cable_delay
-r--r--r--. 1 root 4096 Sep  8 18:20 gnss_sync
-rw-r--r--. 1 root 4096 Sep  8 18:20 internal_pps_cable_delay
-rw-r--r--. 1 root 4096 Sep  8 18:20 irig_b_mode
-rw-r--r--. 1 root 4096 Sep  8 18:20 pci_delay
drwxr-xr-x. 2 root    0 Sep  8 18:20 power
lrwxrwxrwx. 1 root    0 Sep  8 18:20 ptp -> ../../ptp/ptp4
-r--r--r--. 1 root 4096 Sep  8 18:20 serialnum
-rw-r--r--. 1 root 4096 Sep  8 18:20 sma1_in
-rw-r--r--. 1 root 4096 Sep  8 18:20 sma2_in
-rw-r--r--. 1 root 4096 Sep  8 21:04 sma3_out
-rw-r--r--. 1 root 4096 Sep  8 21:04 sma4_out
lrwxrwxrwx. 1 root    0 Sep  8 18:20 subsystem -> ../../../../../../class/timecard
lrwxrwxrwx. 1 root    0 Sep  8 18:20 ttyGNSS -> ../../tty/ttyS5
lrwxrwxrwx. 1 root    0 Sep  8 18:20 ttyMAC -> ../../tty/ttyS6
lrwxrwxrwx. 1 root    0 Sep  8 18:20 ttyNMEA -> ../../tty/ttyS7
-rw-r--r--. 1 root 4096 Sep  8 18:20 uevent
-rw-r--r--. 1 root 4096 Sep  8 18:20 utc_tai_offset
```

The main resource directory is accessed through the /sys/class/timecard/ocpN directory, which provides links to the various TimeCard resources.  The device links can easily be used in scripts:

```
  tty=$(basename $(readlink /sys/class/timecard/ocp0/ttyGNSS))
  ptp=$(basename $(readlink /sys/class/timecard/ocp0/ptp))

  echo "/dev/$tty"
  echo "/dev/$ptp"
```

After successfully loading the driver, one will see:
* PTP POSIX clock, linking to the physical hardware clock (PHC) on the Time Card (`/dev/ptp4`) 
* GNSS serial `/dev/ttyS5` 
* Atomic clock serial `/dev/ttyS6`
* NMEA Master serial `/dev/ttyS7`
* i2c (`/dev/i2c-*`) device

Now, one can use standard `linuxptp` tools such as `phc2sys` or `ts2phc` to copy, sync, tune, etc... See more in [software](/Software) section

## Windows Driver

The Windows driver is a KMDF-based driver that provides similar functionality to the Linux version.

### Features
*   **PTP Support**: High-precision time reading via IOCTL.
*   **Serial Ports**: Automatically enumerates the 4 UARTs (GNSS, MAC, NMEA) as standard Windows COM ports.
*   **Configuration**: Supports board configuration and SMA mapping through a custom interface.

### Instruction
1.  **Enable Test-Signing**: `bcdedit /set testsigning on` (Required for non-production signed drivers).
2.  **Install**: Right-click `windows/timecard.inf` and select **Install**.
3.  **Verify**: Open **Device Manager** and check for "TimeCard High Precision Clock" under "System devices" and the corresponding COM ports under "Ports (COM & LPT)".

### Outcome
Once installed, you can use standard serial tools (like PuTTY or Teraterm) to communicate with the GNSS/Atomic clock on the card. High-precision time synchronization applications can use the provided IOCTL interface.

## Driver is included in the mainstream Linux Kernel
* Initial primitive version ([5.2](https://git.kernel.org/pub/scm/linux/kernel/git/netdev/net-next.git/commit/?id=a7e1abad13f3f0366ee625831fecda2b603cdc17))
* Exposing all devices version ([5.15](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=773bda96492153e11d21eb63ac814669b51fc701)) 
