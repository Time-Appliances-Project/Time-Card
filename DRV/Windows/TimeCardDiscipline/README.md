# Native Windows disciplining library

This DLL is a narrow Windows ABI around Orolia's LGPL miniCOD implementation.
It accepts the same phase, GNSS-validity, oscillator-lock, temperature, fine,
coarse, and quantization-error inputs used by `oscillatord`, and returns the
same action and monitoring state. Hardware access remains in the signed KMDF
driver and the Control Center applies every action through bounded IOCTLs.

The library is built for x64 with clang-cl by `build-discipline-library.cmd`.
Its upstream source and license are retained under
`Software/oscillatord/third_party/disciplining-minipod`.
