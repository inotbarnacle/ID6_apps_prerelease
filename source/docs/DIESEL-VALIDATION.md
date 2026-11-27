# Read-only diesel discovery and staged vehicle validation

This procedure is for evidence collection, not a firmware approval or an
installation instruction. No physical validation of the owner's X5 F15 is
claimed. Keep the vehicle parked for diagnostics and stop immediately for ECU
warnings, head-unit freezes, communication failures or factory regressions.

## Offline preparation

The standard-library-only tool reuses the existing HSFZ probe and can be
reviewed and tested without connecting to a vehicle:

```sh
python3 tools/discover_diesel.py --plan
python3 tools/test_discover_diesel.py
```

The printed policy contains exactly two ECU identification candidates
(`22 F150`, DME `12` and EGS `18`), up to three supported-PID bitmaps
(`01 00/20/40`), and at most eleven selected standard measurements. The bitmap
continuation chain must advertise both the page and measurement. No identifier
scan, retry loop, fault clear, session change, dynamic-DID definition, actuator,
drivetrain or emissions command is included. There are at most 16 requests,
separate connections, a 200 ms gap, two seconds each for connect, initial send
and the complete receive phase, and a 45-second overall collection budget.
Heartbeat acknowledgement sends share the receive deadline and cannot extend
it. The collector reserves all six seconds plus the gap before another read.
This remains the default `standard`
profile. The tool does not use SSH or MOST.

Process existing `probe_signals.py` JSONL records without network access:

```sh
python3 tools/discover_diesel.py --replay /private/probe.jsonl \
  --headunit-report /private/headunit/compatibility.json \
  --out /private/diesel-public-report.json
```

The optional head-unit input is an existing capture, not a request to connect.
The reporter reconstructs only firmware, architecture, known processor role,
screen dimensions and both framework hashes. It does not copy arbitrary input
fields, IP addresses, hostnames, raw errors or firmware/framework binary data.
Raw F150 payloads are omitted; only a bounded three-byte SGBD index **candidate**
is retained. Its interpretation follows the current research in
[upstream PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143), which is
not an authoritative hardware/software inventory. No engine subvariant or
vehicle platform is inferred. Duplicate bitmap records are ambiguous and do
not enable measurements. Unsolicited/unadvertised measurements are omitted.

Reports use a new file with mode 0600 and refuse overwrite. Review the report
before sharing it. Replay provenance is explicitly unverified; replaying a
simulation does not turn it into a vehicle capture. A positive response is
recorded for later decoder and independent-reading checks, not declared a
verified physical measurement. Standard MAP 255 kPa is marked saturated.

### Optional standard-EGT profile

Prepare and review a separate plan when standard EGT evidence is needed:

```sh
python3 tools/discover_diesel.py --plan --profile standard-egt
python3 tools/discover_diesel.py --replay /private/probe.jsonl \
  --profile standard-egt --out /private/diesel-egt-public-report.json
```

`standard-egt` adds only bitmap `01 60` and advertised bank reads `01 78/79`.
It has at most 19 requests: the same two identification candidates, up to four
bitmap pages and thirteen standard measurements. It retains the 45-second
budget, 200 ms gaps, separate wired connections, no retries and the same
approval requirement. A slow or incomplete session may produce a partial
report. Review this exact extended plan and obtain approval for it before
adding `--profile standard-egt` to a parked collection; approval for the default
plan does not include the additional requests. Printing or replaying a plan is
offline and grants no authorization to run it.

The default plan/report retains its v1 schema and excludes EGT records, including
when unsolicited records appear in replay input. The EGT profile emits
`idrive-diesel-discovery-v2` with `profile: standard-egt` and two additional
entries in `signals`, each containing four numbered bank/sensor records.
Missing or ambiguous capabilities remain unknown. An absent bitmap bit or
continuation establishes unsupported; neither can be overridden by a positive
unsolicited temperature. Duplicate bank replies cannot establish a layout.

An EGT reply must contain exactly nine data bytes after `41 78/79`, with only
support bits 0–3 set. Each supported sensor is decoded independently from a
big-endian 16-bit value using `(raw - 400) / 10` °C. The -40..1800 °C plausibility
envelope matches the telemetry app; it is not a BMW operating limit. A valid
0 °C has `celsius: 0.0`. Absent, unknown or implausible sensors have no `celsius`
field; implausible supported values retain a distinct reason. Exact accepted
bank replies are preserved for decoder review. Malformed, overlong or reserved-
bit replies are omitted. Every decoded value still requires independent
validation. Bank/sensor numbers establish no physical sensor location, DPF
regeneration state, engine identity or current freshness.

Format/scaling evidence is the vendor's public
[CSS Electronics OBD reference](https://www.csselectronics.com/pages/obd2-dbc-file),
its [v4.5 ISO-TP archive](https://canlogger.csselectronics.com/files/dbc-files/obd-dbc-files-v4.5.zip)
and an independent [MIT decoder](https://github.com/goabonga/obd-tui/blob/106bdbeb96b69dc5a1b3b8508ff43758cf75eab2/src/obd_tui/models/exhaust.py).
The inspected archive SHA-256 is
`ab38439676c08e3f48026e37645118af2512c79650dbf0cab9cdd865fd0ae84e`;
none of its database, descriptions or sample logs is redistributed. These are
standard protocol references and synthetic tests, not N57/B47 vehicle evidence.

The v1 regression fixture under `tools/fixtures/discovery-standard-v1.json`
contains only synthetic inputs and outputs generated by the old `ac48c54`
implementation. It preserves the old default plan/report contract. Tests also
cover all support bytes, both banks, absent/valid-zero/out-of-range sensors,
bad lengths and duplicate records, privacy filtering, both fixed request
policies, continuation gating, exclusive 0600 output and the complete time
reservation at slow transaction deadlines. CI runs without vehicle access.

## Stage A — non-invasive parked discovery

1. Obtain the owner's explicit approval for the printed bounded collection
   policy. Connect wired ENET with the vehicle parked, transmission P, iDrive
   awake and adequate battery support. Close ISTA, phone OBD apps, all diagnostic
   dashboard pages and other testers; disconnect the OBDLink LX if ENET is in
   use. The project's diagnostic lease cannot exclude external devices.
2. Identify the computer's wired link-local address and the diagnostic gateway
   using the installer's existing **Find car** function. A head-unit SSH
   address is a different endpoint. No automatic broad network scan is added.
3. After that approval, substitute the verified addresses below and execute
   one collection. The flag records approval; it does not replace obtaining it.

   ```sh
   python3 tools/discover_diesel.py --collect --owner-approved-parked \
     --local 169.254.10.2 --gateway 169.254.10.3 \
     --out /private/diesel-public-report.json
   ```

4. Record ignition/engine state and independent instrument/authorized diagnostic
   readings separately. Do not run tools while moving or use a plausible
   result as proof of the identifier's meaning. `F150` alone does not establish
   exact N57 variant, DDE family, HWEL/SWFL or EGS software. Obtain those from an
   authorized inventory report, redact VINs and serials, and correlate the ECU
   address and evidence source. Do not invent additional identity DIDs.
5. Enabling head-unit SSH requires a **separate explicit approval**. If it is
   already authorized and enabled, use `capture_headunit.py` to collect hashes,
   exact firmware, architecture and LayerManager screen 0. It supports split
   Jacinto/OMAP units. Verify the SSH host key locally. Keep proprietary
   libraries private and omit `--include-frameworks` for a public report.
6. MOST availability and standalone behavior are **not tested** by the ENET
   collector. Record them as unknown until the later authorized app checks.

## Stage B — offline evidence checks

Compare each request/response with its exact decoder, byte length, units,
sentinels and independent reading. Preserve sanitized positive and negative
fixtures and test truncated, malformed, negative, timed-out and stale cases.
Record whether fixtures are physical captures or simulations. Do not read
temporary F303 as a fixed oil or DPF channel; another tester may have defined
it for another purpose. Source DIDs and scales from another N57 do not prove
applicability to this DDE. Unsupported data stays unavailable.

Use the existing native host tests and the tool tests. Contact
[QNX licensing/support](https://qnx.software/en/contact) about a legitimate
legacy SDP 6.5 entitlement, or use an existing authorized installation. The
current [public evaluation](https://www.qnx.com/products/evaluation/) supplies
SDP 8.0; it does not establish availability of the required 6.5 toolchain. Do
not acquire restricted SDKs or keys from unofficial archives. Host builds and
simulation do not prove a QNX loader/ABI check or a head-unit runtime test.

## Stage C — installer compatibility check only

With read-only SSH access explicitly authorized, select **Check compatibility**
in the existing desktop installer. Do not select installation. Record:

- Exact firmware and the live SHA-256 pair against one approved profile;
  verify active resolution independently of a matching version string.
- The HMI processor/path on split-node units, diagnostic gateway and available
  storage. Missing storage evidence remains an open check.
- Existing Plugins, names/connectors, native Android Auto/projection/HUD apps
  and potential resource conflicts. Factory CarPlay availability alone proves
  neither MOST nor HUD coexistence.
- Original app folders, preferences, checksums, backup destination, exact files
  a proposed installation would modify and restoration instructions.

Unmatched hashes, missing ABI evidence or untested runtime compatibility prevent
approval. No firmware profile is added by this tool or this procedure.

## Stage D — controlled installation (separate authorization)

Present the concrete candidate build, checksums, modified-file inventory and
backup/restore plan before requesting permission. Obtain explicit approval for
installation and separately for any head-unit restart. Preserve original state
outside Plugins and install only the minimum monitoring apps, one at a time.
Use appropriate voltage support and stay parked. Validate open/close/reopen,
worker termination, released sockets/leases, stale/unavailable display, independent
readiness indicators and bounded polling. Do not install tuning, xDrive or
experimental HUD applications as part of this diesel monitoring work.

## Stage E — factory-function regression

After each authorized parked change, record pass/fail/untested for original
iDrive menus, factory full-screen Apple CarPlay, navigation, Bluetooth, audio,
reversing camera, parking-sensor display, vehicle settings, instrument cluster,
HUD where equipped and normal startup/shutdown. A menu opening is insufficient.
Stop and restore if any regression occurs; do not delete factory HMI files or
disable security, projection or safety systems to make an app work.

## Stage F — standalone operation

After parked stability and factory checks pass, test passive monitoring with
ENET removed from OBD, laptop disconnected and OBD adapter disconnected, then
through normal ignition/sleep cycles. Record each signal and transport separately.
Current main's EGS temperature requires reachable HSFZ; a DME MOST read does not
establish transmission support. Do not force a gateway to stay awake or require
a permanently attached cable. Any later driving check is passive, with no driver
interaction with development tools and no experimental collector launch.

## Stage G — rollback

Use the installer's **Uninstall** operation for only the selected monitoring
menus; retain the timestamped backup and file inventory. Obtain authorization
for any required restart. Verify removed entries, remaining Plugins and factory
functionality. Restore an intact original folder only to its original vacant
location, verify checksums, and repeat factory checks after an authorized restart.
Removing a UI does not reverse ECU commands; this monitoring procedure issues
none. Physical rollback remains unverified until performed on the vehicle.

## X5 F15 compatibility record

| Item | Current evidence |
| --- | --- |
| Owner description | X5 F15 xDrive30d; built 14 Nov 2017; MY2018; N57 190 kW; ZF 8HP; ID6; factory full-screen CarPlay. Not independently discovered. |
| Exact N57/DDE/EGS HW and SW | Unknown; no vehicle inventory provided. |
| Firmware / both framework hashes / screen / processor | Unknown; no authorized head-unit capture. |
| Standard PID and safe report behavior | Host-tested simulation only. |
| Verified N57 fixed oil / full-range boost / DPF / EGR / SCR | Unsupported without ECU-specific evidence. |
| MOST / ENET-free operation / CarPlay coexistence | Untested. |
| QNX build / controlled installation / factory checks / rollback | Not performed. |

Label future evidence explicitly as physical vehicle, recorded diagnostic
evidence, simulation, host only, experimental or unsupported. Model year,
navigation map version and another owner's N57 results cannot fill these gaps.
