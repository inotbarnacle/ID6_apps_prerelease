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
