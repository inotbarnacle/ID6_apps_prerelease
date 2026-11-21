# Dashboard compatibility

Match the reported firmware and both SHA-256 hashes before using the dashboard package. The desktop installers also check the display and select bundled layouts for 800 × 480, 960 × 480, 1280 × 480, 1440 × 540, and 1920 × 720. The raw dashboard ZIP uses the 1280 × 480 layout.

## Desktop installer profiles

The installer checks firmware and framework pairs against [installer-profiles.json](installer-profiles.json). Reported results apply to the named menus and vehicles.

| NBTevo firmware | AppFramework SHA-256 | CoreFramework SHA-256 | Evidence |
| --- | --- | --- | --- |
| N18032I | `93f4c7ab7e59c0adf7923b9b491de06165ba9d97bbe7412292e7ddc418ca2123` | `64ba154c68c772f9ca779fc18507f5b34715be58ded4a198b8a62b90d16c3ee0` | **Tested and working** for the dashboard menus opened on the reference head unit |
| N18092I | Same as N18032I | Same as N18032I | **Tested and working** on wired ENET with an earlier package, per [owner report](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/31). The reported framework pair matches N18032I. |
| P18362I | `d9c674c210272e8f07473e73e59ed8f23d984d4d30162971bedb56d21c682b28` | `896d9de22fa85f7f5aa4fc2a0e62003c63c41c47186b06f39a2fd4fee06fa5d5` | **Tested and working** for gauges over wired ENET on an earlier package, per the [owner report](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/8). The current plugin build also loaded in the QNX emulator. |
| V20153I | `085b7f2a9f96734be51621a70a04c24d01525eb5e9852f45c845a7d2f4ae4c67` | `56705e56c349d9ae57a7782bd39b795cfc01391ba8ff155046f32fbe8ffad2cb` | Exact match to the capture used for the earlier QNX load check |
| W20204I | Same as V20153I | Same as V20153I | Both hashes and the 1280 × 480 display were verified on the car over wired ENET. A parked, read-only check returned `No such file or directory` for `/var/opt/hmi/data/ID5/Plugins` on `hu-omap` and for both Qnet paths checked from `hu-jacinto`. First-install handling and app runtime remain unverified. |
| Y24073J | `ef2488b007d592301d8f21d9aa60771ad552d06b5e97c29e7813e3946644d820` | `cb0e488d1ebe2e30de87bb56f0e93d6a2d76b44309b0b47fef5b0b7cec048973` | Earlier QNX load check passed. [#56](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/56) and [#77](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/77) report clipped or empty display content. |

### Added in installer v0.7.3

| NBTevo firmware | Evidence |
| --- | --- |
| L17252I | Attached capture matches the audited M17345I pair byte for byte. |
| M17365I | Both hashes in [#60](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/60) match the audited M17345I pair. The owner confirmed installation with v0.7.3 and reported missing telemetry for several gauges. |
| Q18491I | The attached libraries match R19101I. The rebuilt BoostDeck and ShiftDeck pass the reference ABI comparison. **Tested and working** for BoostDeck in [#76](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/76), which also reports gateway errors in Bootmod3 Suite and BEAMer. |
| R19144I | The attached report matches the same R19101I pair. **Tested and working** for MHD+ Suite and Engine Fault Codes in [#27](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/27); the same report describes reproducible HMI freezes with BoostDeck and ShiftDeck. The installer accepts this profile. A separate F87 report with the same hashes passed a 10–15 minute parked check with its ID6 interface. [#89](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/89) |
| S19322I | [#38](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/38) matches the audited S19221I pair. |
| T18501I | [#53](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/53) matches the audited R19101I pair. |
| U19384I | [#4](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/4) matches the audited U19384I pair. |
| X20511I | [#73](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/73) matches the audited X20511I pair. |
| Y21432J | **Tested and working** for BoostDeck in [#62](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/62). ShiftDeck and Engine Fault Codes also opened. xDrive mode switching still depends on the DSC version. |
| Y22292I | **Tested and working** for BoostDeck and ShiftDeck in [#59](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/59#issuecomment-6967237281). The owner supplied a passing ABI audit. Engine Fault Codes opened, but the displayed code meanings were not confirmed. |
| Y23236I | **Tested and working** for MHD+ Suite, ShiftDeck, and Engine Fault Codes in [#69](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/69). BoostDeck worked after an initial crash; transmission temperature was missing with the engine running. [#40](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/40) provides the matching hashes. |

### Added in installer v0.7.4

| NBTevo firmware | Evidence |
| --- | --- |
| M17345I | Both framework hashes match M17365I and L17252I. |
| R19101I | Both framework hashes match R19144I, Q18491I and T18501I. |
| S19221I | Both framework hashes match S19322I. |
| T23235J | Both framework hashes from the 1280 × 480 Supra in [#37](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/37) exactly match the existing Y23236I profile. [The owner confirmed](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/37#issuecomment-6990226041) that BoostDeck and Engine Fault Codes work after a parked restart with the car's diagnostic gateway configured. Transmission temperature stays blank because its EGS rejects the existing DID [#79](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/79). |

### Firmware reports still needing evidence

| Firmware | Evidence needed |
| --- | --- |
| J16383I | The 1280 × 480 request in [#86](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/86) supplies a version and display. A submitted Trio capture contains both hashes. The pair is distinct from the existing installer profiles and is recorded as capture-only. |
| K17052I | [#34](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/34) supplies the screen version. Both framework hashes and the active display size are still needed. |
| V20153J | [#25](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/25) reports that the ABI resolves. Both exact framework hashes and display size are still needed. V20153I is a different profile. |
| W202xxI | [#97](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/97) reports a working installation and a BEAMer module issue. The complete firmware code, both framework hashes, and display size are still needed. |

O18184I passed the static ABI comparison and has its own framework hashes. Use the [Trio Compatibility Collector](../tools/trio-collector/Quick%20Start.md) for the version, both framework hashes, and the active display size.

The 320d handoff matches the existing N18032I firmware profile. ShiftDeck RPM and BoostDeck transmission temperature were observed working. Dynamic diesel pressure and intake requests remain unshipped because the draft redefines a shared ECU diagnostic slot and independent validation is incomplete. Firmware compatibility does not determine which engine signals or tuning controls a vehicle supports.

## Static framework checks

The pairs below were extracted from the head-unit software in BMW programming data (HU_NBT2, SWFL `0x2720` for `/opt/hmi/ID5` and `0x271A` for `nbt_version.txt`) and compared offline with the packaged apps. This table records offline results. See the table above for installer eligibility and separate runtime evidence, and [New firmware](BUILD.md#new-firmware) for the validation workflow.

| NBTevo firmware | HMI build | AppFramework SHA-256 (bytes) | CoreFramework SHA-256 (bytes) |
| --- | --- | --- | --- |
| M17345I | ID5_1703 Build 139 | `841355846492474c14e0e4b476f06a1aef5346869cbe38f459666cc4a60379a6` (51644) | `1723a159711021873ded6bfb7dd5c64d00ad77d3d994a51a95438fd7822a71d5` (95472) |
| O18184I | ID5_1807 Build 29 | `286eceafa4868929caecdff3540eaeba2367186b94f0ce15bada305964572f7a` (51644) | `20a21a45304db9e76deadc9d93b0db877566fcf6b91a5220f253a33dbcf4246f` (95472) |
| P18362I | ID5_1811 Build 34 | `d9c674c210272e8f07473e73e59ed8f23d984d4d30162971bedb56d21c682b28` (51644) | `896d9de22fa85f7f5aa4fc2a0e62003c63c41c47186b06f39a2fd4fee06fa5d5` (95472) |
| R19101I | ID5_1903 Build 41 | `057618b81375564f5255308a3c59111dd1e9eb57e43e38b7a858bff04ab41ce5` (51580) | `7c6ad2a7134adf8385435487301c88a192a0e8e3bf1bf5ae5c858eb4f92d8202` (95400) |
| S19221I | ID5_1911 Build 22 | `7650e45f91a9b1e05f978946479efa50dd4e6d7ec6462d0fd2d75ef5be666330` (51644) | `f8d82a0dfa275a5c9a1d7f5b568e64a901bfd1d27e8bad94fcebc9b883f875a7` (95472) |
| S19322I | ID5_1911 Build 32 | Same as S19221I | Same as S19221I |
| U19384I | ID5_2003 Build 1 | `0cc35c7f78e53f5a06999ce8b353209c8079de457865db3c4cedffc3138a0820` (51644) | `e35112bbee55a9d9e0ba3200952067fd5060749c373415d3e743a6fd653f6c9f` (95472) |
| X20511I | ID5_2103 Build 37 | `86afbba9f4b91b6b20c0765bcef1d4f1798ee8cf6bcff7f6765d8fa9e9d6ea2f` (51612) | `3130e897b9809347783f107010c85eb66a8cb348b33d6525fc11a482932b2f32` (95456) |
| Y21432J | ID5_2111 Build 28 | `75aff211cfe29e904cdc8545bf8f4e9174c132dc0469e0a7ef84296391ada69f` (51612) | `235d275fbf7eeebaa955641202f971c260be2e49d9addda1227f5eb0316a2156` (95456) |
| Y24073J | ID5_2403 Build 1 | `ef2488b007d592301d8f21d9aa60771ad552d06b5e97c29e7813e3946644d820` (51524) | `cb0e488d1ebe2e30de87bb56f0e93d6a2d76b44309b0b47fef5b0b7cec048973` (95312) |

Results:

- **ABI audit:** `tools/audit_framework_abi.py`, with the ID5_1911 pair as reference, passes for all seven packaged plugins (BoostDeck, ShiftDeck, EngineCodes, MhdMaps, Bootmod3Suite, XDriveControl, HudTest) on every build above. It covers 46 to 52 function prefixes per plugin and the `bmw::PluginApp` vtable slots.
- **Exports and imports:** all ten builds export the same framework symbols with the same names, sizes, and types, and all 52 `bmw::` symbols imported by the plugins resolve in each.
- **Car capture:** the P18362I pair equals the pair read from a 2017 F20 (B58). The Y21432J and Y24073J pairs equal hashes reported from cars in issues #1, #11, and #13.
- **System libraries:** `libm.so.2`, `libsocket.so.3`, and `libstdc++.so.6` are byte-identical in M17345I, O18184I, P18362I, S19221I, X20511I, Y21432J, and Y24073J. They were not extracted for R19101I, S19322I, and U19384I. For P18362I, every symbol and symbol version imported by the seven plugins and seven workers resolves against these and the firmware's `libc.so.3`. The `libc.so.3` of the other builds was not checked.

Open points:

- The original ten-build audit used ID5_1911 as its reference. The downloadable fingerprint now uses the N18032I reference capture.
- The R19144I report matches both R19101I framework hashes. Its [HMI freeze report](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/27) is unexplained by these checks.
- The audit compares instruction prefixes and vtable slots. It does not show that a plugin loads or that its menus work.

## xDrive Control (experimental)

The Status command reports whether DSC identification timed out, returned a UDS refusal, or identified an unsupported SGBD index. This distinguishes the missing replies in [issue #23](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/23) from an identified 0F2100 in [issue #25](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/25). Mode changes retain the stationary Park checks, with the engine off except on 0F1860. The write allowlist is 0F2510, 0F2850, and 0F1860 (F-series DSC_25). For 0F1860, `dsc_25.prg` defines 0xDBE1 as LMV_FUNKTION (1 active, 0 inactive for the current terminal 15 cycle). The worker first verifies the tested LMV 0F1480 transfer case. This LMV rejects 0xDB32 with NRC 31. The readback is the LMV target qualifier in 0x4002 byte 3 (`lmv_01.prg` STATUS_SOLLMOMENT): 0x20 applies the DSC target, 0x28 opens the clutch, 0x70 is rear-wheel drive. With the engine off, DSC_25 reports 0xDBE1 = 0 and the LMV reports 0x60 (fallback), so on 0F1860 the switch requires the engine running, stationary, and Park. The restore button also writes 0xDBE1 = 1 on 0F1860, as `dsc_25.prg` allows; the owner of the test car confirmed that it restores AWD, and an ignition cycle also restores AWD. On a 2018 F-series car, the write took more than 2 s to acknowledge; 0x4002 changed from 0x20 to 0x28, and front-axle actual torque (0x4003) stayed at its zero value up to 52 km/h. The immediate AWD restore is limited to 0F2850, as in the [upstream measurements and implementation](https://github.com/cauliflowermossgrape/idrive7-apps/blob/main/apps/live_adjustments/xdrive.js), and 0F1860.


FreZZZeR cross-checked the factory `dsc_25.prg` and `lmv_01.prg` definitions. The
LMV 0x4002 qualifier also reports 0x61 for fallback with RTA calibration, 0x62 for
fallback with clutch opening, 0x80 for initialization, 0xE0 for standby, and 0xFF for
an invalid target. A DSC `48097D` describes an open transfer-case clutch and
rear-wheel drive only. It is consistent with commanded RWD. zelnkup's follow-up
drive logged zero front torque and qualifier 0x28 throughout, with no DSC fault
recorded. A `48097D` entry is therefore optional evidence of the commanded state.
[Factory-data cross-check](https://github.com/inotbarnacle/ID6_apps_prerelease/pull/87#issuecomment-7036793563) · [Drive log follow-up](https://github.com/inotbarnacle/ID6_apps_prerelease/pull/87#issuecomment-7036949770)

For the N57 F3x reports, LMV `440113` or `440114` would point to a transfer-case
variant mismatch. That explanation is still a hypothesis for the affected cars.
DSC `480682`, `48068C`, and `480AB5` concern unknown clutch position, temporary
clutch disablement, or protection. LMV `440117` and `440118` concern temporary
transfer-case deactivation. The write allowlist retains its existing scope. For DSC_25/LMV
0F1480 pair, the EGS Park reply is 00 01 with the engine running. The engine-off
branches still require 00 06. RPM and zero-speed checks also have to pass.
[Park reply measurements and fix](https://github.com/inotbarnacle/ID6_apps_prerelease/pull/114) · [N57 compatibility discussion](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/68)

## New compatibility submissions

| Firmware | Evidence | Installer status |
| --- | --- | --- |
| T24081I | Both framework hashes match Y24073J. The submitted capture reports 1280 × 480 and split-node SSH. The current BoostDeck and ShiftDeck passed the static framework comparison. | Exact-hash alias |
| Y24073J | Both submissions match the existing framework pair. One capture reports 1440 × 540. [Issue #105](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/105) | Existing profile |
| Y22292I | The submitted framework bytes match both report digests and the existing profile. [Issue #59](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/59) | Existing profile |
| J16383I | One submitted capture reports a distinct pair. [Issue #86](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/86) | Capture-only |
| O18184I | Both captures agree on a distinct pair. One reports 1440 × 540 over split-node SSH. | Capture-only |
| W20204I | Both frameworks match V20153I. trulex submitted the installer profile. mohamaddude1 reported BoostDeck, ShiftDeck and Engine Fault Codes working on an F90. [PR #112](https://github.com/inotbarnacle/ID6_apps_prerelease/pull/112) · [Issue #104](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/104) | Exact-hash alias |

The installer recognises capture-only pairs and reports that they require a native ABI check and parked runtime validation before staging.

## Reported Bootmod3 and HUD compatibility limits

- Bootmod3 Suite reads the active slot automatically when the map page opens and after each selection, after identifying a supported CustomROM. The ID6 candidate imports newer marker layouts and profile metadata from [iDrive7 Engine Config](https://github.com/cauliflowermossgrape/idrive7-apps/blob/35bca28bc82b30482d73782877ebaf92c69dcb8c/apps/live_adjustments/bm3_protocol.js). For an unsupported dynamic DID, it tries the same fixed marker via a direct diagnostic read. This covers the refusal reported in [issue #95](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/95) in simulation. Whether the S55 accepts the direct read and matches a supported ROM still needs a parked test.
- An unsupported DSC can report the DSC, LMV, and VIP module indexes via **Refresh status**. Upstream has a distinct [G-series VIP control path](https://github.com/cauliflowermossgrape/idrive7-apps/blob/35bca28bc82b30482d73782877ebaf92c69dcb8c/apps/live_adjustments/README.md#xdrive-experimental). The ID6 candidate keeps its existing DSC and LMV write allowlist.
- HUD Gauges accepts a verified gateway IPv4 address and distinguishes missing vehicle data from an overlay rejection. The F06 and F16 in [issue #82](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/82) and [issue #36](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/36) reject the reference HUD text handle. The worker checks the reference text handle before changing visibility or drawing. Their layouts still need a parked capture of the factory HUD handle setup. st1ng's [CarPlay Advanced protocol notes](https://github.com/st1ng/nbtevo-carplay-advanced/blob/main/AGENTS.md) describe observing the factory handle setup, and document why the fixed 6WA handles cannot be assumed across clusters.
- HUD Gauges starts only when enabled for the current HMI session. The worker checks for native NBT-AAD and the CarPlay Advanced HUD hook before registering on MOST. BoostDeck, ShiftDeck, LaunchDeck and WarmDeck retain HSFZ diagnostics when native NBT-AAD is present. Their MOST fallback is disabled in that configuration. HUD Gauges yields its MOST client when a dashboard needs the MOST diagnostics tunnel. The affected combinations still need an on-car check. [Issue #22](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/22) · [Issue #105](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/105)
- HUD uses the active dashboard worker's RPM stream. BoostDeck can also share a current boost reading from its selected channels over HSFZ. ShiftDeck shares RPM only. The HUD overlay also pauses while LaunchDeck or WarmDeck uses the link. [Issue #27](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/27) · [Issue #88](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/88) · [Issue #107](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/107)
