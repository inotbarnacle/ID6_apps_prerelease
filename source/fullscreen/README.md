# ID6 Full-screen Apps

For NBT Evo ID6. The reference head unit is `NBTevo_N18032I`. The desktop
installer sets the renderer and iDrive layout for 800 × 480, 960 × 480,
1280 × 480, 1440 × 540 or 1920 × 720.

## Open in IDEmu

Start **ID6 Full-screen Apps**. Each dashboard has its own **My Vehicle** entry.
Select ShiftDeck, BoostDeck, the chosen tuning suite, Engine Fault Codes,
Road Jet, BEAMer, LaunchDeck or WarmDeck to open it. xDrive Control and
HybridDeck are optional full-screen menus. Press Back to return to
My Vehicle. **ID6 App Updates** is also available in My Vehicle.

Live dashboards read available telemetry every 16 ms while open and pass the
new state to the renderer on the same tick. ShiftDeck's numerical RPM and shift
lights use the latest sample. Its tach bar eases over 32 ms. Select **Target RPM** or the **Shift light target** option to choose 6,000–8,000 RPM in steps of 100. Open **OPTIONS → Speed units** to switch between mph and km/h. The renderer
skips unchanged frames and the app sends a periodic keepalive at idle.

Turn the iDrive knob to move the blue highlight between buttons. Press the
knob to activate the highlighted button. In IDEmu, MHD+ Suite and BEAMer show
the selected control in the status line.

## BoostDeck settings

Open BoostDeck and select **Settings**. **Configure gauges** assigns a signal
to any of the nine positions. Pressure, speed, and temperature units are
independent. Settings also include bars or circular dials, colors, the engine
profile, and the screen and chime alerts. Selecting a value returns to the
previous menu with the saved choice visible.

Gasoline and diesel retain separate assignments. The Library reads standalone
BoostDeck preferences as its initial defaults when available and saves its
own choices separately. IDEmu keeps the Library's files in its test profile
across cold boots.

## WarmDeck temperature targets

Oil, coolant and transmission show independent unknown/pending/met states.
Comparisons use measured Celsius values and hysteresis before display rounding
or unit conversion. Missing readings do not hide the other fluids' progress.
Estimates need fresh measurements and rising trends. These targets are app
choices; follow the vehicle's guidance.

[Behavior, host preview and validation limits](../docs/WARM-READINESS.md#full-screen-warmdeck)
cover the renderer and RHMI fallback. This revision uses surface protocol v2
with the same 496-byte state size. Rebuild the complete matching bundle,
including renderer, plugins and optional companions; update the emulator's
renderer and overlay together. Earlier packets are rejected. Use fresh build
outputs and keep the complete previous bundle for rollback. Firmware and
display checks still apply.

## USB packages

Place the signed `ID6Apps.update` at the top of the USB drive, then open **My Vehicle → ID6 App Updates**.
Choose an action and press **Confirm** to start it.

- **Update installed apps** applies newer entries to apps already installed.
- **Install new apps** adds eligible missing apps from the package to the native
  **My Vehicle** menu. Fresh installs skip Bootmod3 and xDrive. Restart iDrive
  after either operation reports success.

The worker checks the signature, matching framework hashes, and display profile.
It uses the desktop installer's shared storage lock. The signed package installs
standalone My Vehicle apps. The full-screen variants are installed through this QNX bundle.

## Native builds

The native build script writes the QNX plugins to `native/out`. Each dashboard has a separate RHMI plugin. They share
the renderer, workers, and measured display settings in the `ID6LibraryLab`
folder. That folder also registers ID6 App Updates and replaces the former
ID6 App Library drawer when the complete bundle is deployed.

The connector IDs are 229 and 231–242. Check for conflicts and verify firmware and
display compatibility before staging on a head unit. Install the folders as
a set. Remove or move aside any older standalone editions with the same menu
names if you want one entry per app. Back up the installed versions first.

The renderer owns a full-panel QNX Screen window while its RHMI state has
focus. It releases its LayerManager surface on focus loss; RHMI provides
the entry point, controls, and a fallback view.

The bundle includes BoostDeck, ShiftDeck, and WarmDeck telemetry, LaunchDeck
timing and engine fault codes. Engine Fault Codes shows descriptions where
available. **Clear codes** opens a confirmation page before clearing engine
fault memory and re-reading the result.

- **MHD+ Suite** includes four map slots, exhaust flap open, auto and closed,
  and Max Cooling on and off. The page shows worker replies and the last
  accepted map and cooling request from this session. The existing engine
  RPM check still applies to Max Cooling on.
- **Road Jet** uses accelerator and steering data. The scene pauses above
  8 km/h (about 5 mph) or when the speed reading is unavailable. With MOST
  fallback, the game uses pedal input if the DME supplies speed and pedal data.
- **BEAMer** includes Front, Rear, Both, Insane, and Stop. The worker checks
  lighting-module support and checks for a valid speed of 8 km/h or less before
  each step. Leaving the page
  stops the worker. The diagrams reflect acknowledged lamp pulses.

Upstream reported tests of the separate apps and overlay transitions on its
1280 × 480 IDEmu profile. Those results precede the WarmDeck readiness and
surface-v2 changes here. This revision has host tests; its QNX build, ARM
emulator and parked vehicle checks remain pending, including transitions to
OEM camera and projection screens.

## Compatibility and staging

See [COMPATIBILITY.md](COMPATIBILITY.md) for how this project handles unknown
firmware and what the upstream installers actually check.

```sh
python3 native/check-frameworks.py /absolute/path/to/captured/frameworks
python3 native/prepare.py 1280x480 /absolute/path/to/new/staging-folder
```

Staging selects the layout for the measured display and produces only local
files. Use 800x480 or 960x480 for the compact variants. The 1280x480,
1440x540 and 1920x720 choices share one logical layout. Use the optional
gateway argument only when you have the correct vehicle-specific address.
An unknown firmware can pass the symbol check, but RHMI registration and
telemetry still need a parked runtime test.

## Rebuild locally

Run `python3 native/generate.py` and `python3 native/generate-surface.py` from this
directory to regenerate the RHMI files, Mac preview catalogue, and Screen titles.
Run `python3 native/generate-companions.py /path/to/ID6-Apps/source/native`
to regenerate the optional full-screen menus with Pillow. Set
`ID6_STANDARD_NATIVE_DIR` to that absolute source path when building them.
The checked-in glyph atlas is generated by `python3 native/generate-font.py` with Pillow.
Then run `sh native/build.sh` in a
licensed QNX SDP 6.5 Linux environment with `NBTEVO_FRAMEWORKS_DIR` and
`NBTEVO_RUNTIME_DIR` pointing at private authorised link inputs.
The runtime directory needs `libscreen.so.1` to build the full-screen renderer.
Set `QNX_OPENSSL_DIR` to a directory with QNX ARM OpenSSL headers in `include`
and the static library in `lib/libcrypto.a` for the signed USB worker.
Those proprietary libraries are dependencies on the head unit and are
excluded from this project.

## Credits

- Robert Nisipeanu and the MGU Store contributors for the app library concept. [MGU Store](https://github.com/robertnisipeanu/mgu-store)
- nanaisu for NBT-AAD and its QNX Screen interface. The reused header carries its PolyForm Noncommercial notice. [NBT-AAD](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
- The Redline contributors for the shift-dashboard reference. [Redline](https://github.com/cauliflowermossgrape/idrive7-apps/blob/main/docs/media/redline.gif)
- Jeremy Tribby for Barlow Condensed. [Font license](native/fonts/OFL.txt)
- ID6Apps contributors for the existing apps and screens. Credit for WarmDeck goes to Ivan Hryhoriev and credit for CarPlay Advanced goes to st1ng. [ID6Apps credits](https://github.com/inotbarnacle/ID6_apps_prerelease#contributors-and-credits)

## IDEmu telemetry

In **ID6 Full-screen Apps**, the dashboards receive RPM, speed, coolant, gear, pedal
and steering from IDEmu's vehicle controls. BoostDeck and WarmDeck label the
feed as simulated. Boost, intake temperature, oil pressure, ethanol, oil
temperature and transmission temperature use emulator-only test values because
IDEmu does not provide those readings in its standard signal list. LaunchDeck
uses the normal timer with IDEmu's speed. Road Jet uses IDEmu's speed,
pedal and steering. Physical builds continue to use their diagnostic workers.
