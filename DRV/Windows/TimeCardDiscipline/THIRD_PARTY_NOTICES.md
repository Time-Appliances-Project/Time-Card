# Third-party notices

`TimeCardDiscipline.dll` contains the Orolia/Safran
`disciplining-minipod` (miniCOD) library, version 3.6.0, commit
`2b17a3b7d2a945b2e7b8dc7dfa913bc9c9c3d25e`.

Copyright belongs to the upstream contributors. The library is distributed
under the GNU Lesser General Public License version 2.1 or, at your option, a
later version. The complete corresponding source, local Windows compatibility
changes, license, and provenance are included in this repository at
`Software/oscillatord/third_party/disciplining-minipod`.

Upstream: <https://github.com/Orolia2s/disciplining-minipod>

The Windows Control Center and native oscillatord-service executables embed the
native library. The complete application/service source, library source, local
compatibility changes, and the exact Windows build scripts needed to rebuild
and relink them are distributed together in this repository and at
<https://github.com/Time-Appliances-Project/Time-Card>. Binary releases must be
published together with the matching repository tag or commit so recipients
can rebuild either executable with a modified ABI-compatible library.

## rxi log.c

The miniCOD sources include `log.c` and `log.h` by rxi.

Copyright (c) 2020 rxi

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
