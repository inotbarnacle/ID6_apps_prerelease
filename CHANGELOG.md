# Change notes

## Unreleased source changes

- BoostDeck adds optional read-only EGR request/error and A/B request/actual/error gauges. PID and per-field support gates, grouped polling and explicit labels keep error separate from actual duty/position. Automatic diesel defaults are unchanged.
- Existing EGT packets and B3 channel bounds are preserved. EGR uses appended IDs, NBG9 and B4 assignments; older binaries need their backed-up settings restored on rollback.
- BoostDeck adds standard EGT bank/sensor gauges with PID and sensor-support gating, independent freshness and bounded polling. Detected defaults choose at most one EGT. No exhaust-location or regeneration inference is added.
- Existing signal IDs, ordinary NBG7 packets and B1/B2 settings retain their original meaning. EGT readings use 64-bit masks, NBG8/NBC2 frames and B3 assignments; matching plugin and worker builds are required. These changes are host-tested source, not N57 or QNX runtime approval.

## v0.7.18

- Renames the project to **iDriveDeck**.
- Adds 12 native EntryNav2 menus to the desktop installer under **EntryNav2 (early beta)**.
- Adds independent full-screen NBT Evo ID6 apps under **My Vehicle**, with layouts for five panel sizes from 800 × 480 to 1920 × 720.
- Gives MHD+ Suite, Bootmod3 Suite, and MG Flasher a shared full-screen design with their own logos and controls.
- Displays ShiftDeck RPM in 10 RPM steps and adds shift-light targets from 6,000 to 8,000 RPM.
- Backs up and replaces previously installed managed menus. Retires HUD Gauges from the installer.
- Adds **ID6 App Updates** to the full-screen bundle for future signed USB updates.

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
