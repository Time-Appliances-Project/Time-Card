# Time Card host software

This directory contains host-side services that complement the kernel drivers.

- [`oscillatord`](oscillatord) disciplines an Orolia/Safran ART mRO-50 against
  its GNSS reference, initializes the card PHC, exports GNSS PPS to NTP shared
  memory, and provides monitoring telemetry.

The Linux kernel interface remains in [`../DRV/Linux`](../DRV/Linux). The
Windows driver and Control Center remain in
[`../DRV/Windows`](../DRV/Windows). Windows uses the vendored miniCOD source
directly for native ART mRO-50 discipline through ABI 11, while the same
workspace can also connect to an `oscillatord` monitoring endpoint on a Linux
Time Card host. SA53 and other board variants are selected independently by
the Windows driver's oscillator-capability discovery.
