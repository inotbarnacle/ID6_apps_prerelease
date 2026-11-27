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

### Optional EGR and EGT evidence profile

The existing 16-read `standard` and 19-read `standard-egt` plans retain their
request sets, ordering and v1/v2 report contracts. A third, separately selected
`standard-diesel` plan adds EGR evidence while also retaining EGT and the
original standard channels:

```sh
python3 tools/discover_diesel.py --plan --profile standard-diesel
python3 tools/discover_diesel.py --replay /private/probe.jsonl \
  --profile standard-diesel --out /private/diesel-public-v3.json
```

Its limit is **22 requests**: two F150 candidates, up to four supported-PID
pages and sixteen measurements. After identification and capabilities, the
measurement order begins with `01 69`, `01 2C`, `01 2D`, then `01 78/79` and the
original eleven measurements. This lets a slow, partial session prioritize
the explicit EGR A/B layout. The same 45-second budget, six-second transaction
reservation, 200-ms gaps, separate wired connections and no-retry rule apply.
Every measurement still needs its advertised capability. No engine family is
inferred from choosing the profile.

Print and review this exact plan, then obtain explicit approval for it before
adding `--profile standard-diesel` to any parked collection. Permission for an
older plan does not include these additional requests. Planning, replaying and
passing an approval flag do not grant physical-vehicle authorization.

The `idrive-diesel-discovery-v3` report carries `profile: standard-diesel` and
all sixteen signal entries. EGT sensors retain their prior layout. EGR entries
keep unspecified legacy sources separate from A/B fields:

| PID | v3 fields | Interpretation |
| --- | --- | --- |
| `2C` | `system: unspecified`, `quantity: requested`, optional `percent` | Exact one-data-byte request value, raw × 100 / 255. |
| `2D` | `system: unspecified`, `quantity: error`, optional `percent` | Exact one-data-byte error, (raw − 128) × 100 / 128. Not actual position. |
| `69` | `fields` array: A request/actual/error, then B request/actual/error | Exactly seven data bytes; six independent support flags in the first byte, then six values. |

PID 69 rejects reserved flag bits 6/7. Each supported request/actual value uses
raw × 100 / 255 percent; each error uses (raw − 128) × 100 / 128 percent.
Unsupported, unknown, malformed or ambiguous fields have no `percent` property.
A supported zero retains `percent: 0.0`. A/B actual is reported normalized
duty/position; the reporter never reconstructs it from legacy request/error,
labels it as independently measured flow, or infers hardware position, a
regeneration state or an emissions fault. Every decoded value still requires
independent validation.

Protocol/scaling provenance and limitations match the independently implemented
[native EGR decoder documentation](https://github.com/voioo/iDriveDeck/blob/5f5e35f688f3536434f8d50607bc9b8c231afcd2/source/docs/STANDARD-EGR.md):
the vendor's public CSS reference for scalar conversions, the
[pinned OBDb definitions](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/signalsets/v3/default.json)
for the complete PID 69 layout, and Bosch's semantic labels. OBDb's related
Pelican documentation is the same vendor source, not a second independent
confirmation. No vendor database entries, descriptions or logs are vendored;
the arithmetic and synthetic fixtures are project source. No exact N57/B47
support, ECU identity or QNX/physical result is established.

V3 also adds a fixed `record_status` for each signal:

| Value | Input-record evidence |
| --- | --- |
| `not_present` | No allowlisted record for this ECU/request. |
| `ambiguous_duplicate` | Multiple records; none is selected as authoritative. |
| `present_no_valid_reply` | A record exists but supplies no valid hex reply bytes. |
| `present_reply_bytes` | Hex bytes exist; this does not assert correct format, capability, value or physical provenance. |

These states describe the input stream, not why a read was skipped or proof that
a physical request occurred. Capability and measurement validation remain
separate. A 45-second partial collection can therefore distinguish uncollected
fields from valid zero without exporting raw error strings. Accepted exact
EGR replies are retained; malformed or unadvertised positives are omitted.
Negative response codes remain separate evidence and do not establish support.

The frozen `tools/fixtures/discovery-egt-v2.json` contains synthetic inputs and
old outputs from `c7ab86b`; the original v1 fixture remains unchanged. Tests
compare both old contracts, cover every EGR support/raw byte, unknown and
unsupported capabilities, per-field absence, duplicate evidence, wrong ECU and
format, privacy filtering, exact request policy, EGR priority during a partial
session, offline paths and exclusive 0600 output. Replay provenance remains
unverified and output still needs local review before sharing.

### Optional requested/actual pressure evidence profile

The separately selected `standard-pressure` profile adds `01 6D` and `01 70`
to the EGR/EGT evidence set. The original 16/19/22-read plans retain their exact
requests, ordering and v1/v2/v3 report contracts. This new plan is at most
**24 requests**: two identification candidates, up to four bitmap pages and
eighteen measurements. After capabilities, pressure is read first (6D, 70),
then EGR (69, 2C, 2D), EGT (78, 79) and the original eleven measurements.

```sh
python3 tools/discover_diesel.py --plan --profile standard-pressure
python3 tools/discover_diesel.py --replay /private/probe.jsonl \
  --profile standard-pressure --out /private/diesel-public-v4.json
```

The existing 45-second budget, 6.2-second reservation for connect/send/receive
and gap, 200-ms minimum gaps, separate explicit wired connections and no-retry
rule apply. Unknown or unadvertised PIDs are never probed. A slow session can
end after one pressure reply; `record_status` distinguishes an uncollected
record from one with reply bytes, independently of field support and decoding.
No broader scanning, MOST, SSH, session change or write is added.

Review the printed 24-read plan and obtain the owner's explicit approval for
that exact plan before any physical use. An earlier plan's approval does not
cover these additional requests. Planning, replaying or supplying an approval
flag does not grant permission. No physical collection was performed here.

The `idrive-diesel-discovery-v4` report identifies `profile: standard-pressure`
and retains the EGT/EGR layouts and record statuses. Each new PID contains four
`fields`, in order A requested, A actual, B requested, B actual:

| PID | Exact reply | `pressure_reference` | `unit` / optional `value` |
| --- | --- | --- | --- |
| `6D` | `41 6D` plus 11 data bytes | `relative_to_atmosphere` | `bar`, unsigned big-endian raw / 10 |
| `70` | `41 70` plus 10 data bytes | `absolute` | `kPa`, unsigned big-endian raw / 32 |

Support bits 0, 1, 3 and 4 respectively authorize those four fields. Bits 2/5
describe unused temperatures or control status; their bytes are not interpreted.
High support bits are rejected. PID 6D uses B–C / D–E / G–H / I–J pressure
bytes; PID 70 uses B–C / D–E / F–G / H–I (A is the support byte). A field has a
numeric `value` only with complete, advertised, unambiguous evidence and its
own support bit. Supported zero and FFFF retain their defined conversions.
The report does not apply the dashboard's graphic scale as a plausibility limit.

Requested is ECU-returned data, not a pressure command. Actual is never filled
from requested, legacy rail PID 23, MAP or barometric pressure. No atmospheric
default or subtraction is applied to absolute boost. A/B names do not establish
physical rail/turbo identity, sensor location, manifold pressure or accuracy.
Injection-control pressure PID 6E and inlet-pressure PID 6F are excluded.
Values retain `decoded_requires_independent_validation`; exact N57 support,
independent comparison and QNX/physical behavior remain unverified.

Protocol facts and their limits match the independently implemented
[native pressure documentation](https://github.com/voioo/iDriveDeck/blob/d0b7808a0fc86260eb1ebd54874de30211bde3c7/source/docs/STANDARD-PRESSURE.md).
The vendor's
[pinned OBDb reference](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/signalsets/v3/default.json)
supplies the full layouts and rail atmospheric reference. Independent
[preOBD encoder tests](https://github.com/preobd/preOBD/blob/45b371e2fd0edf0001971c0768754c237de7b2ce/test/test_obd_encode/test_obd_encode.cpp)
corroborate PID 70's length, actual-A support/offset and absolute conversion
(raw 6442 is 201.3125 kPa, including quantization). Its simulated fallback
atmosphere is not adopted. CSS v4.5 placeholders are not complete pressure
decoder evidence. No vendor database entries, descriptions or logs are copied;
independent arithmetic and synthetic fixtures are project source.

`tools/fixtures/discovery-diesel-v3.json` freezes synthetic inputs and outputs
from unchanged `35f6f00`, supplementing the prior v1/v2 fixtures. Tests retain
all three older contracts and reject unsolicited pressure evidence in them.
New cases cover units/reference, every support byte, unsigned boundaries,
exact prefix/length, absent actual, missing/duplicate evidence, private fields,
wrong ECU, negative replies, the exact 24-read policy, budget priority and
offline/exclusive 0600 output. Replay provenance remains unverified; locally
review every report before sharing it.

## Optional standard DPF pressure and temperature evidence profile

`standard-dpf` is a separate **27-read maximum** plan and v5 report. The earlier
16/19/22/24-read plans and v1/v2/v3/v4 reports remain unchanged. It adds only
standard `01 7A`, `01 7B` and `01 7C`, after advertisement through the existing
00/20/40/60 continuation chain. Measurements are prioritized as DPF 7A/7B/7C,
pressure 6D/70, EGR 69/2C/2D, EGT 78/79, then the original eleven channels.
The unchanged 45-second budget, full 6.2-second transaction reservation,
200-ms gaps, explicit wired endpoints and no-retry rule still apply.

Planning and replay remain offline:

```sh
python3 tools/discover_diesel.py --plan --profile standard-dpf
python3 tools/discover_diesel.py --replay /private/dpf-probe.jsonl \
  --profile standard-dpf --out /private/dpf-public-report.json
```

Review this exact plan and obtain separate explicit owner approval before
any physical collection. An earlier profile's approval does not cover it.
No physical use is authorized or performed by this source contribution.

The v5 report identifies `profile: standard-dpf`. It retains the earlier
standard fields and fixed `record_status` values, adding ten optional DPF
`fields` across three PIDs:

| PID | Data after `41 <pid>` | Fields in wire order | Conversion |
| --- | --- | --- | --- |
| 7A | Exactly seven bytes | Bank 1 delta, inlet, outlet pressure | Signed big-endian delta / 100 kPa; unsigned inlet/outlet / 100 kPa |
| 7B | Exactly seven bytes | Bank 2 delta, inlet, outlet pressure | Same conversions, separate bank |
| 7C | Exactly nine bytes | Bank 1 inlet/outlet, bank 2 inlet/outlet temperature | Unsigned big-endian raw / 10 - 40 C |

A is the support byte. Pressure bits 0/1/2 authorize B-C/D-E/F-G respectively;
temperature bits 0/1/2/3 authorize B-C/D-E/F-G/H-I. Reserved support bits reject
the layout. Each numeric `value` requires an exact positive reply, valid PID
advertisement and its own field support. A missing field stays unavailable;
delta is never computed from inlet/outlet, and generic EGT is never used as a
DPF inlet/outlet fallback. Bank/position names preserve protocol labels and do
not establish the hardware fitted to this vehicle.

Fields carry `bank`, `quantity`, `unit`, `capability` and `measurement`.
Pressure uses `kPa`; temperature uses `celsius`. Delta has
`pressure_reference: differential`; inlet/outlet use `not_established`, since
the inspected references do not establish an absolute or atmospheric reference.
No atmospheric value is assumed or subtracted.

This evidence report preserves the full protocol domains: delta -327.68 to
327.67 kPa, inlet/outlet 0 to 655.35 kPa, temperature -40 to 6513.5 C. Supported
zero remains numeric. FFFF means -0.01 kPa delta, 655.35 kPa inlet/outlet or
6513.5 C temperature; it is not an invented unavailable marker. These ranges
are protocol arithmetic, not BMW plausibility limits. Values remain
`decoded_requires_independent_validation` and are not promoted to application
readiness or a physical compatibility claim.

[DashLogic's published PID reference](https://www.dashlogic.com/docs/technical/obdii_pids)
independently corroborates the layouts, per-field flags, signed delta and
temperature conversion in the previously inspected
[pinned OBDb catalog](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/signalsets/v3/default.json).
The German Environment Agency's
[exhaust-emissions research report](https://www.umweltbundesamt.de/system/files/medien/1410/publikationen/2020-01-29_texte_22-2020_fortentwicklung-abgasuntersuchung_kurz_en.pdf)
also identifies these standard DPF pressure/temperature PIDs. None establishes
this X5's support. No vendor tables, descriptions, captures or proprietary
standard are copied; the implementation uses protocol arithmetic and synthetic
fixtures.

Pressure and temperature alone do not establish regeneration status, soot/ash
mass, time/distance since regeneration, inhibition reasons, completion or a
service threshold. No such estimate, regeneration command, counter reset,
dynamic DID, session change or new native app is added. PID 8B and other higher
pages remain outside this plan. Exact DDE support, independent measurements
and the shared B47/N57 evidence requested in #196 are still needed before a
DPFDeck implementation decision.

The 15 added discovery cases cover the entire signed/unsigned 16-bit domains,
every support byte, exact prefix/length, no fallback/inference, all four frozen
earlier contracts, missing/duplicate/negative evidence, privacy, wrong ECU,
exact request/gap/budget bounds, no retries, offline planning/replay, explicit
collection approval and exclusive 0600 report output. A complete 27-read run
also exercises the real HSFZ exchange through local stream socket pairs,
including heartbeat acknowledgement, pending and wrong-ECU/PID replies and
split frames; those pairs cannot reach a vehicle or external network. The new
`tools/fixtures/discovery-pressure-v4.json` freezes synthetic v4 output from
unchanged source `f18cf1c`; it contains no vehicle data.

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
