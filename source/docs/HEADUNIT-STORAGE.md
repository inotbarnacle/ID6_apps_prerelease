# Optional head-unit storage evidence

`tools/capture_headunit.py --storage` adds a storage snapshot to the existing
private compatibility report. Use it only after owner approval of read-only
SSH access and this exact added command. A CLI option records the requested
scope; it does not authorize access to a physical vehicle.

The default capture keeps its existing reads and report fields. With `--storage`,
a recognized direct OMAP unit gets one additional read:

```sh
LC_ALL=C df -kP /var/opt/hmi/data/ID5
```

For split-node units reached on the Jacinto, the command runs on the HMI OMAP:

```sh
on -f hu-omap sh -c "LC_ALL=C df -kP /var/opt/hmi/data/ID5"
```

An unknown processor gets an unavailable status without a storage command.
There is no retry or Jacinto filesystem fallback. The command does not upload
a helper, stage an app, change configuration or restart a service.

The [QNX SDP 6.5 df reference](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_utilities/d/df.html)
documents this format and its 1024-byte units. Counts are rounded down by `df`.
The parser accepts one headed data row, bounded unsigned counts and a mounted
filesystem. Reserved space may make used plus available smaller than total.
A full filesystem's zero available count remains valid data. Wrong units,
multiple/wrapped rows, invalid arithmetic, control bytes and oversized output
produce an unavailable status instead of a guessed zero.

The optional `storage` object contains a fixed target path, status and, when
recognized, `total_kib`, `used_kib` and `available_kib`. The parser accepts at
most 4096 output bytes and counts through 2^53-1 KiB for exact JSON integer
interchange. These are report bounds, not QNX capacity limits. Device names,
mount names and error text are discarded. Failed commands or timeouts preserve
the other captured evidence. Existing SSH timeout and transport limits remain.

This is a free-space snapshot, not a reservation or an installation-space check.
It does not estimate selected payload allocation, backup overhead or concurrent
filesystem activity. The existing compatibility report remains private and may
contain an HMI address. Review it before sharing; the separate diesel report
exporter continues to omit this new optional field.

From `source/`, after the required approval, use a verified `HEAD_IP` and a new
private output directory:

```sh
python3 tools/capture_headunit.py --head HEAD_IP --storage --out PRIVATE_NEW_DIR
```

The flag does not enable framework-binary export. Keep any separately requested
framework binaries private. Host tests use synthetic responses and mocked SSH;
no storage value or utility availability on a particular BMW is established.
Check the actual output during a separately authorized parked validation before
using it as evidence for that unit.
