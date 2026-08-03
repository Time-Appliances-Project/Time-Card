# FPGA and UCM coverage audit

This document records the result of comparing the timing-core manuals in
`SOM/FPGA/Doc`, the Linux `ptp_ocp` implementation, and NetTimeLogic's
Universal Configuration Manager (UCM) with Windows driver 1.43 / ABI 15 and
the Time Card Control Center.

The implementation follows two rules throughout:

1. A documented interface version proves the layout of that core, but it does
   not prove which synthesis generics were enabled in a particular bitstream.
2. Software never discovers hardware by reading guessed BAR addresses. Only
   the published, board-specific Meta/Facebook, Celestica, and Orolia/Safran
   ART resource maps are trusted.

## Implemented core coverage

| Reference core | Linux | Windows 1.43 / ABI 15 | Safety boundary |
| --- | --- | --- | --- |
| Adjustable Clock | PHC, source selection, time/offset/drift control, and contract-gated optional telemetry | PHC read/set, cross timestamp, source selection, smooth offset/drift/window/threshold controls, Q16 fractional drift, base status with holdover/aging readiness, and contract-gated servo factors/logs, holdover, outlier filters, rate limiters, aging, revert, and dynamic update | Common registers use revision gates. Synthesis-optional registers require the exact-image contract. NTP requires Clock 1.8+; SyncE selector 8 and dynamic selector 253 require Clock 2.7+; each source also requires its explicit synthesized-datapath contract. |
| Signal Generator | Four PTP periodic outputs with phase/start, repeat count, and cable delay | Four generators with phase-aligned or exact future PHC/TAI start, polarity, period, pulse width, repeat count, cable delay, readback, sticky-status clear, and interrupt-backed completion/error/time-jump event queues | v1.2/v1.3 fields are revision-gated. Every write is read back and the complete prior register set is restored on failure. |
| Signal Timestamper | PTP `EXTTS` and PPS events | Six channels (GNSS1, TS1-TS4, and PHC/PPS), logical polarity, enable, bounded interrupt queues, overflow/drop reporting, timestamp/event counters, cable delay, and bounded 256-bit payload capture where the published register aperture supports them | Standard MSI/MSI-X uses the full documented v1.3+ surface. ART uses only its published basic enable/polarity/interrupt/time surface; extended ART registers remain inaccessible without a trusted image identity and manifest. |
| Configuration Slave | Not instantiated by the published Time Card maps | Replaced by a typed, read-only static core inventory containing trusted offsets, spans, versions, instances, and interrupt messages | No descriptor-ROM sweep and no speculative probe is performed. |
| PPS Master/Slave | PTP PPS/per-out, polarity, width, delay, supervision and errors | Enable, polarity, bounded pulse width, signed cable delay, measured width/status, and explicit W1C fault handling | Reserved bits are preserved; write/readback failure restores control, polarity, width, and delay. |
| IRIG Master/Slave | Mode/code, correction, control bits, delay, and exact-contract-gated AM/year support | IRIG-B/G mode and code, signed correction, 27-bit control field, cable delay, AM selection, slave code/manual year, documented year-valid strobe, and explicit W1C errors | Master AM requires v1.5 plus contract; slave code/year requires v1.5 plus contract; slave AM requires v1.6 plus contract. Failed changes restore every touched register. |
| DCF Master/Slave | Correction, air delay, decoder position, and errors | Enable, signed correction, 30-bit air delay, decoder position, status, and explicit W1C errors | Uses the same disable-change-verify-rollback transaction as IRIG. |
| ToD Slave | NMEA, UBX, TSIP, ESIP, PFEC, GNSS/baud/polarity/correction/message masks, and contract-gated telemetry | Equivalent typed configuration including PFEC selector 4 and mask `0x7f`; UTC/leap/GNSS/satellite telemetry follows protocol-specific revision gates | Optional telemetry is not read unless the exact image is contracted for it. UART and MMIO settings are restored together when configuration fails. |
| ToD Master | Typed NMEA generator controls and optional UTC handshake | Enable, baud, logical polarity, correction, local offset, talker/GNSS, message gates, UART synchronization, error handling, and contract-gated `GxUTC` read/write | Reads use the documented request/done handshake with a bounded poll. Writes use only request bit 0; reserved bit 1 is never set. Failed updates restore MMIO and UART state. |

The standard signal-generator completion vectors are 11-14 for MSI and 43-46
for MSI-X. The six standard timestamper vectors are 1, 2, 6, 15, 16, 0 for
MSI and 33, 34, 38, 47, 48, 32 for MSI-X. ART uses the fixed interrupt map
published by its Linux resource profile.

## Exact-image synthesis contract

Several manuals describe registers or control bits that disappear when a
synthesis generic is disabled. Reading such an address on the wrong image can
raise an AXI decode error, so a core version alone is not an adequate guard.

ABI 15 exposes an explicit, session-scoped contract:

- `IOCTL_TIMECARD_FPGA_CONTRACT_QUERY` reports the trusted raw image word,
  board profile, layout, requested features, effective features, and match
  state.
- `IOCTL_TIMECARD_FPGA_CONTRACT_SET` requires ABI 15, the exact raw image word,
  the exact board/layout pair, and acknowledgement value `TCMF`.
- Unknown capability bits, nonzero reserved fields, a mismatched/reflashed
  image, and write-only ToD UTC without read support are rejected.
- The driver re-reads the trusted image identity before every optional access.
  A mismatch fails closed instead of carrying a contract across a reflash.
- The contract is not persisted or automatically restored. It must be applied
  deliberately for each driver/device session from the bitstream's build
  manifest.

Contract flags cover Adjustable Clock servo/log and advanced controls, ToD
telemetry, ToD Master UTC read/write, IRIG Master/Slave AM, IRIG Slave
code/year, synthesized NTP-client, SyncE, and dynamic-clock sources, and future
ART extended timestamp support. ART currently has no equivalent trusted
image-identity word, so its
extended timestamp contract cannot be activated; its published basic capture
surface remains available.

## Revision gates implemented

The public ABI returns only fields that exist in the reported core revision:

- Adjustable Clock servo/log: 1.6; fractional log: 2.0; holdover: 2.1;
  outlier filters: 2.2; rate limiters: 2.3; dynamic control: 2.4; aging: 2.5;
  revert: 2.6; NTP source: 1.8 plus an exact synthesis contract; SyncE source 8
  and dynamic source 253: 2.7 plus their respective exact contracts.
- ToD Slave protocols: NMEA always, UBX 1.6, TSIP 1.9, ESIP 2.1, and PFEC 2.3.
  NMEA UTC telemetry starts at 2.0 and GNSS telemetry at 2.2; UBX uses 1.6
  and 1.7 respectively; TSIP, ESIP, and PFEC use the revision that introduced
  their protocol.
- IRIG Master AM: 1.5. IRIG Slave code/manual year: 1.5. IRIG Slave AM: 1.6.
- Signal Timestamper current layout: 1.3. Signal Generator cable delay: 1.2;
  repeat count and the current timing/interrupt layout: 1.3.

## UCM cross-audit

UCM 5.3.01 was audited at commit
`0a90709e418aa5b8422ebe3abd895feb70074657`. Its implementations were used to
identify behavior and register coverage, not copied into this repository.

The applicable gaps found in UCM are now represented by typed Time Card APIs:
advanced Adjustable Clock controls, all documented ToD parser protocols and
telemetry, ToD Master `GxUTC`, IRIG AM/code/year, timestamp-channel capture,
generator completion events, and core inventory. Multi-card enumeration is
also implemented in the Windows client and Control Center through the device
interface, with stable identity and explicit active-card selection.

UCM also supports many cores that are not part of any published Time Card
resource map: PTP ordinary/transparent/hybrid clocks and server/client blocks,
NTP server/client, HSR/PRP, TSN, SyncE, RTC, TAP, PHY, switch, mux, generic
GPIO/I2C/EEPROM, and generic frequency/dynamic-clock blocks. They are not
software gaps in this driver. They become applicable only if a future Time Card
bitstream publishes trusted base addresses, interrupt assignments, and a
synthesis manifest for them. An unrestricted UCM-style raw register console
and replayable raw-write scripts are intentionally excluded from the
production ABI.

## Remaining hardware and bitstream dependencies

The software-feasible gaps identified by the manuals and UCM audit are covered.
The following items cannot be completed safely in host software alone:

- A machine-readable, signed synthesis manifest is still absent from current
  images. Exact-image contracts therefore require an operator to verify the
  bitstream build manifest before opting in.
- The Configuration Slave cannot be used unless a future bitstream actually
  instantiates its ROM and assigns a trusted address.
- Register acceptance does not prove that PPS delay, IRIG mode, ToD parser, or
  similar synthesis-selected datapaths are physically present. Validate them
  with a receiver stream or a PPS/IRIG/DCF/SMA loopback.
- ART extended timestamper counters, cable delay, event count, and payload data
  remain gated until ART publishes a trustworthy image identity and extended
  aperture contract.
- UCM network-oriented cores require FPGA additions and board-specific resource
  maps before a Windows or Linux software layer can expose them.

## Validation

- Kernel register structures use compile-time size and offset assertions.
- `tools/test-fpga-abi.ps1` checks the broad ABI/driver/CLI integration.
- `tools/test-fpga-advanced.ps1` checks ABI 15 layouts, revision gates,
  exact-image fail-closed behavior, trusted static inventory, interrupt maps,
  and transactional rollback invariants without touching hardware.
- `DRV/Linux/test-fpga-contract.ps1` checks the Linux exact-image gates,
  PFEC masks/telemetry, IRIG optional fields, and initialization ordering.
- Multi-card and Control Center tests validate enumeration and managed ABI
  layout independently of installed hardware.
- WDK, service, provider, and Control Center Release builds validate compile
  and packaging integration.

These are static and build checks, not a claim of runtime validation. Before a
production deployment, install 1.43 on each supported board/image combination,
exercise all contracted features, verify interrupt delivery under load, and
perform signal loopbacks with calibrated external equipment.
