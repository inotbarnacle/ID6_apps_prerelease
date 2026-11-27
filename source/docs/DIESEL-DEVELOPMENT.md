# Diesel compatibility investigation

Audit baseline: upstream `8567abaadd1305965b3ad39e3222575cd49c507d`,
27 November 2026. Source is MIT licensed. No additional contribution template or
CI workflow is present in this checkout. Follow `source/AGENTS.md` and the host
checks in [BUILD.md](BUILD.md). Changes below are source contributions; published
application ZIPs are separate maintainer build artifacts.

## Current gaps and overlapping work

| Area | Current evidence and gap | Action |
| --- | --- | --- |
| Native HMI / RHMI | C++98 plugins use private framework ABI declarations and generated XML/image archives. Full-screen layouts and workers are separate compatibility concerns. | Keep ECU changes independent of ABI/layout changes; no new firmware approval. |
| QNX / renderer | `native/build.sh` requires licensed SDP 6.5 and private framework link inputs. Framework fingerprint comparison checks static imports, not runtime behavior. | Host validation first. Ask [QNX licensing/support](https://qnx.software/en/contact) about a legitimate legacy SDP 6.5 entitlement. The current SDP 8 evaluation does not supply this toolchain. Do not use key generators or unauthorized archives mentioned in older project instructions. |
| HSFZ / gateway | Shared link filters target ECU and response prefix, bounds replies and deadlines, and discovers a unique wired link-local gateway. | Reuse the link and diagnostic lease. Do not scan arbitrary addresses or DIDs. |
| MOST / standalone | Current MOST tunnel accepts DME only; EGS transmission temperature requires HSFZ. A worker that has connected HSFZ does not subsequently switch to MOST. | Coordinate with [PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143); do not duplicate its multi-ECU tunnel work. |
| Ownership / lifecycle | Loopback leases coordinate project workers, not external tools. Parent monitoring, signals and MOST watchdogs bound worker lifetime. Plugin `ECHILD` handling and polling remain under review upstream. | PR #143 and [PR #50](https://github.com/inotbarnacle/iDriveDeck/pull/50) cover lifecycle changes. Close other diagnostic clients for parked validation. |
| Diesel telemetry | Explicit Diesel selection substitutes standard OBD coolant, IAT, rail and boost for unverified gasoline DIDs. Only fuel-rate polling currently checks a supported-PID bitmap. No automatic ECU-specific verified N57 profile exists on main. | Add shared capability checks first. Preserve fixed gasoline decoders and persisted signal IDs. |
| Oil temperature | [#21](https://github.com/inotbarnacle/iDriveDeck/issues/21), [#162](https://github.com/inotbarnacle/iDriveDeck/issues/162): DDE741 rejects fixed reads and does not advertise PID 5C. Temporary F303 is reused for different signals. | Leave unavailable without a verified fixed source. Never define or poll somebody else's dynamic DID. |
| Pressure | PID 23 is direct-injection rail gauge pressure, 0.1 bar per count. MAP PID 0B is limited to 255 kPa absolute; subtracting barometric pressure does not extend that range. | Test units and avoid representing saturated MAP as full-range diesel boost. Requested rail/boost require separate evidence. |
| WarmDeck | Three temperatures expire independently and trend estimates require all three. Oil PID is currently queried unconditionally. Thresholds 85/75/40 C are application choices. | Gate unsupported oil and preserve independent coolant/transmission readiness. |
| ShiftDeck | Source artwork/preferences retain five gasoline targets while the latest installer release advertises 6000–8000 RPM. Neither establishes an N57 limit. | Separate configurable engine-profile contribution, with persistence and generated-asset tests. No universal diesel redline. |
| Faults | Worker reads 24-bit UDS codes with status bytes; plugin descriptions are a reference gasoline DME table. UI confirmation precedes clearing. | No invented DDE descriptions. Audit data license and ECU scope before adding a DDE table. |
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
| Physical installation / factory regression / rollback | Not performed | Separate owner-authorized milestone; see staged plan to be added. |

## Progress and next actions

- Completed: upstream clone, authenticated fork at
  [voioo/iDriveDeck](https://github.com/voioo/iDriveDeck), remotes, issue and PR
  investigation, source architecture audit and compatibility gap analysis.
- Development branch: `diesel/obd-capabilities`.
- Implemented: shared OBD capability gate, BoostDeck/WarmDeck integration,
  saturated diesel MAP handling and unsupported/malformed/timeout regressions.
  `make -C source/native test` passes: eight C/C++ test programs and
  88 Python tests (31 app, 23 one-shot MOST, 26 transport, 8 capability tests).
  Existing probe/head-unit tests (2 + 6) pass. Clang AddressSanitizer and
  UndefinedBehaviorSanitizer checks pass for core decoders and capability
  logic; Clang static analysis reports no finding in the new helper.
- Discovery and staged F15 validation are in [draft PR #201](https://github.com/inotbarnacle/iDriveDeck/pull/201)
  on independent branch `diesel/read-only-discovery`; 11 new discovery tests and
  8 existing tool tests pass, including privacy and live-request bounds.
  [Fork CI](https://github.com/voioo/iDriveDeck/actions/runs/45976265810) passed
  for discovery commit `d78b709`.
- Next: finish telemetry fork CI, open its draft PR, then ShiftDeck
  configuration and fault display audit. Keep lifecycle/MOST work coordinated
  with #143/#138/#141 rather than replacing their pending fixes.
- Vehicle status: no vehicle connection, SSH, ECU requests, installation,
  head-unit restart or QNX build has been performed.
- Constraints: no legitimate QNX SDK/private framework inputs or physical
  captures have been supplied. Offline work continues. Missing verified DPF
  definitions prevent an honest functional DPF dashboard for this vehicle.
- Community coordination: [comment on #196](https://github.com/inotbarnacle/iDriveDeck/issues/196#issuecomment-7215061142)
  asks the existing DPFDeck contributor to align signal/evidence and worker
  responsibilities before a competing implementation. No reply assumed.
- Pull requests opened by this effort: [#201](https://github.com/inotbarnacle/iDriveDeck/pull/201)
  (draft; discovery/reporting and validation plan).

Retain separate evidence levels: physical vehicle, recorded diagnostics,
simulation, host only, experimental, unsupported. Never promote one to another
from plausible-looking measurements or model/firmware names alone.
