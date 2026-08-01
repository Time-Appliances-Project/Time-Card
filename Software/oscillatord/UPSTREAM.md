# Upstream provenance

This directory imports the complete
[`Orolia2s/oscillatord`](https://github.com/Orolia2s/oscillatord) project.

- Upstream tag: `v3.10.0`
- Upstream commit: `e2d44b7a52e0e93cdc4ad2cc09fad83881f99184`
- Imported: 2026-08-01
- License: GNU General Public License version 2; see [`LICENSE`](LICENSE)

Time Card integration changes are maintained in this repository. They include
automatic `ocpN` discovery, use of the Linux driver's direct mRO-50 bridge,
safe monitoring defaults and token-aware control, repeatable dependency builds,
and Control Center monitoring support.

To compare a future upstream release:

```sh
git clone https://github.com/Orolia2s/oscillatord.git /tmp/oscillatord-upstream
git diff --no-index /tmp/oscillatord-upstream Software/oscillatord
```

Do not remove the upstream GPL license or copyright notices when redistributing
this component.

