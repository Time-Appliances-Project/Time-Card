MIT License

Copyright (c) 2020-2026 Time Appliances Project (TAP) contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Third-party components

Some directories in this repository contain third-party components that are
distributed under their own licenses, which take precedence for those files:

- `FPGA/Open-Source/` — Copyright (c) NetTimeLogic GmbH, licensed under the
  GNU Lesser General Public License v3 (see `FPGA/Open-Source/LICENSE.md`).
- `DRV/Linux/ptp_ocp.c` — Linux kernel driver, licensed GPL-2.0-only (see the
  SPDX identifier in the file).
- `DRV/Windows/` — Windows driver sources, licensed BSD-3-Clause (see the
  SPDX identifiers in the files).
- `Software/oscillatord/` — imported Orolia/Safran oscillator-disciplining
  daemon and Time Card integration changes, licensed under GNU GPL version 2
  (see `Software/oscillatord/LICENSE` and `Software/oscillatord/UPSTREAM.md`).
- `Software/oscillatord/third_party/disciplining-minipod/` — Orolia miniCOD
  oscillator-discipline library, licensed under GNU LGPL version 2.1 or later
  (see its `LICENSE` and `UPSTREAM.md`). The Windows application loads it as a
  separate DLL; its notices, wrapper, and corresponding source are retained in
  the repository.
- `FPGA/Binary/Production/Binaries/` and `SOM/FPGA/Binaries/` — FPGA
  bitstreams distributed under the terms in the `LICENSE.md` file in each of
  those directories.

The Time Card specification itself is governed by the Open Web Foundation
agreements described in the License section of the top-level `README.md`.
