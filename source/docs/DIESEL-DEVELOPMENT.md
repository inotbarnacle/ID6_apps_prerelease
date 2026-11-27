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
| QNX / renderer | `native/build.sh` requires licensed SDP 6.5 and private framework link inputs. Framework fingerprint comparison checks static imports, not runtime behavior. | Host validation first. Ask [QNX licensing/support](https://qnx.software/en/contact) about a legitimate legacy SDP 6.5 entitlement. The current SDP 8 evaluation does not supply this toolchain. Do not use key generators or unauthorized archives mentioned in older project instructions. |
| HSFZ / gateway | Shared link filters target ECU and response prefix, bounds replies and deadlines, and discovers a unique wired link-local gateway. | Reuse the link and diagnostic lease. Do not scan arbitrary addresses or DIDs. |
| MOST / standalone | Current MOST tunnel accepts DME only; EGS transmission temperature requires HSFZ. A worker that has connected HSFZ does not subsequently switch to MOST. | Coordinate with [PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143); do not duplicate its multi-ECU tunnel work. |
| Ownership / lifecycle | Loopback leases coordinate project workers, not external tools. The original reapers can forget a live PID on `ECHILD`, permitting duplicate workers after retry deadlines and losing the PID needed for close. | Reuse PR #143's existing process-existence fix. [Fork test review #1](https://github.com/voioo/iDriveDeck/pull/1) adds production-plugin/baseline/owned-child regressions. Hidden polling changes in #143/#50 and actual vehicle cleanup remain separate. |
| Diesel telemetry | Explicit Diesel selection substitutes standard OBD coolant, IAT, rail and boost for unverified gasoline DIDs. Only fuel-rate polling currently checks a supported-PID bitmap. No automatic ECU-specific verified N57 profile exists on main. | Add shared capability checks first. Preserve fixed gasoline decoders and persisted signal IDs. |
| Oil temperature | [#21](https://github.com/inotbarnacle/iDriveDeck/issues/21), [#162](https://github.com/inotbarnacle/iDriveDeck/issues/162): DDE741 rejects fixed reads and does not advertise PID 5C. Temporary F303 is reused for different signals. | Leave unavailable without a verified fixed source. Never define or poll somebody else's dynamic DID. |
| Pressure | PID 23 is direct-injection rail gauge pressure, 0.1 bar per count. MAP PID 0B is limited to 255 kPa absolute; subtracting barometric pressure does not extend that range. The original graphic fills at only 250 bar. | #202 blanks saturated diesel MAP/boost. [Draft #205](https://github.com/inotbarnacle/iDriveDeck/pull/205) adds a configurable diesel rail display scale, not an ECU pressure limit. Requested rail/boost require separate evidence. |
| WarmDeck | Three temperatures expire independently and trend estimates require all three. Oil PID is currently queried unconditionally. Thresholds 85/75/40 C are application choices. | Gate unsupported oil and preserve independent coolant/transmission readiness. |
| ShiftDeck | Source artwork/preferences retain five gasoline targets while the latest installer release advertises 6000–8000 RPM. Neither establishes an N57 limit. | [Draft #203](https://github.com/inotbarnacle/iDriveDeck/pull/203) adds manually selected diesel targets/display scales, legacy settings migration and asset/plugin tests. No universal diesel redline or physical validation is claimed. |
| Faults | Worker reads 24-bit UDS codes; the old UI discarded status availability and always used a reference gasoline table. | [Draft #204](https://github.com/inotbarnacle/iDriveDeck/pull/204) preserves supported status bits, adds a DDE/raw view and backend clear confirmation. Licensed DDE descriptions and OBD-format validation remain open. |
| DPF | [#196](https://github.com/inotbarnacle/iDriveDeck/issues/196) offers B47 capture and DPFDeck development; no verified DPF mapping in this checkout. | Coordinate with the contributor before a separate DPFDeck implementation. No guessed DIDs, thresholds or regeneration controls. |
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
| [#196](https://github.com/inotbarnacle/iDriveDeck/issues/196) | Open | B47 capture and DPFDeck contribution offer; no maintainer response at audit time. |

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
| Fixed BMW diesel, DPF, EGR, SCR, oil pressure | Unsupported in this effort | Credible ECU-specific definitions and repeatable sanitized captures. |
| Physical installation / factory regression / rollback | Not performed | Separate owner-authorized milestone; staged plan in [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201). |

## Progress and next actions

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
- Static follow-up: modern host syntax checks suppress an inherited SDK
  callback-overload hiding warning in menus such as BEAMer. Clang reports two
  inherited stream-read findings at `launch_records.h:39`, also reproduced in
  the pinned baseline. These are documented separately; analysis is not claimed
  warning-free and no F15/F16 freeze cause is asserted from these host tests.
- Next: evaluate the existing WarmDeck RPM guide against the requirement for
  measured/advisory cold-operation information, address the LaunchRecords
  read-error path separately, and clean up obsolete toolchain acquisition links.
  Keep remaining lifecycle/MOST work coordinated with #143/#138/#141.
- Vehicle status: no vehicle connection, SSH, ECU requests, installation,
  head-unit restart or QNX build has been performed.
- Owner update: ENET/captures are not yet available; continue offline. No live
  collection or installation authorization has been received or inferred.
- Constraints: no legitimate QNX SDK/private framework inputs or physical
  captures have been supplied. Offline work continues. Missing verified DPF
  definitions prevent an honest functional DPF dashboard for this vehicle.
- Community coordination: [comment on #196](https://github.com/inotbarnacle/iDriveDeck/issues/196#issuecomment-7215061142)
  asks the existing DPFDeck contributor to align signal/evidence and worker
  responsibilities before a competing implementation. No reply assumed.
- Pull requests opened by this effort: [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201)
  (draft; discovery/reporting and validation plan) and
  [#202](https://github.com/inotbarnacle/iDriveDeck/pull/202)
  (draft; capability-gated telemetry and pressure saturation), and
  [#203](https://github.com/inotbarnacle/iDriveDeck/pull/203)
  (draft; configurable ShiftDeck diesel profile, persistence and resources), and
  [#204](https://github.com/inotbarnacle/iDriveDeck/pull/204)
  (draft; supported fault statuses, raw view and clear confirmation), and
  [#205](https://github.com/inotbarnacle/iDriveDeck/pull/205)
  (draft; configurable diesel rail display scale and preserved settings/assets).

## Combined source validation

The isolated [fork integration branch](https://github.com/voioo/iDriveDeck/tree/diesel/integration)
combines the source contributions without merging any upstream PR.
Conflicts in `CHANGELOG.md`, `source/README.md` and `source/native/Makefile` were resolved by
retaining every feature, documentation link and test target. The common MIT
framework fake is identical across the ShiftDeck, fault and BoostDeck scale branches.

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

## Fault-data licensing finding

The existing `dme_descriptions.h` cites `gediz/bmw-unified-db`. Its
[license at the audited revision](https://github.com/gediz/bmw-unified-db/blob/0b2f53b3e298aa4f8700eaf704cdfae1d8e9e8c5/LICENSE)
limits the MIT grant to the repository's original tooling/compilation and
explicitly excludes underlying BMW-derived data rights. Therefore its DDE
dictionary is not treated as an established MIT-redistributable dataset. No new
BMW fault dictionary or diagnostic database was downloaded or incorporated.
The existing upstream gasoline table is unchanged. A DDE description extension
still needs independently established provenance, permission and ECU applicability;
raw fault parsing/status improvements can proceed without importing that data.

## Outstanding scope

The overall N57 objective remains active. These drafts do not complete it.
Still required: ShiftDeck QNX/runtime validation; licensed, ECU-scoped DDE fault
descriptions and validated standard OBD fault formats; verified ECU-specific
signals and ECU-specific profiles beyond the standard PID/default pool; DPFDeck decision
after contributor coordination; reproducible F15/F16 fixes coordinated with
existing PRs; exact target vehicle identification/firmware/transport evidence;
and any applicable QNX and separately authorized physical validation.
Maintain this scope rather than treating host tests or these drafts as proof
of comprehensive N57 support.

Retain separate evidence levels: physical vehicle, recorded diagnostics,
simulation, host only, experimental, unsupported. Never promote one to another
from plausible-looking measurements or model/firmware names alone.
