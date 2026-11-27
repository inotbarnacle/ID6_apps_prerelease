# Diesel compatibility investigation

Audit baseline: upstream `8567abaadd1305965b3ad39e3222575cd49c507d`,
27 November 2026. Source is MIT licensed. No additional contribution template or
CI workflow was present at that baseline. Follow `source/AGENTS.md` and the host
checks in [BUILD.md](BUILD.md). Changes below are source contributions; published
application ZIPs are separate maintainer build artifacts.

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
| DPF | [#196](https://github.com/inotbarnacle/iDriveDeck/issues/196) contributor confirms no implementation branch yet and plans B47 collection once ENET hardware arrives. No verified DPF mapping in this checkout. | Share the sanitized evidence format; decide on a shared B47/N57 profile only after captures. No guessed DIDs, thresholds or regeneration controls. |
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
are reported as not vehicle-tested. [Draft PR #157](https://github.com/inotbarnacle/iDriveDeck/pull/157)
is open and proposes an identity-pinned N57 dynamic-DID path plus PowerDeck.
Neither is merged in this baseline. Its temporary-state mechanism is outside the
default read-only scope here. Do not import it without separate review,
authorization, conflict/cleanup/restoration validation and exact ECU evidence.

## Target vehicle evidence matrix

Owner-supplied description: X5 F15 xDrive30d, production 14 November 2017,
2018 model year, N57 190 kW, ZF 8HP, NBT Evo ID6, factory full-screen CarPlay.
This description is not diagnostic confirmation.

| Item | Evidence level | Remaining evidence |
| --- | --- | --- |
| Exact engine subvariant, DDE HW/SW and EGS | Unverified | Bounded ECU identification and independent corroboration. |
| Firmware, framework hashes, architecture, active screen | Unverified | Authorized read-only head-unit capture. No approved F15 profile added. |
| Gateway, HSFZ, MOST, standalone operation | Unverified | Parked transport observations with equipment attached and detached. |
| Standard OBD signals | Simulation / host tests only | Supported bitmaps and independently compared readings from this vehicle. |
| Standard EGT bank/sensor channels | Simulation / host tests only | Exact DDE support for PID 78/79, sensor bits, physical sensor locations and independently compared temperatures. No regeneration inference. |
| Standard EGR request, actual and error channels | Simulation / host tests only | Exact DDE support for PID 2C/2D/69, per-field bits and independent comparisons. Actual is reported normalized duty/position, not inferred flow or a value reconstructed from error. |
| Standard requested/actual rail and boost | Simulation / host tests only | Exact DDE support for PID 6D/70, per-field bits, physical A/B identity and independent comparisons. Rail is gauge pressure; boost is absolute. |
| Standard emissions fault codes | Simulation / host tests only | Confirm that the exact DDE/gateway supports services 03/07/0A and the counted CAN reply format. A category's zero count does not clear other categories. |
| Fixed BMW diesel, DPF, SCR, oil pressure | Unsupported in this effort | Credible ECU-specific definitions and repeatable sanitized captures. Standard EGR support does not establish a BMW-specific mapping. |
| Physical installation / factory regression / rollback | Not performed | Separate owner-authorized milestone; staged plan in [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201). |

## Progress and next actions

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
- Next offline work: prepare a separately reviewable pressure evidence profile
  while freezing #201's existing 16/19/22-read plans and v1/v2/v3 reports. Do not
  infer that selecting any profile authorizes a physical run. Retain exact-target
  capture, standalone EGS and licensed DDE-description requirements, plus the
  remaining transmission/DPF/SCR investigation. ENET is still unavailable and
  the owner instructed this effort to continue offline.
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
  (draft; LaunchDeck record read-error handling and regression tests).

## Combined source validation

The isolated [fork integration branch](https://github.com/voioo/iDriveDeck/tree/diesel/integration)
combines the source contributions without merging any upstream PR.
Conflicts in `CHANGELOG.md`, `source/README.md` and `source/native/Makefile` were resolved by
retaining every feature, documentation link and test target. The common MIT
framework fake is identical across the ShiftDeck, fault and BoostDeck scale branches.

The latest combined revision `8e8dbff` adds optional requested/actual standard
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
| DPF soot/ash mass, differential pressure, time/distance since regeneration | No verified mapping or universal thresholds implemented. | Licensed/documented formats, units and repeatable exact-ECU evidence before a DPFDeck decision. |
| EGR requested/actual/error | Standard 2C/2D and explicit PID 69 A/B fields are host/simulation tested; actual is normalized duty/position and is not reconstructed from error. | Exact DDE support, A/B applicability and independent comparisons; any fixed BMW-specific mapping needs separate evidence. |
| SCR/AdBlue | No verified diesel mapping implemented. | Review credible standard/OEM definitions and exact ECU applicability before adding a signal. |
| DDE fault descriptions | Raw BMW codes/status and separate standard emissions categories are host-tested; no new DDE dictionary imported. | Licensed, ECU-applicable descriptions and exact-target fault-format validation. |

## Outstanding scope

The overall N57 objective remains active. These drafts do not complete it.
Still required: ShiftDeck QNX/runtime validation; licensed, ECU-scoped DDE fault
descriptions and exact-target validation of the counted CAN standard fault path;
verified ECU-specific signals and profiles beyond the standard PID/default pool;
DPFDeck decision after the agreed shared evidence collection; reproducible F15/F16 fixes coordinated with
existing PRs; exact target vehicle identification/firmware/transport evidence;
and any applicable QNX and separately authorized physical validation.
Maintain this scope rather than treating host tests or these drafts as proof
of comprehensive N57 support.

Retain separate evidence levels: physical vehicle, recorded diagnostics,
simulation, host only, experimental, unsupported. Never promote one to another
from plausible-looking measurements or model/firmware names alone.
