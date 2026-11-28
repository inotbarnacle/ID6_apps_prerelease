# Build the ID6Apps desktop installer

Use Go 1.27.3 or newer, Python 3 with Pillow, and the macOS command-line tools.
Build the QNX programs using [the native build guide](../../docs/BUILD.md), or
extract `tools/gaugedeck-fileops` from the matching release QNX archive. The
package ZIPs in the repository's `packages/` are the source for the embedded
payload and display variants.

Run these commands from this directory, substituting the helper and output paths.

```sh
python3 stage-payload.py ../../.. /absolute/path/to/gaugedeck-fileops
sh build-release.sh template /absolute/path/to/output
```

The release script runs the Go tests, makes an ad-hoc signed universal macOS
application and builds the two Windows executables. CarPlay Advanced is fetched
from st1ng's pinned GitHub release on request.

DPFDeck is optional. Generate its resources and package an actual licensed QNX
build with `../package_dpfdeck.py`, as described in the
[DPFDeck guide](../../docs/DPFDECK.md#build-and-optional-installer-payload).
Place the reviewed archive at `packages/dpfdeck.zip` before staging. If absent,
staging removes stale DPF payload and the install choice stays disabled; the
legacy defaults do not change. DPFDeck requires a supported wide layout and
separate firmware/vehicle validation.

For host-only Go tests, `../test_dpf_resources.py --stage-installer-fixture`
stages synthetic DPF input alongside the existing public packages. The fixture
marker is created before the first synthetic payload file, disables DPF
installation and blocks release builds until real inputs have staged
successfully. Interrupted staging retains it. Never distribute or install that
test payload.
