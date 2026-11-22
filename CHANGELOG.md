# Change notes

## Unreleased

- **Intercooler coolant gauge.** BoostDeck can show the low-temperature intercooler circuit coolant (DID `EC3F`, signed 2 bytes, raw / 128 °C) on the G-series DMEs whose SGBD defines it (DME982, DME98, DME9X2). The frame format is now `NBG8` (32 channels); `NBG7` is still accepted. Not read on a car.
- **Intercooler coolant is selectable.** The gauge menus of BoostDeck now list "Intercooler coolant" for every gauge; the signal was read by the worker but had no menu entry, so it could never be shown.
- **DID audit against the SGBDs.** Every `dme_signal_table.h` row was checked against its SGBD. Fixes intake air on DME982 and DME9X2, which is one byte, not two. Adds ignition timing `59FA` for six more SGBDs, DME98 oil pressure `EF58`, and transmission temperature rows for GSAW01, GSAW02FH/FP, GKEB23TU, GKEG3HY and GSB231. None of these was read on a car.
- **Gauge plugins stop polling when hidden.** ShiftDeck, LaunchDeck, BoostDeck, WarmDeck and HybridDeck no longer re-arm their poll timer once the page is hidden and the worker has exited.
- **Less work per tick.** The HUD menu polls once a second instead of every 200 ms, BoostDeck redraws only changed gauges, and ShiftDeck and LaunchDeck send only changed digits.
- **Fixes a new worker being started on every tick** when the HMI ignores `SIGCHLD`. Plugins now confirm with `kill(pid, 0)` before forgetting a worker pid.
- Adds host tests for the plugins against a fake BMW framework (`make test-plugins`) and `make check-reap`.
- **MOST fallback for every worker.** Workers now share one diagnostic link that switches to the head unit's MOST tunnel when the HSFZ gateway cannot be reached. This now covers xDrive Control, Beamer, Hybrid Deck, MG Flasher, Warm Deck, Road Jet and the HUD reader, as well as the workers that already had it. Only the Bootmod3 burble write stays on HSFZ, because it is larger than the tunnel limit. Workers stay on HSFZ when native NBT-AAD (Android Auto) is installed.
- **Reads are not limited to the DME.** The tunnel carries the target ECU address, so EGS and DSC reads work over MOST too. BoostDeck and Warm Deck now read the transmission temperature over MOST. Probed read-only on one car, not yet run in these builds on a car.
- Adds a host test that every ECU address and UDS service reaches the tunnel.
- **Per-car DME identification (`22 F150`).** BoostDeck reads the DME's SGBD index once per connection and, for the SGBDs with calibration (including the S55 on F82 M4 and the DDE741 research), uses that SGBD's own DIDs, widths and scaling for the DME gauges. Unknown SGBDs and signals keep the existing defaults. The S55 coolant is now one byte, which fixes the blank coolant gauge in [issue #117](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/117).
- **OBD PID fallback.** Where the identified SGBD has no verified DID, BoostDeck reads coolant, intake air, rail pressure and boost as standard Mode 01 PIDs first, because the diesel DDEs reuse those DIDs for other quantities. A verified DID is still read first, and timing and AFR fall back to their PIDs. Not run on a car.
- **Diesel DDE rows.** D70BX7A0, D72N47A0, D73N57A0, D73N57E0, D75BX7A0 and D83BX7C0 get their own ambient, charge pressure, rail, coolant and intake-air DIDs from their SGBDs. A car whose SGBD has a row reads only the DIDs listed for it, with PID fallback; without a row it keeps the defaults. Not run on a car.
- **EGS identification.** BoostDeck also identifies the EGS and uses a verified transmission-temperature DID for it, including the DCT SGBDs.
- **xDrive Control names the DSC** from `22 F150` and lists the 11 known DSC/IB SGBDs. Control writes stay limited to the SGBDs already verified.
- Adds the `--identify` mode to the signal probe, and `docs/F150.md` with the J-series scope, DDE741 candidates and the OBD PID fallback research.
- **Layouts use the full screen, on a 6-unit grid with 12-unit gutters and margins.** The layouts were migrated to the HMI's 1440 × 540 design-unit canvas, which the head unit scales to the panel, so every app is now authored directly for the 1260 × 477 content area instead of being scaled from a smaller base. Cards and tiles are equal-width rows exactly 12 units apart, and every layout ends 12 units from the top, bottom and right of the content area (rows span 1248 units, from y = 12 to 465). The 12-unit bottom margin and four-across cards (303 wide) are the only places off the 6-unit grid. See `docs/LAYOUT.md`.
- **Compact 800 × 480 and 960 × 480 layouts are centered vertically.** They are still scaled uniformly, so they are shorter than the content area; each page is now centered in it instead of sitting at the top.

## v0.7.6

- Adds MG Flasher live controls and HybridDeck for plug-in hybrid telemetry, including battery health where reported.
- BoostDeck gets larger circular gauges, radio-button settings, gasoline rail-pressure fallback, and improved handling of unavailable signals and an occupied HUD sharing port.
- Bootmod3 detects supported ROMs automatically. Exhaust flap and Active Sound work independently of the ROM lookup.
- The installer qualifies firmware variants by two matching framework hashes and reads firmware from Jacinto first on split-node head units.
- Engine Fault Codes gains a wider code column and a read-only diagnostic trace.

- **v0.7.5** adds T24081I as an exact-hash alias of Y24073J. The Windows and macOS installers offer opt-in [CarPlay Advanced HUD by @st1ng](https://github.com/st1ng/nbtevo-carplay-advanced).

- **v0.7.5-rc9** labels the ethanol gauge tile `E%`.

- **v0.7.5-rc8** shortens BoostDeck tile captions to fit the bars and dials, with abbreviated captions for the narrow gauge.

- **v0.7.5-rc7** LaunchDeck uses a dedicated telemetry port so its menu can coexist with BoostDeck.

- **v0.7.5-rc6** adopts the standalone QNX worker and RHMI plugin layout from [Andreas Boehm’s PR #109](https://github.com/inotbarnacle/ID6_apps_prerelease/pull/109).

- **v0.7.5-rc5** uses the measured engine-running Park response for DSC_25 with LMV 0F1480. [zelnkup’s PR #114](https://github.com/inotbarnacle/ID6_apps_prerelease/pull/114)

- **v0.7.5-rc4** narrows the BoostDeck low-oil-pressure alert choices to 5–25 psi. Earlier saved thresholds above 25 psi load as 25 psi.

- **v0.7.5-rc3** adds WarmDeck to the native packages and desktop installer. It shows warm-up progress for engine oil, coolant, and transmission fluid from existing read-only diagnostics.
- **xDrive documentation** records FreZZZeR's factory SGBD cross-check and zelnkup's follow-up drive observations for DSC_25.

- **v0.7.5-rc2 BoostDeck** adds saved unit combinations, circular dials, and configurable coolant, oil temperature, and low oil pressure alerts with a soft chime option. Choice lists close after selection and show the saved value on the settings page. The compact dial artwork follows [lucasaviao’s image-size findings](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/55#issuecomment-6975682884).
- **Engine Fault Codes** waits for a clear response, verifies fault memory in the active session, and reports when the ECU acknowledges the clear but codes remain. A possibly delivered clear is not retransmitted.

- **Desktop installer v0.7.5-rc1** reuses one logical layout on 8:3 displays and stages compact layouts on narrower panels, based on Andreas Boehm’s measurements. It adds W20204I and recognises the submitted J16383I and O18184I capture-only profiles.
- **HUD Gauges** starts on demand per iDrive session, shares dashboard telemetry, and yields to native projection add-ons and dashboards using the MOST diagnostic tunnel. Its reference HUD handles still require compatible cluster hardware.

- **Engine Fault Codes and MHD+ Suite** wait up to three seconds for another app to release diagnostics. MHD+ sends the requested map command once after acquiring the gateway.
- **HUD Gauges** polls after a 30 ms gap between reads and checks for redraws every 5 ms.
- **Desktop installers v0.7.4 beta** add exact framework matches for M17345I, R19101I, S19221I, and T23235J.

- **BoostDeck** has nine configurable gauge positions in the original layout and separate saved gasoline and diesel layouts. New choices include OBD oil temperature, control module voltage, RPM, load, air flow, and fuel rate.
- **LaunchDeck** adds 0–60 and 60–130 mph plus 0–100 and 100–200 km/h timing with three saved results per mode.
- **MHD+ Suite** shows the last map and Max Cooling settings acknowledged by the DME.
- **xDrive Control** incorporates the DSC_25/LMV 0F1480 profile with engine-running, Park, and zero-speed checks.
- **Diagnostic gateway** lookup supports a shared address and link-local HSFZ discovery. Bootmod3 and HUD Gauges report more specific compatibility failures.

- **Desktop installers v0.7.3 beta** add eleven firmware and framework profiles and manual firmware and display selection when automatic detection cannot read them.
- **BoostDeck** saves color and units across iDrive restarts. **ShiftDeck** saves units and shift-light RPM.

- **MOST fallback** covers Bootmod3 map switching, Exhaust Flap and Active Sound, plus the dedicated Exhaust Flap submenu in MHD+ Suite. Bootmod3 burble presets and xDrive still require HSFZ.
- **Desktop installers v0.7.1 beta** embed both rebuilt workers.

- **BoostDeck** has faster gauge updates, a new layout, an Active Ethanol gauge, and metric measurements under OPTIONS.
- **ShiftDeck** performance improvements.
- **MHD+ Suite** groups map switching, exhaust flap control, and Max Cooling. The new Max Cooling tiles show the last request accepted by the DME.
- **Bootmod3 Suite** adds a new control menu for compatible CustomROM tunes. Beta.
- **xDrive Control (experimental)** adds an xDelete-style menu for temporary rear-wheel drive on supported DSC variants.
- **Engine Fault Codes** reads engine codes and shows their descriptions when available. It can read codes even when the DME refuses to clear them.
- **HUD Gauges** adds a curved windshield tachometer and numeric boost in PSI, with a persistent on/off checkbox. The packaged build targets the reference 340i with its factory HUD.
- **Dashboard diagnostics** uses the MOST fallback from PR #9 in the packaged workers. A per-app gateway address can be set in gateway-ip.txt.
- **Exhaust Flap** moves under MHD+ Suite → Options, alongside Max Cooling.
- **Desktop installers** add a read-only compatibility check and recognize matching N18092I and P18362I framework captures.
- **xDrive Control (experimental)** distinguishes missing DSC replies, negative replies, and detected DSC indices outside the write allowlist.
