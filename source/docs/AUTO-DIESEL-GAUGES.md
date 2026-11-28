# Capability-based BoostDeck diesel defaults

A new BoostDeck installation uses **Detected defaults** when Diesel is selected.
It starts with empty gauge slots and chooses only from explicit capability
evidence reported by its current collector. This replaces the fixed nine-gauge
diesel default, which could reserve slots for unsupported signals.

Choose **BoostDeck settings → Diesel gauges → Custom layout** to use saved gauge
selections. Valid B1/B2 and extended B3/B4/B5 configuration files retain Custom mode when no
mode preference exists. The automatic layout never overwrites either profile's
saved selections. **Configure custom gauges** edits those selections; its page
explains when Detected defaults are active. Gasoline continues to use its saved
layout and ordinary telemetry packets.

## Evidence and selection

The collector inspects the existing Mode 01 supported-PID cache. A PID is known
unsupported only after its bitmap bit is absent or a preceding continuation bit
is absent. A missing, malformed, refused or lost bitmap remains unknown and
uses the existing five-second retry backoff. Unknown signals cannot occupy
automatic slots. Advertisement does not establish a valid measurement: the
existing response lengths, scaling, plausibility and freshness rules still apply,
and a refused or stale advertised reading displays `--`.

The priority is boost, oil temperature, coolant, transmission temperature, rail
pressure, intake air, one supported EGT bank/sensor, mass airflow, calculated load, control-module voltage,
fuel rate, barometric pressure and MAP. Advertised RPM is reserved for the narrow
ninth tile. Without RPM, the next available candidate may use that tile. At most
nine unique supported signals are displayed. Unfilled slots retain `--` and no
signal caption. This order is an application choice, not an N57 ECU profile.

Engine candidates use standardized PID definitions. [EGT](STANDARD-EGT.md) needs
both PID advertisement and a valid bank reply establishing sensor support.
Derived boost needs both MAP `0B` and barometric pressure `33` advertisements.
The 255-kPa absolute MAP ceiling still makes both MAP and derived boost invalid
at saturation. There is no guessed fixed or temporary diesel oil DID, DPF,
requested pressure, EGR, SCR or oil-pressure mapping in this default pool.
Optional [standard EGR gauges](STANDARD-EGR.md) remain in Custom layout and
do not change this automatic candidate pool.

Transmission is a separate EGS endpoint and has no Mode 01 capability bit here.
It enters the layout only after the existing `22 DA12` request returns a positive
reply accepted by its existing decoder in this collector lifetime. Rejection or
timeout does not prove permanent lack of ECU support and cannot populate the
tile. A successful reply still needs independent calibration/meaning validation
on the actual EGS. EGS-over-MOST remains outside this change: transmission can
go stale after transport loss, even if its earlier successful read is remembered.

The worker polls chosen supported signals plus unknown candidates needed for
bounded capability discovery. It stops polling unused, lower-priority supported
signals once nine are chosen. Existing two-second polling/retry deadlines and
diagnostic leases remain in use. Standard bitmap reads remain limited to `00`,
`20`, `40` and `60`; no identifier scanning or state-changing service is introduced.
An EGT bank is polled once for all its selected sensors, at most every two
seconds; malformed/refused reads back off five seconds.
The diesel HUD RPM-sharing path also respects PID advertisement.

## Process-local telemetry and persistence

Automatic collection uses `--auto-diesel TOKEN`, which implies Diesel. It cannot
be combined with `--demo` or a manual `--signals` mask. The token is supplied by
the plugin for that worker launch. An automatic datagram consists of a bounded
capability header (token, worker PID, monotonic timestamp, known mask, supported
mask) and its measurement frame. Legacy masks retain `NBC1`; extended evidence
uses `NBC2`. `NBG7` retains its original 31 channels, while valid extended
readings use `NBG8` with 39 channels and a mandatory final newline. Their
timestamps must match. The shared receive buffer is 1696 bytes to also accommodate
optional manual pressure frames; the UI still drains at most eight datagrams
per tick. This does not change the ECU protocol or HUD's separate sharing format.

The plugin accepts an automatic frame only while visible, from its current
worker PID/token, with a valid mask subset, a non-future timestamp less than
2.5 seconds old and no backward sample timestamp. Established capability bits
cannot disappear or flip within a worker lifetime. Restarting collection clears
capabilities and chooses a new launch token. Queued old-worker frames, ordinary
unwrapped packets and demo samples cannot populate an automatic dashboard.
Custom/Gasoline collection retains ordinary NBG7 encoding when no extended
channel is valid. Old packet parsing is retained; EGT readings use NBG8 and
valid optional EGR readings use NBG9 and new pressure readings use NBG10.
Automatic selection never requests EGR or the optional pressure groups.
Automatic mode needs the matching new plugin and collector, deployed together.

The three-byte D1 mode sidecar is `BoostDeck.signals.defaults` beside the existing
assignments file, normally under `/var/opt/hmi/data/ID5/`. It uses the existing
atomic 0600 temporary-file/rename helper. Saving a mode writes only that sidecar;
the assignment writer also preserves the current mode before writing its usual
B2 file for old IDs, B3 when an EGT is selected, B4 for EGR IDs or B5 for pressure
IDs. Failed mode saves retain the
prior mode and show an error. Invalid or newer sidecars leave the mode derived
from the existing assignments. B1/B2 keep their original ID bounds, G1/G2 are
unchanged, and EGT/EGR/pressure IDs are appended. B3/B4 retain their original
ID limits with the new pressure options. An older binary ignores D1 and reads an
old-format custom layout; it cannot interpret newer B3/B4/B5 revisions or automatic mode. Back up
the complete app and preference files before any separately authorized update.

## Offline checks and limits

```sh
make -C source/native test
python3 -m venv /tmp/idrive-defaults-env
/tmp/idrive-defaults-env/bin/pip install Pillow==13.0.0
/tmp/idrive-defaults-env/bin/python -B source/tools/test_boost_defaults_resources.py
```

Tests cover all 2,097,152 candidate subsets, unknown/unsupported/advertised states,
composite boost, EGS evidence, strict envelopes, legacy packets, actual production
UDP/plugin callbacks, old/future/stale/wrong-worker data, B1/B2 preservation,
mode persistence and failed saves. A host-only waiting child stands in for the
collector in plugin tests; no vehicle diagnostic worker is executed there.
The BMW framework fake is reused from Andreas Boehm's MIT-licensed
[PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143).

Loopback HSFZ tests exercise the real collector against synthetic bitmap,
measurement and negative replies. They verify unsupported oil is never read,
an unknown bitmap never becomes support, EGS rejection never selects transmission,
and advertisement alone never creates a valid measurement. CI adds GCC/Clang,
C++98 syntax, static analysis, ASan/UBSan and generated-resource checks. Dashboard
geometry and image IDs are preserved; the new mode controls use native text.

These are host/simulation results. Exact engine/DDE/EGS identity, actual signal
meaning, target firmware hashes, QNX build/loader behavior and native HMI runtime
remain unverified for the owner's X5. No vehicle connection, installation or
head-unit restart is authorized by this feature. See [diesel validation](DIESEL-DEVELOPMENT.md)
for outstanding scope and the separate parked-validation milestone.
