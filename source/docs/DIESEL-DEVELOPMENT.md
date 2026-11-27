# Diesel compatibility investigation

Audit baseline: upstream `8567abaadd1305965b3ad39e3222575cd49c507d`,
27 November 2026. Source is MIT licensed. No additional contribution template or
CI workflow was present at that baseline. Follow `source/AGENTS.md` and the host
checks in [BUILD.md](BUILD.md). Changes below are source contributions; published
application ZIPs are separate maintainer build artifacts.

Publication update, 27 November: the configured 1Password signer is available
again. The DPFDeck HMI and optional status commits are signed, with final source
`7a27e22` pushed to the fork and exact-source CI running. The aftertreatment,
SCR and private-identity source commits are signed and being combined into
#201; the reagent gauge commit is signed for #202. Earlier references below
to waiting for the signer describe their local checkpoint at that time.

## Current gaps and overlapping work

| Area | Current evidence and gap | Action |
| --- | --- | --- |
| Native HMI / RHMI | C++98 plugins use private framework ABI declarations and generated XML/image archives. Full-screen layouts and workers are separate compatibility concerns. | Keep ECU changes independent of ABI/layout changes; no new firmware approval. |
| QNX / renderer | `native/build.sh` requires licensed SDP 6.5 and private framework link inputs. Framework fingerprint comparison checks static imports, not runtime behavior. | Host validation first. [PR #208](https://github.com/inotbarnacle/iDriveDeck/pull/208) replaces the archive/key-generator recommendation with official acquisition channels. Legacy availability and entitlement need QNX confirmation; the current SDP 8 evaluation does not establish either. |
| HSFZ / gateway | Shared link filters target ECU and response prefix, bounds replies and deadlines, and discovers a unique wired link-local gateway. | Reuse the link and diagnostic lease. Do not scan arbitrary addresses or DIDs. |
| MOST / standalone | Current MOST tunnel accepts DME only; EGS transmission temperature requires HSFZ. A worker that has connected HSFZ does not subsequently switch to MOST. | Coordinate with [PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143); do not duplicate its multi-ECU tunnel work. |
| Ownership / lifecycle | Loopback leases coordinate project workers, not external tools. The original reapers can forget a live PID on `ECHILD`, permitting duplicate workers after retry deadlines and losing the PID needed for close. | Reuse PR #143's existing process-existence fix. [Fork test review #1](https://github.com/voioo/iDriveDeck/pull/1) adds production-plugin/baseline/owned-child regressions. Hidden polling changes in #143/#50 and actual vehicle cleanup remain separate. |
| Diesel telemetry | Explicit Diesel selection substitutes standard OBD coolant, IAT, rail and boost for unverified gasoline DIDs. Only fuel-rate polling currently checks a supported-PID bitmap. No automatic ECU-specific verified N57 profile exists on main. | Add shared capability checks first. Preserve fixed gasoline decoders and persisted signal IDs. |
| Oil temperature | [#21](https://github.com/inotbarnacle/iDriveDeck/issues/21), [#162](https://github.com/inotbarnacle/iDriveDeck/issues/162): DDE741 rejects fixed reads and does not advertise PID 5C. Temporary F303 is reused for different signals. | Leave unavailable without a verified fixed source. Never define or poll somebody else's dynamic DID. |
| Pressure | PID 23 is direct-injection rail gauge pressure, 0.1 bar per count. MAP PID 0B is limited to 255 kPa absolute; subtracting barometric pressure does not extend that range. The original graphic fills at only 250 bar. | #202 blanks saturated diesel MAP/boost and adds optional PID 6D rail gauge / PID 70 absolute boost request/actual fields. Physical support remains unverified. [Draft #205](https://github.com/inotbarnacle/iDriveDeck/pull/205) changes the original rail channel's display scale only. |
| WarmDeck | Three temperatures expire independently and trend estimates require all three. Main queries oil unconditionally and displays generic RPM guidance. Thresholds 85/75/40 C are application choices. | #202 gates oil by advertised support. [Draft #209](https://github.com/inotbarnacle/iDriveDeck/pull/209) adds independent target states, measured estimates and advisory vehicle-guidance text, with production UDP/UI and resource tests. |
| ShiftDeck | Source artwork/preferences retain five gasoline targets while the latest installer release advertises 6000–8000 RPM. Neither establishes an N57 limit. | [Draft #203](https://github.com/inotbarnacle/iDriveDeck/pull/203) adds manually selected diesel targets/display scales, legacy settings migration and asset/plugin tests. No universal diesel redline or physical validation is claimed. |
| Faults | Worker reads 24-bit UDS codes; the old UI discarded status availability and always used a reference gasoline table. | [Draft #204](https://github.com/inotbarnacle/iDriveDeck/pull/204) preserves status support, adds a DDE/raw view and clear confirmation, plus separate standard confirmed/pending/permanent emissions reads. The counted CAN format is host-tested; target support and licensed DDE descriptions remain open. |
| DPF | [#196](https://github.com/inotbarnacle/iDriveDeck/issues/196) contributor plans B47 collection once ENET arrives. #201 provides standard pressure/temperature evidence collection; separate DPFDeck source now includes HMI, resources, lifecycle and optional installer integration. | Publish the focused draft after exact-source CI. A licensed QNX build and shared captures remain required for validated B47/N57 profiles. No guessed DIDs, soot/ash thresholds or regeneration controls. |
| Installer / rollback | Desktop installer checks framework pairs and display layouts, inventories/stages managed folders and preserves backups; uninstall is documented in `Installer.md`. | Run compatibility check only after authorized vehicle discovery. No installation or restart is authorized yet. |
| Factory integration | Native Android Auto installation currently disables gauge MOST fallback; [PR #138](https://github.com/inotbarnacle/iDriveDeck/pull/138) proposes a startup process check. This does not establish CarPlay/HUD coexistence. | Keep current guards pending reproducible evidence and maintainer review. |

## Historical issues, checked 27 November 2026

| Issue | Current status | Interpretation |
| --- | --- | --- |
| [#20](https://github.com/inotbarnacle/iDriveDeck/issues/20) | Closed as duplicate of #21 | Not a resolved N57 compatibility claim. |
| [#21](https://github.com/inotbarnacle/iDriveDeck/issues/21) | Open | DDE741 dynamic oil/rail evidence; not a verified fixed read-only mapping. |
| [#36](https://github.com/inotbarnacle/iDriveDeck/issues/36) | Open | F16 HUD protocol rejection, split-node and diesel pressure limitations. Framework matching alone cannot approve HUD use. |
| [#68](https://github.com/inotbarnacle/iDriveDeck/issues/68) | Open | N57/B47 owners still report blank telemetry. Its drivetrain-control request is outside this effort. |
| [#105](https://github.com/inotbarnacle/iDriveDeck/issues/105) | Open | Freeze cause unconfirmed; maintainer requests matching firmware and watchdog/core evidence. No factory-file cleanup or firmware flash is justified. |
| [#162](https://github.com/inotbarnacle/iDriveDeck/issues/162) | Open | Unsupported oil PID and intermittent transport/lifecycle failures. Owner corrected the ENET dependency report: the failure also recurs with ENET connected. |
| [#196](https://github.com/inotbarnacle/iDriveDeck/issues/196) | Open | Contributor replied on 27 November: no implementation branch; B47 read-only collection planned with #201's sanitized format after ENET hardware arrives. No maintainer approval or verified mapping inferred. |

[PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143) is open and proposes
SGBD-specific fixed signals, EGS-over-MOST and lifecycle fixes. Its diesel rows
are reported as not vehicle-tested. [PR #157](https://github.com/inotbarnacle/iDriveDeck/pull/157)
is now closed without a merge and is superseded by
[draft #211](https://github.com/inotbarnacle/iDriveDeck/pull/211), reviewed at
`8ae065ea74e5bfe1defe334ce546598af12895cc`. The subsequent
`dcafa161b5e37fb4cb04d04f5b10d15b432e8ed7`
changes the narrow gauge layout and its tests; the diagnostic reader, codecs
and assignment functions examined below are unchanged. That candidate adds an
explicitly
pinned N57 dynamic-DID path and PowerDeck, attributes the worker-ownership fix
to #143 and adapts the rail scale from #205. Its source uses temporary F303
clear/define/read transactions; pinning an SGBD index does not establish this
X5's complete DDE hardware/software identity or a fixed read-only oil source.
Its reported QNX/emulator results use synthetic ECU data and are not validation
performed by this effort or on the owner's X5.

An isolated local review compiled the unchanged candidate reader and both
branches' codecs/settings code against #202 source `b9bbdd8`. It passed 14
in-memory lifecycle cases (also with ASan/UBSan), 13 cross-codec cases and 11
cross-settings checks. The candidate's own native N57 test also passes. These
are additional host observations, outside the published CI counts; no vehicle
transport was linked into the lifecycle model.

| Review item | Reproduced observation and implication |
| --- | --- |
| Temporary definition | The reader sends clear, define and read, with no post-read restoration. In the model, successful oil/pair reads retain the new F303 definition; a refused define after a successful clear leaves it empty. Read errors preserve output variables but retain the new definition. These describe the callback sequence, not this X5's ECU behavior. |
| Lease and teardown | Identity/read groups hold a project lease, and late MOST timeouts retain ownership until tunnel teardown. The inspected `diag_link_end`, drop and close paths release transport resources without restoring F303. A project lease cannot exclude an external diagnostic client. |
| Packet meaning | IDs 31–38 overlap #202's standard EGT channels. The candidate accepts a #202 `NBG8` packet with ID 31 and value 42 as **DPF soot, 42 g**, although the sender means **EGT bank 1 sensor 1, 42 C**. All eight EGT IDs are accepted with different meanings. The common legacy RPM control passes; the tested NBG9/NBG10/NBGA cross-formats are rejected. |
| Saved assignments | Legacy B1/B2 controls load in both branches. Extended B3/B4/B5 formats differ in length/layout: the tested cross-branch files fall back to defaults rather than migrate. Matching revision labels alone do not establish compatible settings. |

Source anchors: [reader and identity check](https://github.com/FreZZZeR/ID6_apps_prerelease/blob/8ae065ea74e5bfe1defe334ce546598af12895cc/source/native/src/common/n57.c),
[transport lifecycle](https://github.com/FreZZZeR/ID6_apps_prerelease/blob/8ae065ea74e5bfe1defe334ce546598af12895cc/source/native/src/common/diag_link.c),
[codec](https://github.com/FreZZZeR/ID6_apps_prerelease/blob/8ae065ea74e5bfe1defe334ce546598af12895cc/source/native/src/common/gauge_core.c)
and [assignments](https://github.com/FreZZZeR/ID6_apps_prerelease/blob/8ae065ea74e5bfe1defe334ce546598af12895cc/source/native/src/plugins/boostdeck_assignments.h).
The candidate's [NOTICE](https://github.com/FreZZZeR/ID6_apps_prerelease/blob/8ae065ea74e5bfe1defe334ce546598af12895cc/source/third_party/nbtevo-diag/NOTICE)
attributes its catalog to Apache-2.0 `nbtevo-diag` commit
`4e85c42952b3edb89cf71583de5dba98810219e4` and factory-derived tables. That
[catalog](https://github.com/FreZZZeR/nbtevo-diag/blob/4e85c42952b3edb89cf71583de5dba98810219e4/nbtevo_diag/signals.py)
labels the N57 signals as candidates requiring independent vehicle validation.
This review establishes the attribution chain, not exact-ECU applicability or
permission to redistribute the underlying factory-derived data.

Combining these features needs agreed signal IDs, unambiguous wire schemas,
explicit settings migration and matched collector/plugin/resources. No #211
code, definitions, binaries or firmware approval is imported here. Dynamic-DID
ownership, conflicts, cleanup/restoration, data provenance and exact ECU
applicability remain subject to separate review and owner authorization.

## Target vehicle evidence matrix

Owner-supplied description: X5 F15 xDrive30d, production 14 November 2017,
2018 model year, N57 190 kW, ZF 8HP, NBT Evo ID6, factory full-screen CarPlay.
This description is not diagnostic confirmation.

| Item | Evidence level | Remaining evidence |
| --- | --- | --- |
| Exact engine subvariant, DDE HW/SW and EGS | Unverified | The local six-read standard identity tool can collect private HW/SW evidence after separate approval; an authorized inventory, record-codec review and independent corroboration are still required. |
| Firmware, framework hashes, architecture, active screen | Unverified | Authorized read-only head-unit capture. No approved F15 profile added. |
| Gateway, HSFZ, MOST, standalone operation | Unverified | Parked transport observations with equipment attached and detached. |
| Standard OBD signals | Simulation / host tests only | Supported bitmaps and independently compared readings from this vehicle. |
| Standard EGT bank/sensor channels | Simulation / host tests only | Exact DDE support for PID 78/79, sensor bits, physical sensor locations and independently compared temperatures. No regeneration inference. |
| Standard EGR request, actual and error channels | Simulation / host tests only | Exact DDE support for PID 2C/2D/69, per-field bits and independent comparisons. Actual is reported normalized duty/position, not inferred flow or a value reconstructed from error. |
| Standard requested/actual rail and boost | Simulation / host tests only | Exact DDE support for PID 6D/70, per-field bits, physical A/B identity and independent comparisons. Rail is gauge pressure; boost is absolute. |
| Standard DPF bank pressure and inlet/outlet temperature | Simulation / host tests only, discovery #201 and separate DPFDeck source app | Exact DDE support for PID 7A/7B/7C, field support, physical layout and independent comparisons. QNX, firmware and parked validation remain open; no regeneration, soot or ash mapping is established. |
| Standard DPF regeneration progress/type | Simulation / host tests only, local optional DPFDeck follow-up | Exact DDE advertisement and independent progress/type comparisons for PID 8B. Hidden by default, with no threshold, completion estimate, command or physical N57 support claim. |
| Standard SCR reagent/inducement evidence | Simulation / host tests only, local v6/v7 discovery and optional native PID 85 gauges | Exact DDE advertisement and independent comparisons for PID 85/88. Current state, causes, history and counters remain separate; no SCR health, remaining-range or native vehicle compatibility claim. |
| Standard emissions fault codes | Simulation / host tests only | Confirm that the exact DDE/gateway supports services 03/07/0A and the counted CAN reply format. A category's zero count does not clear other categories. |
| Fixed BMW diesel, DPF, SCR, oil pressure | Unsupported in this effort | Credible ECU-specific definitions and repeatable sanitized captures. Standard EGR support does not establish a BMW-specific mapping. |
| Physical installation / factory regression / rollback | Not performed | Separate owner-authorized milestone; staged plan in [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201). |

## Progress and next actions

- The first local DPFDeck HMI checkpoint includes the native six-tile dashboard, bank/units OPTIONS,
  production callback/UDP ownership tests, original resources and optional
  installer packaging/rollback integration on the separate
  [fork branch](https://github.com/voioo/iDriveDeck/tree/diesel/dpfdeck-standard).
  Publication is awaiting the configured 1Password signer, then exact-source
  CI and a focused upstream draft. No QNX build, native archive, approved
  firmware or physical X5 validation is claimed.

  Only advertised standard 05/7A/7B/7C are read after supported-PID bitmaps.
  The capability helper and private host receiver allocator remain identical
  to #202/#213. Signed pressure, field flags, conservative sample ages and
  independent unknown/unsupported/unavailable/stale/valid states are preserved.
  The 1800 C app envelope is not a BMW/regeneration limit; #201 retains full
  protocol-domain evidence. Regeneration stays unknown; soot, ash and last
  regeneration stay unavailable. No control or completion estimate is added.

  DPF1 is separate from BoostDeck IDs/wire/settings. Ports 42867/42868/42869
  retain data/activity/MOST roles and HUD handoff checks. The HMI checks live
  owned PID/token, source address/port, monotonic age, sequence and generation,
  drains at most eight packets and retains the worker across OPTIONS. It
  requests stop once and reuses #143's process-existence ownership rule.
  Actual worker-parent death and socket release are exercised by a Linux
  guardian. A project lease still cannot exclude an external tester.

  DPFDeck is an unchecked optional installer selection, enabled only with a
  separately staged payload. Wide 1280×480, 1440×540 and 1920×720 geometries
  are supplied; compact/unknown panels are rejected before mutation. Defaults
  and firmware profiles are unchanged. The packager checks ARM ELF/resource
  shape and emits sanitized checksums, not ABI/vehicle approval. Explicit host
  installer fixtures disable DPF installation and block release builds. The
  marker now precedes the first synthetic native file and survives both initial
  and subsequent interrupted staging.

  Local validation passes ten native programs, 96 Python transport/regression
  cases, seven resource/package cases and all 26 top-level Go installer tests.
  Core/plugin ASan/UBSan, static analysis, C++98 syntax and the comma-decimal
  protocol check pass. The plugin's 45 host BMW imports are covered by the
  existing Warm/Boost import union; this is not QNX ABI proof. A labeled
  [host preview and validation/rollback guide](https://github.com/voioo/iDriveDeck/blob/diesel/dpfdeck-standard/source/docs/DPFDECK.md)
  are included. Combining #211 requires reviewing its changed MOST ownership
  name map/ports; connector 123 avoids its PowerDeck connector 121.
  Two additional real-worker handoff cases pass both on the standalone DPF
  source and the combined diesel source: DPF/Boost make progress against one
  loopback gateway, requests are serialized while both are live, and Boost
  continues after DPF terminates or is killed. Activity/lease sockets release
  after exit. These are host observations, not physical coexistence evidence.

- A local `diesel/dpf-regeneration-status` follow-up adds an optional standard
  8B status display, hidden by default. The default worker still has no 80/8B
  reads. Showing status follows all continuation bits, then checks advertised
  8B support through a separate bitmap-80 cache; the shared four-page helper
  and other apps' policies remain unchanged. Progress and type flags are
  independent. False progress means not in progress, while type alone cannot
  assert progress. Only fresh supported fields show Active or Passive; no
  bank-specific status, completion estimate, alert or control is invented.

  DPF2 appends fields 11/12 without changing earlier meanings. A frozen DPF1
  fixture from published `6f9176c` checks legacy reads, with new fields unknown.
  Strict D2 settings preserve bank/units from D1 with status hidden. Changing
  the request set stops the old worker once and retains ownership until its
  exit is established before relaunching with a new token. Save failures keep
  the prior choice and worker. Default and optional-status host previews use
  actual production model callbacks and remain labeled synthetic.

  Local checks pass eleven native programs and 107 distinct Python cases
  (the full 106-case suite plus the new optional-status coexistence case),
  seven resource/package cases and seven focused DPF installer tests.
  Core/plugin/capability ASan/UBSan, static analysis, C++98 syntax and a comma
  decimal locale pass. All eight captured dashboard states fit their labels.
  Comparing host objects built with the same flags confirms an unchanged BMW
  framework import set; this is not QNX ABI proof.
  The additional real-worker overlap test observes bitmap 80 and PID 8B while
  Boost continues, at most one in-flight request, and continued Boost reads
  after DPF exits. This remains host evidence, not physical coexistence proof.
  A new isolated combined tree preserves the earlier integration checkpoint
  and passes 43 native programs and 305 distinct Python cases: 175 native-suite
  cases plus 134 tool/resource cases, with four shared fault-resource cases
  counted once. All fourteen production plugins pass strict GNU C++98 syntax.
  The shared capability helper and private UDP allocator remain byte-identical
  across the standalone and combined sources. Publication still awaits
  the configured signer and exact-source CI. No QNX or vehicle result is added.

- A local `diesel/reagent-telemetry` follow-up adds optional BoostDeck custom
  gauges for PID 85 reagent level, average consumption and average demand.
  IDs 55–57 append to the existing 0–54 meanings. Detected defaults and older
  selections keep their request sets. A separate bitmap-80 cache follows the
  complete continuation chain, while the shared four-page helper is unchanged.
  Full ten-byte replies and independent bits 2/0/1 are required. Valid zero
  and the full raw domains remain data, with no inferred range, SCR health,
  warning or DPF state and no new alerts or controls.

  New data uses NBG11/58 fields; NBG10 remains 55 fields/1536 bytes and has a
  frozen fixture from published `0c2938a`. B6 retains the old assignment file
  size and allows IDs below 58; B1–B5 retain their own bounds and old-only
  layouts keep their older revision. Current support flags, two-second actual
  request periods, five-second failure backoff and conservative 3.5-second
  ages are exercised. The native callback test verifies B2–B6 migration,
  zero/stale states, units and precision. A labeled host preview uses actual
  captured model updates, production XML/images and host fonts; its extreme
  value checks text width and visual clamping, not a vehicle measurement.

  Standalone checks pass 19 native programs, 140 Python cases and nine resource
  cases. New core/capability/plugin sanitizers, static analysis and C++98 syntax
  pass; the BMW framework import set is unchanged on the host. The focused
  combined tool suite passes 148 cases. A real-worker overlap case observes
  DPF PID 8B and reagent PID 85 under one serialized gateway lease, followed by
  continued reagent reads after DPF exits. The complete combined native run
  passes 46 programs and 186 Python cases; together with the 148 tool cases,
  that is 330 distinct Python cases (four shared fault-resource cases counted
  once). All fourteen production plugins pass strict GNU C++98 syntax. No
  QNX, exact-ECU or physical result is claimed.

  A separate host reproduction found an inherited numeric-locale constraint
  at published `0c2938a`: canonical Boost packets parse in the C locale, but
  those same bytes fail under a comma-decimal process locale. This is not an
  observation of the actual head unit. The shared codec needs an independent
  fix and regression coverage before that environment can be claimed to work;
  the new reagent guide records the limitation.

- A local `diesel/scr-evidence` follow-up adds the optional `standard-scr`
  31-read/v7 profile. Only advertised PID 88 is added to v6, prioritized before
  8B/85. All six earlier plans, allowlists and report contracts remain frozen;
  a synthetic v6 fixture records source tree `65afc573` before this change.
  The same 45-second budget, complete-transaction reserve, 200-ms gap,
  no-retry policy and separate parked approval gate apply. Nothing has been
  sent to the owner's vehicle.

  The exact thirteen-byte payload keeps current inducement-active and cause
  bits separate from four historical cause blocks and five distance counters.
  It has no per-field support flags: valid false/zero remains supported data.
  Reserved current-state bits, malformed lengths/prefixes, unknown support,
  duplicates and wrong ECUs cannot authorize values. Full unsigned counters
  are retained as evidence, with values above the documented 10000-km block
  range explicitly flagged. No ratio, remaining range, odometer or DPF state
  is inferred. Manufacturer and pinned OBDb layouts agree with the official
  J1699-3 archive's thirteen-byte shape and five independent counter offsets;
  its reference implementation is neither copied nor executed.

  All 90 discovery cases (77 prior plus 13 new), four probe and six head-unit
  capture cases pass. New cases cover all current/history status bytes, the
  full counter domain, endian offsets, privacy, frozen contracts, actual
  31-read HSFZ framing over local stream pairs and slow/partial sessions.
  The combined tool/resource suite passes 147 cases. Its native files are
  unchanged from the prior 43-program/175-case native run, giving 318 distinct
  Python cases across that evidence (four fault-resource cases counted once).
  This is local host evidence; signing, exact-source CI and exact-ECU/physical
  validation remain open. No native signal, firmware profile or control is added.

- A separate local `diesel/aftertreatment-evidence` branch now implements an
  opt-in `standard-aftertreatment` 30-read/v6 evidence profile. It starts with
  the continuation-checked bitmap 80, then prioritizes advertised 8B/85. Earlier
  16/19/22/24/27-read plans and v1–v5 reports remain frozen. The 45-second budget,
  six-second transaction reserve, 200-ms gap, no retries and explicit parked
  approval gate are unchanged. This is not an app mapping or permission to run
  a collector; nothing has been sent to the owner's vehicle.

  Standard regeneration in-progress/type, NOx-adsorber states, normalized
  trigger, average interval/distance and four reagent quantities retain their
  separate flags/units. Passive type alone does not imply regeneration;
  trigger is not completion/soot, averages are not since-last values, and
  reagent data does not establish SCR health or remaining range. No DPFDeck
  state, native cache or packet format is changed.

  All 77 discovery, four probe and six capture cases pass locally, including
  15 new cases, all support/status bytes, complete 16-bit domains, 32-bit
  boundaries, privacy, real HSFZ stream pairs and slow-session limits. A pinned
  v5 fixture records unchanged `683e9c2` synthetic output. The changes await
  signing and exact-source CI before adding them to #201; they are not yet
  published or vehicle-validated.

- A separate local `diesel/ecu-identity-evidence` branch adds the companion
  `collect_ecu_identity.py` tool and `ECU-IDENTITY.md`. It has a fixed six-read
  plan: standard F191 hardware, F188 software number and F189 software version,
  once each at addresses 12/18. There are no arbitrary identifiers, VIN/serial
  or programming-fingerprint reads, session/security changes, retries, SSH or
  MOST. Physical execution requires approval of this separate parked plan;
  the owner's earlier offline-only instruction remains in effect.

  Raw bounded replies go to a new 0700 directory with 0600 private records.
  The shareable summary contains only statuses, positive lengths and NRCs;
  neither raw identity values nor their hashes are exported. The manufacturer
  codec and exact ECU identity remain unverified, even for positive replies.
  Replays are explicitly unverified. This provides private identity evidence
  for comparison with an authorized inventory, not a guessed BMW HWEL/SWFL or
  engine-family mapping. Fourteen new tests and all 72 earlier discovery/probe/
  capture cases pass, including six real HSFZ exchanges over local stream pairs.
  Existing PID plans/reports are unchanged. Publication awaits signing and CI.

- Standard DPF evidence follow-up in
  [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201): the separately
  selected `standard-dpf` plan allows at most 27 reads and exports a v5 report.
  It prioritizes advertised PID 7A/7B/7C, preserving the original four plans and
  report contracts against frozen fixtures. Ten fields retain bank/position,
  explicit support and units. Delta pressure is signed; inlet/outlet pressure
  has no assumed atmospheric reference. Full protocol ranges remain evidence
  requiring independent validation, including supported zero. No temperature
  fallback, soot/ash estimate, regeneration inference or native app is added.
  The unchanged 45-second budget reserves full transaction deadlines, uses
  200-ms gaps and never retries. Independent DashLogic format corroboration
  and the exact request/response policy are in the
  [capture guide](https://github.com/voioo/iDriveDeck/blob/15857d6f084dacb2e4cc21626e06048f7116ea62/source/docs/DIESEL-VALIDATION.md#optional-standard-dpf-pressure-and-temperature-evidence-profile).
  All 62 discovery, four probe and six head-unit capture cases pass locally and
  in [CI](https://github.com/voioo/iDriveDeck/actions/runs/46036060794) on exact
  source `15857d6f084dacb2e4cc21626e06048f7116ea62`. Coverage includes the full
  16-bit domains, every support byte and a 27-read real HSFZ exchange over local
  stream pairs with heartbeat, pending and wrong-ECU/PID replies. Those pairs
  cannot reach a vehicle. This supports the agreed #196 evidence path; exact
  B47/N57 support, independent measurements and QNX/parked DPFDeck validation remain open.
  This plan needs separate review and explicit owner approval before any
  physical use. No vehicle command, installation or firmware approval occurred.

- Shared host receiver isolation is available independently in
  [#213](https://github.com/inotbarnacle/iDriveDeck/pull/213). The older ShiftDeck
  test was reproduced failing on unchanged upstream `8567aba` when its private
  UDP receiver was forced onto HUD port 42870. A common allocator now excludes
  all project IPC ports, closes rejected sockets and bounds retries. Private
  transport/app/MOST receivers use it; intentional HUD, lease and activity
  bindings remain explicit. Four regressions cover every reserved port,
  retry exhaustion, bind-error cleanup and the real ShiftDeck worker's frame.
  [Standalone GCC/Clang CI](https://github.com/voioo/iDriveDeck/actions/runs/46030718245)
  passes seven native programs and 84 Python cases on exact source
  `c7cb0a1e0f44d333f5b38eace215b4bc764ae18f`. The diesel capability and
  EGT/EGR/pressure helpers also reuse this allocator; their local 24 capability/
  EGT cases pass. The isolated telemetry branch passes 16 native programs and
  130 native-suite Python cases, plus eight resource and eight read-only tool
  cases. Both [diagnostic CI](https://github.com/voioo/iDriveDeck/actions/runs/46031110579)
  and [receiver CI](https://github.com/voioo/iDriveDeck/actions/runs/46031110388)
  pass on exact source `e344917d5ca614ffcdbb520c685d7abe9374b5cd`.
  No production code, diagnostic requests or layout changed.

- Host EGT/EGR/pressure test receivers now avoid project IPC ports. A CI failure
  was reproduced by forcing the receiver onto HUD port 42870: the worker
  correctly detected a HUD listener and delivered a 24-byte binary HUD sample
  to the text-frame test. The new regression forces HUD, diagnostic-lease and
  activity-marker assignments, verifies rejection/cleanup, and keeps strict
  frame decoding. The fix changes test socket allocation only. All 12 EGT,
  ten EGR and 12 pressure transport cases pass locally.
  [Telemetry CI](https://github.com/voioo/iDriveDeck/actions/runs/46027879340)
  passes on exact source `c0a9115b69637f1dac9ce74aa6edaf9f4465fc5d`. The isolated
  branch now passes 16 native programs and 126 native-suite Python cases, plus
  eight resource and eight read-only tool cases. Runtime C/C++ is unchanged.

- Pressure evidence follow-up in [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201):
  a separately selected 24-read
  `standard-pressure` plan and v4 report prioritize PID 6D/70 while retaining
  EGR, EGT and the original measurements. All earlier 16/19/22-read plans and
  v1/v2/v3 reports remain unchanged, checked against frozen synthetic fixtures.
  Requested/actual A/B fields carry explicit pressure reference and units;
  unknown, unsupported, malformed and ambiguous fields have no numeric value.
  Supported zero is retained. The unchanged 45-second budget, full transaction
  reservation, 200-ms gaps and no-retry/advertisement gates remain enforced.
  All 47 discovery, four probe and six capture tests pass locally. A local
  native/Python differential check passes 131,609 cases covering the complete
  unsigned pressure domain in every field, every support byte and strict
  lengths. [Discovery CI](https://github.com/voioo/iDriveDeck/actions/runs/46026051677)
  passes all 57 tool cases on exact source
  `f18cf1c53b36e7cc008d7477fd765b927f921e39`. All 83 combined tool/resource
  cases also pass locally. These are host/simulation results only. This new
  plan needs its own review and explicit owner approval before physical use.
  No vehicle command or installation occurred.

- Standard pressure follow-up in [#202](https://github.com/inotbarnacle/iDriveDeck/pull/202): eight
  optional Custom gauges retain separate requested/actual A/B fields. Exact
  11-byte PID 6D data supplies rail gauge pressure in bar; exact ten-byte PID 70
  data supplies absolute boost in kPa. PID advertisement and current per-field
  support are mandatory, including in Gasoline. No MAP, derived boost, legacy
  rail source or automatic candidate pool is replaced. Polling is grouped per
  PID at two seconds from the actual request, with five-second backoff and
  3.5-second freshness. Unsupported fields remain blank; supported zero and the
  full unsigned domain retain their defined conversions. Fixed 2000-bar rail
  and 500-kPa absolute boost graphics clamp independently of numeric readings;
  these are app scales, not BMW limits or pressure targets. Existing IDs 0–46,
  NBG7/NBG8/NBG9 bytes/bounds and B1–B4 bounds remain stable. New IDs 47–54 use
  explicit NBG10/B5 extensions. The shared receive buffer is bounded at 1696
  bytes. [Protocol provenance and limitations](STANDARD-PRESSURE.md) distinguish
  the vendor layout evidence from independent preOBD encoder corroboration.
  The 12 focused pressure transport and eight resource cases pass locally,
  alongside native decoder/UI checks, ASan/UBSan, C++98 syntax and static
  analysis. Paired optimized host objects retain the same 48 BMW imports; this
  does not establish QNX ABI. All 906 original image pixels remain intact.
  The complete local telemetry suite passes 16 native C/C++ programs and 125
  native-suite Python cases, plus eight resource and eight read-only tool cases.
  [Telemetry CI](https://github.com/voioo/iDriveDeck/actions/runs/46021924804)
  passes on exact source `d0b7808a0fc86260eb1ebd54874de30211bde3c7`, including
  both compilers, C++98 syntax, analysis, resources and sanitizer checks.
  No BMW-specific mapping, physical support or QNX validation is claimed.

- Standard EGR follow-up in [#202](https://github.com/inotbarnacle/iDriveDeck/pull/202): Custom layout adds PID 2C
  request, 2D error and six independent requested/actual/error A/B fields from
  PID 69. Every new read is PID-gated, even in Gasoline. PID 69 requires exact
  seven-byte data and per-field support; error is never used as actual EGR.
  A/B hardware identity, flow and exact N57 support remain unverified.
  Two-second request-based polling, five-second failure backoff and 3.5-second
  freshness are bounded and host-tested. Automatic defaults remain the same
  21-candidate pool. IDs 0–38, exact NBG7/NBG8 packets and B1/B2/B3 bounds are
  retained; EGR adds IDs 39–46, NBG9 and B4. All 906 original image pixels and
  dashboard geometry remain intact. Decoder/UI sanitizer checks pass; paired
  optimized host objects retain the same 48 BMW imports. Those import checks
  do not establish QNX ABI or runtime behavior. The [EGR protocol and limits](STANDARD-EGR.md)
  records primary references, provenance, settings rollback and pending exact-
  target validation. The complete local suite passes 14 native C/C++ programs
  and 113 native-suite Python cases, plus seven resource and eight read-only
  tool cases. [Telemetry CI](https://github.com/voioo/iDriveDeck/actions/runs/46015121428)
  passes on exact source `5f5e35f688f3536434f8d50607bc9b8c231afcd2`, including
  optimized GCC/Clang, C++98 syntax, analysis and sanitizer jobs. No vehicle
  session or control command was performed.

- Standard EGT follow-up in [#202](https://github.com/inotbarnacle/iDriveDeck/pull/202): eight named bank/sensor
  channels use advertised PID 78/79 and a valid per-bank support layout.
  Polling is grouped per bank, at most every two seconds with five-second
  malformed/NRC backoff; conflicting layouts invalidate the bank. Automatic
  defaults choose at most one EGT. Existing IDs 0–30 and exact legacy NBG7
  bytes remain intact; appended IDs use wider masks, NBG8/NBC2 and B3 settings.
  Old B1/B2 ID bounds remain strict. Source/limits and legitimate public protocol
  references are in [Standard EGT](STANDARD-EGT.md). Local evidence includes
  two new C/C++ tests, eleven transport tests, all 2,097,152 default subsets and
  six resource checks preserving all 906 image pixels. Static analysis and
  sanitizer checks pass; all 49 host BMW imports match the pinned old source.
  [Telemetry CI](https://github.com/voioo/iDriveDeck/actions/runs/46009126701)
  passes on exact source `f398f78dbbae87077a0fc232cc17a4cea603746e`: 12 native
  C/C++ programs, 103 native-suite Python cases, six resources and eight
  collection-tool cases, with GCC/Clang, C++98 syntax, static analysis and
  ASan/UBSan. Clang exposed cancellation around raw 400 (exactly 0 C); the
  decoder now applies its integer offset before division. A delayed-capability
  regression also proved a 1.7-second bank interval was possible; the interval
  is now measured from the actual EGT request. The combined MOST simulator
  retains all fault/EGT fixtures and fails loudly on future capacity overflow.
  Exact target support, QNX and physical validation remain unverified.

- Completed: upstream clone, authenticated fork at
  [voioo/iDriveDeck](https://github.com/voioo/iDriveDeck), remotes, issue and PR
  investigation, source architecture audit and compatibility gap analysis.
- Development branch: `diesel/obd-capabilities`.
- Implemented: shared OBD capability gate, BoostDeck/WarmDeck integration,
  saturated diesel MAP handling, capability-based automatic diesel layouts,
  preserved custom layouts and unsupported/malformed/timeout regressions.
  `make -C source/native test` passes: ten C/C++ test programs and
  92 Python tests (31 app, 23 one-shot MOST, 26 transport, 12 capability tests).
  Four additional generator/menu tests pass. The automatic layout is limited
  to advertised existing standard channels and the EGS temperature candidate
  after a valid positive reply; no new BMW signal definition is added. Unknown
  capability slots stay empty. Automatic frames carry worker identity/launch
  token, capability masks and existing measurements in one bounded envelope;
  wrong-worker, old/future/stale and capability-regressing frames are rejected.
  Legacy G1/G2 and B1/B2 formats stay readable, existing valid layouts default
  to Custom mode, and a separate atomic D1 sidecar persists the mode choice.
  See [automatic diesel gauges](AUTO-DIESEL-GAUGES.md) for semantics and limits.
  Existing probe/head-unit tests (2 + 6) pass. Clang AddressSanitizer and
  UndefinedBehaviorSanitizer checks pass for core decoders and capability
  logic, automatic-layout/protocol and production plugin tests; Clang static
  analysis reports no finding in the new helper, worker or plugin. All 906
  regenerated image dimensions/pixels and the 49 host BMW imports are unchanged.
- Discovery and staged F15 validation are in [draft PR #201](https://github.com/inotbarnacle/iDriveDeck/pull/201)
  on independent branch `diesel/read-only-discovery`; 11 new discovery tests and
  8 existing tool tests pass, including privacy and live-request bounds.
  [Fork CI](https://github.com/voioo/iDriveDeck/actions/runs/45976585881) passed
  for discovery commit `ac48c54`.
- Telemetry/capability work is in [draft PR #202](https://github.com/inotbarnacle/iDriveDeck/pull/202).
  [CI for automatic-default code `824b78a`](https://github.com/voioo/iDriveDeck/actions/runs/45989318583)
  passed GCC/Clang, production syntax, helper/worker/plugin analysis, resource
  checks and sanitizers. A small follow-up avoids assuming `uint32_t *` and
  `unsigned *` are the same C pointer type when filling capability reports;
  its synthetic collector regression passes with identical output semantics.
  Check the [fork workflow runs](https://github.com/voioo/iDriveDeck/actions/workflows/host-tests.yml)
  for the current pushed commit's GCC/Clang and sanitizer results.
- ShiftDeck profiles are implemented in [draft PR #203](https://github.com/inotbarnacle/iDriveDeck/pull/203),
  independent branch `diesel/shift-profiles`, commit `eddd41e`. Diesel target
  adjustment, display scaling and independent profile choices preserve legacy
  gasoline behavior. All 312 original image payloads and dashboard geometry are
  preserved. Nine native C/C++ test programs, 80 existing Python worker tests and
  six asset tests pass; GCC/Clang syntax, ASan/UBSan and static checks pass.
  [Fork CI](https://github.com/voioo/iDriveDeck/actions/runs/45979404545) passed
  for both compiler jobs and memory checks and uploaded synthetic host previews.
  No QNX build or head-unit renderer/runtime result is implied.
- Fault handling is implemented in [draft PR #204](https://github.com/inotbarnacle/iDriveDeck/pull/204),
  independent branch `diesel/fault-status`, commit `57a3ec4`. The supported-status
  mask reaches the UI, exact hexadecimal fields/record lengths are validated,
  DDE/raw mode suppresses the gasoline lookup, reads are the default and clears
  require backend confirmation as well as the existing UI confirmation.
  [Fork CI](https://github.com/voioo/iDriveDeck/actions/runs/45982875509) passed
  GCC/Clang, static and sanitizer checks. Ten native test programs and 89 Python
  tests cover the fault branch, including mocked read/clear boundaries and
  native resource preservation. No new DDE dictionary is imported.
- Standard fault-format follow-up in #204: source `e8ab51f`, followed by the
  follow-ups through `208c21a`, adds explicit 03/07/0A emissions reads
  using the shared lease and deadline. Strict counted CAN responses become
  separate five-character P/C/B/U codes; BMW 24-bit records are never shortened.
  Missing, refused and malformed results remain unavailable; standard operations
  cannot request clearing, session changes or another format. The default BMW
  read and confirmed-clear flow remain unchanged. The category belongs to the
  active request, including during repeated clicks and description-profile
  changes. Permanent-code rows explain that the ECU controls removal. HSFZ
  requests wait 1.8 seconds; MOST uses a two-second nominal wait with pending
  waits capped at five seconds, excluding setup/cleanup. Unsupported, busy and
  security refusals do not retry or change session. Eleven native programs and
  98 Python cases pass locally, including
  eight new HSFZ/MOST cases and four resource checks. The new decoder/UI tests
  pass ASan/UBSan; production C++98 syntax and Clang analysis pass; all 44 BMW
  host imports match the previous fault branch. Original artwork is staged
  unchanged. Protocol evidence comes from AUTOSAR DCM and the ELM327 datasheet,
  not an N57 capture. No new dictionary or proprietary diagnostic input is added.
  [Current fork CI](https://github.com/voioo/iDriveDeck/actions/runs/46000950947)
  passes GCC/Clang, production analysis and all fault sanitizer checks on
  `208c21a`.
- BoostDeck rail display ranges are implemented in [draft PR #205](https://github.com/inotbarnacle/iDriveDeck/pull/205),
  independent branch `diesel/boost-rail-scale`, commit `1f2682a`. The Diesel
  profile uses a selectable 1000/1600/2000/2500-bar graphic range, default 2000;
  these are display choices, not BMW/DDE limits. Existing units, gasoline/other
  signal ranges, G1/G2 preferences and B1/B2 assignments are preserved. A
  separate R1 scale file uses atomic saves and reports failures. Nine native
  programs, 80 Python worker cases, five resource tests and eight existing
  tool tests pass locally. All 906 regenerated image dimensions/pixels match
  the package. Local GCC/Clang syntax, Clang static analysis, ASan/UBSan pass;
  the host BMW undefined-symbol set matches baseline (49 symbols), not proof
  of QNX compatibility. [Fork CI](https://github.com/voioo/iDriveDeck/actions/runs/45985869445)
  passed GCC/Clang, static, resource and sanitizer jobs on the exact pushed
  source commit. No diagnostic worker or decoder changed.
- Lifecycle dependency: Andreas Böhm's original `d63bd1f` from #143 is preserved
  with authorship and cherry-pick reference as `6e46890` on
  [fork branch `diesel/worker-lifecycle`](https://github.com/voioo/iDriveDeck/tree/diesel/worker-lifecycle).
  New regression commit `76a062d` is independently reviewable in
  [fork draft #1](https://github.com/voioo/iDriveDeck/pull/1), based on that fix;
  the original source contribution remains under upstream #143.
  The 13 current reaper plugins cover 14 process paths, including both BoostDeck
  processes. Tests simulate wait errors, use real inert owned children for
  signal-zero/termination checks, and advance the four monitor/read controllers
  through ten retry deadlines without duplicate launches. All diagnostic
  launches are intercepted. Each pinned original plugin must compile and fail
  specifically for losing a live PID; the four controllers independently
  reproduce duplicate starts. Twenty native programs, 80 existing Python cases,
  all reaper sanitizer runs and the negative controls pass locally.
  [Fork push CI](https://github.com/voioo/iDriveDeck/actions/runs/45992828635)
  passed GCC/Clang, baseline controls, production syntax/analysis and sanitizers.
  All 13 host BMW undefined-symbol sets match the baseline; this is not QNX ABI
  evidence. New source behavior is the existing #143 fix, not a newly guessed
  ECU/firmware workaround. The original patch's text-count guard is replaced
  by explicit behavioral coverage in this test contribution.
- WarmDeck targets: [draft #209](https://github.com/inotbarnacle/iDriveDeck/pull/209),
  branch `diesel/warm-readiness`, commit `4bd26e3`, implements independent
  unknown/pending/met states with per-fluid hysteresis and trend resets. It
  removes generic RPM numbers and identifies the targets as application choices.
  The production display rejects wrong-size/source, future, pre-opening and
  older positive samples; explicit unavailable markers clear a fluid immediately.
  The legacy frame cannot order timestamp-free invalidations or authenticate a
  worker, so a delayed invalidation can conservatively clear a newer reading.
  Eight native programs, 80 worker Python tests and three resource tests pass;
  the progress/UI tests pass ASan/UBSan, static analysis is clear, all 47 host
  BMW imports match baseline and all 22 regenerated image pixels are preserved.
  [Fork CI](https://github.com/voioo/iDriveDeck/actions/runs/45997064547) passes
  GCC/Clang, production syntax/analysis, resources and memory checks.
- Record-file follow-up: [draft #210](https://github.com/inotbarnacle/iDriveDeck/pull/210),
  branch `fix/launch-record-reads`, preserves both legacy/current record formats
  and avoids another read after a stream error or known EOF. Real glibc EIO
  streams reproduce the old loader's second failing read, including after a
  plausible legacy prefix; the fixed loader returns no records and closes once.
  Eight native programs and 80 Python cases pass locally, including every
  truncated current-format length and trailing bytes. Both record tests pass
  ASan/UBSan; Clang analysis of production LaunchDeck no longer reports the two
  inherited read-after-error/EOF findings. A CI wrapper initially failed because
  its runner lacked ripgrep; `9f472fa` uses Python and verifies the old loader's
  exact exit status and error count.
  [Corrected fork CI](https://github.com/voioo/iDriveDeck/actions/runs/45997659997)
  passes both compiler jobs, static analysis, sanitizers and the old-loader
  expected-failure control. No F15/F16 freeze cause is asserted.
- Toolchain documentation: [PR #208](https://github.com/inotbarnacle/iDriveDeck/pull/208),
  commit `b1838c7`, replaces third-party SDK/key-generator links in both setup
  guides with official QNX download/licensing/support channels. Compiler
  requirements match `build.sh`; official links and the current SDP 8 commercial
  evaluation were checked. This documentation-only PR is ready for review.
  No SDK was obtained, license accepted, or vendor contacted.
- Next: continue evidence-backed offline work on remaining diagnostic/profile
  and compatibility gaps. Keep remaining lifecycle/MOST work coordinated with
  #143/#138/#141. Extra diesel signals, DPF and DDE descriptions still require
  valid definitions, licensing where relevant, and ECU-specific evidence.
- Vehicle status: no vehicle connection, SSH, ECU requests, installation,
  head-unit restart or QNX build has been performed.
- Owner update: ENET/captures are not yet available; continue offline. No live
  collection or installation authorization has been received or inferred.
- Constraints: no legitimate QNX SDK/private framework inputs or physical
  captures have been supplied. Offline work continues. Missing verified DPF
  definitions prevent an honest functional DPF dashboard for this vehicle.
- Community coordination: [comment on #196](https://github.com/inotbarnacle/iDriveDeck/issues/196#issuecomment-7215061142)
  asks the existing DPFDeck contributor to align signal/evidence and worker
  responsibilities before a competing implementation. The contributor's
  [reply](https://github.com/inotbarnacle/iDriveDeck/issues/196#issuecomment-7219135140)
  confirms no implementation branch yet and offers B47 read-only evidence in
  #201's sanitized format once their ENET equipment arrives. Both sides retain
  monitoring-only scope and require ECU-specific evidence before mappings or
  thresholds. Their planned collection is not a completed capture, approval of
  this owner's vehicle work, or a verified B47/N57 profile.
- Completed offline discovery follow-up in [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201):
  source `c7ab86b37cfc4ca19f2ed3525be669bfb37afe17` preserves the default
  16-request plan and v1 report, verified against a frozen synthetic fixture
  generated by the old `ac48c54` implementation. An explicitly selected
  `standard-egt` profile adds only bitmap 60 and advertised PID 78/79 bank reads,
  capped at 19 requests with the same 45-second budget. Its v2 report separates
  unknown/unsupported sensor support, valid 0 C and implausible temperatures.
  All decoded values still require independent comparison; bank numbers do
  not establish sensor position, regeneration or freshness. Neither plan is
  approved for this vehicle by preparing it offline.
  [Discovery CI](https://github.com/voioo/iDriveDeck/actions/runs/46010975670)
  passes 21 discovery, four probe and six capture tests. A reproduced heartbeat
  acknowledgement timeout overrun (3.8 seconds against a two-second receive
  phase) is fixed by using the remaining receive deadline; the full six-second
  transaction reservation remains valid. All 55 combined tool/resource cases
  also pass locally. See the [profile and report contract](https://github.com/voioo/iDriveDeck/blob/diesel/read-only-discovery/source/docs/DIESEL-VALIDATION.md).
- Completed offline EGR evidence follow-up in [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201):
  source `35f6f00de1f47e229e73a74850e606a17c2d78c7` preserves the prior 16/19-read
  request sets and v1/v2 outputs, verified against frozen synthetic fixtures.
  A separately selected `standard-diesel` plan permits at most 22 reads and
  prioritizes PID 69, 2C and 2D before EGT and the original channels. The same
  45-second budget, full transaction reservation, 200-ms gaps and no retries
  apply. V3 reports distinguish each EGR requested/actual/error field and add
  fixed input-record status values for missing, duplicate, missing-reply and
  parsed-byte records. Actual is never inferred from legacy request/error;
  unsupported fields have no numeric value. Neither choosing the profile nor
  decoding synthetic data establishes a diesel family or physical support.
  [Discovery CI](https://github.com/voioo/iDriveDeck/actions/runs/46018009954)
  passes 33 discovery, four probe and six capture tests. All 68 combined tool
  and resource cases also pass locally. This new plan needs its own review and
  owner approval before any physical use.
- Next offline work: corroborate the remaining standard DPF/SCR/transmission
  candidate definitions below and their complete response formats. Reads above
  PID 80 need a separately tested bounded capability-cache extension and a new
  reviewed request policy. Do not infer that choosing a profile authorizes a
  physical run. Retain exact-target capture, standalone EGS, verified fixed oil
  and licensed DDE-description requirements. ENET remains unavailable; the owner
  instructed this effort to continue offline.
- Pull requests opened by this effort: [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201)
  (draft; discovery/reporting and validation plan) and
  [#202](https://github.com/inotbarnacle/iDriveDeck/pull/202)
  (draft; capability-gated telemetry, supported defaults, pressure saturation and standard EGT/EGR/pressure), and
  [#203](https://github.com/inotbarnacle/iDriveDeck/pull/203)
  (draft; configurable ShiftDeck diesel profile, persistence and resources), and
  [#204](https://github.com/inotbarnacle/iDriveDeck/pull/204)
  (draft; supported fault statuses, DDE/raw and standard emissions reads, clear confirmation), and
  [#205](https://github.com/inotbarnacle/iDriveDeck/pull/205)
  (draft; configurable diesel rail display scale and preserved settings/assets),
  [#208](https://github.com/inotbarnacle/iDriveDeck/pull/208)
  (ready for review; official QNX acquisition documentation),
  [#209](https://github.com/inotbarnacle/iDriveDeck/pull/209)
  (draft; independent WarmDeck app targets and data quality), and
  [#210](https://github.com/inotbarnacle/iDriveDeck/pull/210)
  (draft; LaunchDeck record read-error handling and regression tests), and
  [#213](https://github.com/inotbarnacle/iDriveDeck/pull/213)
  (ready for review; host UDP receiver isolation with no production-code change).

## Combined source validation

A new local worktree, `diesel/integration-dpf`, combines the previously published
`00d5fe1` integration with DPF backend `6f9176c`, the staged native DPF app and
the staged discovery v5/v6 and private identity-evidence changes. It is
uncommitted and unpushed while signing
access is pending; the older published integration/CI evidence below is retained.
The merge resolves the shared Makefile and fake MOST mode inventory, reuses
one capability test binary and expands lifecycle inventory to 14 plugins.
The original 13 baseline plugins still fail the lost-live-PID check, and all
four duplicate-launch controls reproduce the original bug. DPF did not exist
at that baseline and is explicitly reported as absent; its candidate ownership
matrix, owned-child cleanup and retry controller pass, including ASan/UBSan.

The reconciled full native run passes **42 C/C++ test programs and 162 Python
cases**. Two additional DPF/Boost handoff cases then pass, for **164 native-suite
Python cases**. All **134 tool/resource cases** pass; four fault-resource cases
are already included in the native total, giving **294 distinct Python cases**.
All 14 plugin sources pass Clang C++98 syntax checks and DPF static analysis.
The Go source remains unchanged from the standalone passing 26 top-level
suite. After fixing fixture-marker ordering, seven resource/package tests and
the two Go fixture selection/release guards pass again. The combined
host preview also renders from actual plugin model updates. The new identity
tool changes only host collection/reporting; native production source remains
byte-identical to the full native run.

This local result has no new GitHub CI run or QNX/vehicle validation. Source
snapshot hashes and logs are kept privately under `/tmp/idrive-dpf-integration-*`;
they are not presented as a signed commit or physical diagnostic capture.


The isolated [fork integration branch](https://github.com/voioo/iDriveDeck/tree/diesel/integration)
combines the source contributions without merging any upstream PR.
Conflicts in `CHANGELOG.md`, `source/README.md` and `source/native/Makefile` were resolved by
retaining every feature, documentation link and test target. The common MIT
framework fake is identical across the ShiftDeck, fault and BoostDeck scale branches.

The latest combined revision `00d5fe1` shares the private receiver allocator
across the older host suites and the newer diesel capability/EGT/EGR/pressure
helpers. Validation passes **39 native C/C++ test programs and 227 distinct
Python tests**: 148 native-suite cases plus the same 79 separate tool/resource
cases. The total tool/resource suite remains 83 cases, including four fault
resource cases already counted by the native suite. The increase is four
forced-port/resource-cleanup cases; runtime C/C++ and every diagnostic request
policy remain unchanged. All nine workflows pass on exact source
`00d5fe13b76cb1423eaa94f2f5d07ab21e375db7`:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/46031178080),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/46031177863),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/46031177943),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/46031178003),
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/46031177860),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/46031177947),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/46031178015),
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/46031177927), and
[receiver isolation](https://github.com/voioo/iDriveDeck/actions/runs/46031177894).
The independent test-only contribution is #213; #202 reuses that commit and
adds the diesel-helper adoption. Source from #211 is not included. These
results do not establish QNX, firmware or physical X5 compatibility. Later
progress-only documentation does not change this pinned source.

The earlier combined revision `f12c30a` adds #201's separately selected 24-read
pressure evidence profile and a forced-IPC-port host regression. Validation
passes **39 native C/C++ test programs and 223 distinct Python tests**: 144
native-suite cases plus 79 separate tool/resource cases. All 83 tool/resource
cases pass locally; four fault-resource cases are already counted by the native
suite. Compared with `8e8dbff`, the increase is 14 discovery cases and one UDP
receiver regression. Runtime C/C++, existing 16/19/22-read plans and v1/v2/v3
report contracts remain unchanged. All eight workflows pass on exact source
`f12c30a556c5c542236762a03fd82a35d019c1f6`:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/46027904024),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/46027903887),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/46027903943),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/46027903869),
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/46027904023),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/46027904225),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/46027904060), and
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/46027903907).
These are host/simulation results. The new 24-read plan needs its own review and
owner approval before a physical run. Exact DDE/EGS, A/B identity, independent
values, firmware, QNX and standalone operation remain unverified. Later
progress-only documentation does not change this pinned source.

The earlier combined revision `8e8dbff` adds optional requested/actual standard
pressure gauges while preserving the original MAP/rail sources, automatic
defaults and all older packet/settings bounds. Validation passes **39 native
C/C++ test programs and 208 distinct Python tests**: 143 native-suite cases
plus 65 separate tool/resource cases. All 69 tool/resource cases pass locally;
four fault-resource cases are already counted by the native suite. The increase
is two pressure native programs, 12 pressure transport cases and one resource
case. All eight workflows pass on exact source
`8e8dbfff8255310905e6401f41392b18be280cbd`:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/46022049069),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/46022048849),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/46022048902),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/46022049047),
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/46022048905),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/46022049054),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/46022049263), and
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/46022048842).
These are host/simulation results. No physical collection, installation or
factory-function test was performed. Existing 16/19/22-read discovery profiles
and v1/v2/v3 reports are unchanged and do not collect the new pressure fields.
Exact DDE/EGS support, A/B identity, independent values, firmware, QNX and
standalone transport remain unverified. Later progress-only documentation does
not change this pinned source.

The earlier combined revision `4ab39bc` adds #201's explicitly selected v3
EGR/EGT evidence profile and preserved v1/v2 contracts. Validation passes
**37 native C/C++ test programs and 195 distinct Python tests**: the same 131
native-suite cases, plus 64 separate tool/resource cases. All 68 tool/resource
cases pass locally; four fault-resource cases are already counted by the native
suite. The increase is 12 discovery cases; no native source or live read set in
the older profiles changes. All eight workflows pass on exact source
`4ab39bc4012f0c5ee4febb4ce642c7764c8c2ce0`:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/46018096862),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/46018096763),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/46018096903),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/46018096973),
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/46018096799),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/46018096754),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/46018096949), and
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/46018096984).
These are host/simulation results. The new 22-read plan is separately reviewable
and needs its own owner approval before a physical session. Firmware, exact
DDE/EGS, independently compared values, QNX and factory-function behavior remain
unverified. Later progress-only documentation does not change this pinned source.

The earlier combined revision `1a29e40` includes optional EGR request/actual/error
monitoring, retained NBG7/NBG8 and B1/B2/B3 contracts, and new NBG9/B4 boundaries.
The complete local suite passes **37 native C/C++ test programs and 183 distinct
Python tests**: 131 native-suite cases (36 app, 24 one-shot MOST, eight standard
OBD faults, 26 transport, 12 capability, four fault resources, 11 EGT transport
and ten EGR transport), plus 52 separate tool/resource cases. All 56 tool/resource
cases pass; four are already counted by the native suite. The existing #143
worker-ownership fix and its 13 production-plugin regression targets survive
the merge. Conflicts in `CHANGELOG.md` and the native Makefile retained every
feature and test target. All eight workflows pass on exact source
`1a29e40f42b99cc3e56ed5b31c66fefe49347190`:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/46015233005),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/46015232913),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/46015232933),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/46015233372),
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/46015233052),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/46015233164),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/46015232989), and
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/46015232971).
These are host/simulation results. No new discovery requests are silently added
to #201's fixed plans, and exact DDE/EGS, QNX, physical transport and sensor
validation remain unverified. The subsequent documentation-only commit on #202
does not alter the pinned runtime source above.

The earlier combined revision `031b67d` includes #201's explicit EGT evidence
profile, preserved default/v1 contract and heartbeat deadline fix. Validation
passes **35 native C/C++ programs and 172 distinct Python tests**: the same 121
native-suite cases as the EGT revision, plus 51 separate tool/resource cases.
All 55 tool/resource cases pass locally; four also run in the native suite and
are counted once. The increase is ten discovery cases and two probe deadline
cases. All eight workflows pass on exact source
`031b67d16789e1fb9388c04f7b9b1b361e68c79e`:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/46011054629),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/46011055529),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/46011054843),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/46011054572),
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/46011054517),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/46011054540),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/46011054667), and
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/46011054513).
This is combined host/simulation validation. The optional 19-read plan still
needs separate review and owner approval before any physical session. No
vehicle capture, QNX build, installation or new ECU mapping is claimed. A later
progress-only commit on #202 does not alter this pinned combined source.

The earlier combined revision `498dff2` includes standard EGT from #202 and its
valid-zero and delayed-capability rate regressions. Validation passes **35 native
C/C++ programs and 160 distinct Python tests**: 121 native-suite cases (36 app,
24 one-shot MOST, eight standard OBD faults, 26 transport, 12 capability, four
fault resources and 11 EGT transport), plus 39 separate tool/resource cases.
All 43 tool/resource cases pass; four are already included in the native suite.
The previous complete local run at `c1244d3` passed the same native programs and
120 Python native-suite cases; the final request-based polling change then
passed all 11 EGT transport cases locally. CI reruns the complete final suite
with GCC and Clang. All eight workflows pass on exact final combined source
`498dff202d67b054d88cc7f8d794d972a24b873f`:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/46009207186),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/46009207074),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/46009207244),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/46009207124),
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/46009207252),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/46009207217),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/46009207235), and
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/46009207309).
The inherited worker ownership fix, R1 rail settings, independent warm-up,
fault categories and legacy records remain covered. Local worker suites sharing
the fixed diagnostic lease ports must run sequentially; concurrent worktrees
can otherwise interfere with a simulated MOST case. These results establish
host/simulation behavior, not an N57 sensor mapping, QNX build or vehicle result.
The later progress-only commit on #202 does not change this pinned source.

The earlier combined revision `6ca7162` includes the standard emissions-code
follow-up in #204. Validation passes **33 native C/C++ programs and 147 distinct
Python tests**: 110 native-suite cases (36 app, 24 one-shot MOST, eight standard
OBD faults, 26 transport, 12 capability and four fault-resource cases), plus
37 separate discovery/capture/asset/resource cases. All 41 tool/resource tests
pass; four are already included in the native-suite total. The existing worker
ownership fix and all previous diesel features remain covered. All eight CI
workflows pass on exact commit `6ca71627df1bd22955ad9c2a3cf95d8e025d0409`:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/46000954234),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/46000954324),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/46000954228),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/46000954330),
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/46000954254),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/46000954350),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/46000954259), and
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/46000954242).
These are host/simulation results. Target DDE format support, licensed
descriptions, QNX loading and physical vehicle behavior remain unverified.

At integration code commit `ab6e1dd`, `make -C source/native test` passes all
13 native C/C++ test programs and 97 Python cases (36 app, 24 one-shot MOST,
26 transport, 8 capability and 3 fault-resource tests). Separate tool checks
pass 11 discovery, 2 probe, 6 head-unit capture and 6 ShiftDeck asset cases:
122 Python tests in total. All four workflows passed on the docs-updated
integration commit `b54b6e2`: [host diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/45984080553),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/45984080700),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/45984080870), and
[fault handling](https://github.com/voioo/iDriveDeck/actions/runs/45984080510).
These are simulation/host results, not QNX or vehicle
validation. A candidate build still needs an authorized SDK and exact firmware
inputs before any proposed installation can be prepared and reviewed.

The fifth contribution (#205) is integrated at code commit `a05e023`. All
15 native C/C++ test programs and the same 97 native-suite Python cases pass.
The separate discovery/probe/capture/ShiftDeck checks above plus five new
BoostDeck generator/resource cases pass: 127 Python tests in total. Both new
BoostDeck tests also pass standalone ASan/UBSan. The combined source is pushed
only to the fork; [integration workflow runs](https://github.com/voioo/iDriveDeck/actions?query=branch%3Adiesel%2Fintegration)
show results for each exact pushed revision. Later progress-only merges do not
change the tested source at `a05e023`. No upstream PR has been merged.

The rail display gap found in this combined tree is addressed separately by
#205: a correctly decoded synthetic 1000-bar value previously filled the
250-bar graphic. The optional diesel range changes presentation without
assuming an ECU maximum or expanding sensor support. PR #202 now also supplies
an explicit capability envelope and automatic/default policy; it does not infer
permanent support/absence from transient blank samples or replace saved gauge
choices. The expanded integration at code commit `8fee471` passes all 17 native
test programs and 101 native-suite Python cases (36 app, 24 one-shot MOST,
26 transport, 12 capability and three fault-resource cases). Separate tests
pass 11 discovery, two probe, six capture, six ShiftDeck assets, five rail-scale
resources and four automatic-default resources: 135 Python tests in total.
The production automatic-default test additionally changes the diesel rail
scale through its real callback while the automatic layout is active, checks
the resulting image fill, and confirms saved custom assignments are untouched.
The merged scale test explicitly selects Custom layout and both mode/scale
sidecars are cleaned up by the host fixtures. The combined plugin links the
new common object in host and native build rules. The previous five draft PRs
remain separate; these local/fork merges do not merge an upstream contribution.

The lifecycle dependency and new tests are integrated at `51600be`. The complete
host run passes 30 native programs and the same 101 native-suite Python cases.
All 34 separate discovery/probe/capture/asset/resource cases above pass as well:
135 Python tests in total. The new lifecycle harness compiles the combined
production plugins and links their additional defaults object. Host process
cleanup, retry and retained-PID behavior now have regression evidence alongside
the diesel features. This remains simulation/host evidence; it does not establish
the target HMI's signal disposition, diagnose a specific freeze, or validate QNX
installation, factory functionality or standalone transport.

WarmDeck #209, LaunchRecords #210 and the official QNX acquisition docs #208
are integrated at source commit `f868543`. The complete local run passes
32 native C/C++ programs and 101 native-suite Python cases. All 40 tool/resource
cases also pass, including three already counted by the native suite: 138
distinct Python tests in total. The added production WarmDeck actor shares the
unchanged MIT framework fake, and the existing #143 worker-PID fix survives the
merge. Legacy record formats and the inherited lifecycle controls remain covered.
The subsequent `fcf85a4` revision adds a documentation qualification and the
portable CI baseline checker, without changing runtime code. All eight fork CI
workflows pass on this exact revision:
[diagnostics](https://github.com/voioo/iDriveDeck/actions/runs/45997694388),
[discovery](https://github.com/voioo/iDriveDeck/actions/runs/45997694890),
[ShiftDeck](https://github.com/voioo/iDriveDeck/actions/runs/45997694613),
[faults](https://github.com/voioo/iDriveDeck/actions/runs/45997694679),
[rail scale](https://github.com/voioo/iDriveDeck/actions/runs/45997694439),
[worker ownership](https://github.com/voioo/iDriveDeck/actions/runs/45997694444),
[WarmDeck](https://github.com/voioo/iDriveDeck/actions/runs/45997694549), and
[record reads](https://github.com/voioo/iDriveDeck/actions/runs/45997694421).
No upstream contribution is merged. The latest progress-only edit does not
change the runtime source validated at `f868543` or the CI source at `fcf85a4`.

## Fault-data licensing finding

The existing `dme_descriptions.h` cites `gediz/bmw-unified-db`. Its
[license at the audited revision](https://github.com/gediz/bmw-unified-db/blob/0b2f53b3e298aa4f8700eaf704cdfae1d8e9e8c5/LICENSE)
limits the MIT grant to the repository's original tooling/compilation and
explicitly excludes underlying BMW-derived data rights. Therefore its DDE
dictionary is not treated as an established MIT-redistributable dataset. No new
BMW fault dictionary or proprietary BMW diagnostic database was downloaded or
incorporated. The publicly offered standard-OBD vendor archive inspected for
EGT protocol facts is documented separately in [STANDARD-EGT.md](STANDARD-EGT.md);
none of its database, descriptions or sample logs is vendored.
The existing upstream gasoline table is unchanged. A DDE description extension
still needs independently established provenance, permission and ECU applicability;
raw fault parsing/status improvements can proceed without importing that data.

## Remaining signal coverage against the objective

No row below is confirmed on the owner's X5 or from a recorded N57 capture.
An advertised PID and a plausible synthetic decode do not establish a physical
sensor's accuracy or an ECU-family-specific mapping.

| Requested signal group | Current source and evidence | Remaining work |
| --- | --- | --- |
| RPM, coolant, intake air, MAP, barometric pressure | Standard PID decoders, capability gates and host/simulation tests in #202. | Exact DDE advertisements and independent measurements. |
| Derived turbo boost | Standard MAP minus measured barometric pressure; saturated MAP is unavailable. | A verified higher-range source for full N57 boost monitoring. |
| Rail pressure | Standard direct-injection rail gauge pressure, host-tested scaling; #205 changes display range only. | Exact DDE support and independent comparison. |
| Requested versus actual boost and rail | Optional PID 6D gauge rail / PID 70 absolute boost fields, with format/flag/unit provenance and host/simulation tests; no substitution for existing readings. | Exact DDE advertisement, per-field support, A/B identity and independently compared paired captures. No BMW-specific mapping claimed. |
| Oil temperature | Advertised standard 5C only; unsupported reads skipped. No fixed BMW diesel source implemented. | Verify this DDE's support or a legitimate fixed source. Temporary F303 remains outside the default scope. |
| Oil pressure | Existing gasoline reference source does not establish diesel support. | Verified diesel identifier, format, scale and meaning. |
| MAF, calculated load, fuel rate, module voltage | Selectable standard channels with host/simulation coverage. Fuel rate is not a measured fuel-consumption total. | Exact DDE support and independently compared values; any consumption calculation needs a defined method and evidence. |
| Exhaust temperature | Standard bank/sensor fields in #202; optional sanitized evidence profile in #201. | Actual PID/sensor support, physical positions and independent temperature comparison. |
| Transmission temperature | Existing positive-read EGS/HSFZ candidate; host-tested decoding and independent WarmDeck state. | Exact EGS applicability/calibration and standalone transport validation. EGS-over-MOST is pending separate work. |
| Current gear, selected transmission mode, torque-converter status | No verified diesel mapping implemented. | ECU-scoped format and repeatable evidence; coordinate overlapping EGS work. |
| DPF regeneration active/requested/inhibited and reasons | No verified mapping implemented. Temperature alone is insufficient. | Shared B47/N57 evidence, contributor coordination and identity-scoped states. |
| DPF differential/inlet/outlet pressure and filter temperatures | Standard 7A/7B/7C discovery and DPFDeck source are host/simulation tested; no BMW-specific mapping. | Exact DDE advertisement, field/bank/position identity and independent comparison. |
| DPF soot/ash mass and time/distance since regeneration | No verified mapping or universal thresholds implemented. | Licensed/documented formats, units and repeatable exact-ECU evidence. |
| EGR requested/actual/error | Standard 2C/2D and explicit PID 69 A/B fields are host/simulation tested; actual is normalized duty/position and is not reconstructed from error. | Exact DDE support, A/B applicability and independent comparisons; any fixed BMW-specific mapping needs separate evidence. |
| SCR/AdBlue | No verified diesel mapping implemented. | Review credible standard/OEM definitions and exact ECU applicability before adding a signal. |
| DDE fault descriptions | Raw BMW codes/status and separate standard emissions categories are host-tested; no new DDE dictionary imported. | Licensed, ECU-applicable descriptions and exact-target fault-format validation. |

## Candidate standard emissions and transmission references

Offline reference review found additional candidates in the vendor's
[pinned SAE J1979 definitions](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/signalsets/v3/default.json).
The 7A/7B/7C layouts now have independent corroboration in
[DashLogic's PID reference](https://www.dashlogic.com/docs/technical/obdii_pids)
and host-tested decoding in #201's separately selected v5 evidence profile.
Offline follow-up also corroborates the 85/8B field layouts against DashLogic;
a separate local evidence profile now decodes them, awaiting publication. The
8F remains a single-vendor lead. A4 now has independent format evidence with
unresolved gear interpretation, described below. No row establishes this X5's support.
No database entries or descriptions are copied into project code; earlier
published collector plans are unchanged. Higher-page reads are isolated to the
new opt-in local profile pending signing and CI.

| Candidate | Distinction required before implementation |
| --- | --- |
| 7A/7B particulate-filter pressures | #201 v5 retains separate delta, inlet and outlet fields. Delta is signed 16-bit / 100 kPa; inlet/outlet are unsigned / 100 kPa with no assumed absolute/atmospheric reference. Field support is required. No soot estimate is inferred; the separate DPFDeck source uses these standard fields. |
| 7C particulate-filter temperatures | #201 v5 retains explicitly supported bank/inlet/outlet fields, separate from generic EGT 78/79. The full unsigned / 10 - 40 C protocol domain is evidence, not a BMW plausibility limit. Neither format proves regeneration or the physical N57 sensor location. |
| 85 reagent data | Ten data bytes: low-four support bits, separate average/demanded consumption (u16 / 200 L/h), tank fraction (u8 × 100 / 255 percent) and NOx-warning runtime (u32 seconds). Per-field support is essential; none establishes SCR health or remaining driving range. The local v6 profile collects evidence only; no app mapping is added. |
| 8B particulate-filter regeneration data | Seven data bytes: support byte, independently supported status/type bits, normalized trigger, average interval and average distance. The two u16 averages are minutes/km between events, not time/distance since the last regeneration. Trigger is not completion percentage or measured soot. The local v6 profile collects evidence only; no app state is added. |
| 8F particulate-sensor data | Sensor regeneration/measurement status must not be relabelled as DPF regeneration. |
| A4 transmission data | Actual gear and gear ratio are separate fields. The reference allows ambiguity between first and reverse; it does not establish selected P/R/N/D mode or torque-converter state. Preserve uncertainty rather than infer either from a ratio. |

A4 follow-up recovered the official
[J1699-3 release 17.00.00](https://sourceforge.net/projects/j1699-3/files/j1699/j1699-17.00.00/j1699-ver-17.00.00.zip/download)
with its published SHA-256
`fa7f6349a5fd57f20295c7b3a01a144f160d1715ffc1bf211a6da20838a4fd39`.
Its GPL-v2-or-later source requires four data bytes, checks A bits 0/1 with
high bits reserved, extracts B's high nibble and scales unsigned C-D / 1000.
This corroborates request shape/flags/arithmetic; no GPL file or binary is
copied into the project or executed.

The [MODALES D4.1 report](https://ec.europa.eu/research/participants/documents/downloadPublic?appId=PPGMS&documentIds=080166e5dff5f327)
was recovered through the European Commission archive after the project-site
fetch failed (SHA-256
`0e235c67ec0424978a4931992477b1f96022a0297454a6226340ccaec7266bf9`).
Its page 15 table was inspected visually. It corroborates the support flags
and ratio, but labels gear as byte B with 1 meaning reverse; J1699 extracts a
nibble, while OBDb retains first/reverse ambiguity. Page 31 reports no tested
car supplying A4. These references do not resolve a safe complete gear mapping
or establish this X5's support. No gear decoder, collector read or selected
P/R/N/D/torque-converter inference is added from them.

The shared native capability cache deliberately retains four pages through 60
and rejects measurement PIDs above 80. DPFDeck's local optional 8B follow-up
uses a separate bounded extension after verified continuation through bitmap
80, with its own tests and default-off request policy. Native 85/8F would need
their own reviewed policies; A4 requires bitmap A0 as well. Do not add reads to
an existing reviewed plan. The local 85/8B evidence profile and boundary/flag
tests await publication; exact-ECU comparisons remain necessary before any
physical app trial. For 8F, range/sentinel policies and complete fixtures still
need review; A4 still needs resolved
gear semantics, invalid/reserved states and exact-ECU evidence. All readings require exact DDE
applicability, independent comparisons and the agreed #196 evidence path before
use in a monitoring application.

Further reference inspection distinguishes PID 8F's PM **sensor** regeneration
from DPF system regeneration. [DashLogic](https://www.dashlogic.com/docs/technical/obdii_pids)
and the same pinned OBDb source separate per-sensor operating states and signed
normalized outputs. J1699-3's `SidSaveResponseData.c` requires seven data bytes;
`VerifyDiagnosticSupportAndData.c` checks the independent flags/status masks and
uses signed 16-bit / 100 output. Its additional minimum-value conformance check
does not establish a BMW operating limit. No 8F read, decoder or DPF-state alias
is added here.

PID 88 now has the local optional v7 evidence policy described above. J1699-3's
thirteen-byte shape and all five counter offsets agree with the manufacturer
and pinned OBDb layouts. A historical cause or reagent level must not become
a current DPF regeneration state. The full counter domain remains explicit
evidence, with out-of-block values flagged rather than clamped or given an
invented sentinel. This establishes a generic reply decoder and bounded plan,
not exact-ECU support, a BMW plausibility limit or a native app mapping.

## Outstanding scope

The overall N57 objective remains active. These drafts do not complete it.
Next: finish publishing the signed focused branches and verify exact-source CI,
then open/update the draft PRs with concise reviewer-focused descriptions.
Address the reproduced numeric-locale issue as a separate codec change.
Native SCR use still requires exact-ECU comparison and separately authorized
parked validation. Remaining transmission signals still need fully
corroborated layouts/semantics and request policies before implementation. Continue
offline review without promoting any host result to physical N57 validation.

Still required: ShiftDeck QNX/runtime validation; licensed, ECU-scoped DDE fault
descriptions and exact-target validation of the counted CAN standard fault path;
verified ECU-specific signals and profiles beyond the standard PID/default pool;
DPFDeck QNX/ABI, actual rendering, vehicle-profile and standalone validation
from the agreed shared evidence collection;
reproducible F15/F16 fixes coordinated with
existing PRs; exact target vehicle identification/firmware/transport evidence;
and any applicable QNX and separately authorized physical validation.
Maintain this scope rather than treating host tests or these drafts as proof
of comprehensive N57 support.

Retain separate evidence levels: physical vehicle, recorded diagnostics,
simulation, host only, experimental, unsupported. Never promote one to another
from plausible-looking measurements or model/firmware names alone.
