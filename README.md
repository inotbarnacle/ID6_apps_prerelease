# iDriveDeck

Vehicle gauges, tuning controls, diagnostics, and more under **My Vehicle** on BMW NBT Evo ID6 and EntryNav2.

[Download v0.7.19 for macOS or Windows](https://github.com/inotbarnacle/iDriveDeck/releases/tag/installer-v0.7.19) · [Installation guide](Installer.md) · [Change notes](CHANGELOG.md)

## NBT Evo ID6

Choose **NBT Evo** in the desktop installer. **Install** gives you the standard iDrive menus. **Install full-screen apps** gives each app its own entry under **My Vehicle**. Open an entry to go directly to its full-screen dashboard. Turn the iDrive knob to highlight a control and press it to select.

Full-screen mode detects 800 × 480, 960 × 480, 1280 × 480, 1440 × 540 or 1920 × 720 and uses the matching layout. The bundle includes BoostDeck, ShiftDeck, WarmDeck, LaunchDeck, Engine Fault Codes, Road Jet, BEAMer and your choice of MHD+ Suite, Bootmod3 Suite, or MG Flasher. HybridDeck and experimental xDrive Control are optional.

| Bootmod3 Suite | MG Flasher |
| --- | --- |
| ![Bootmod3 Suite full-screen on ID6](images/bootmod3-fullscreen.png) | ![MG Flasher full-screen on an 800 × 480 ID6 display](images/mg-flasher-fullscreen.png) |

## EntryNav2 (early beta)

The same macOS and Windows installer now includes 12 native EntryNav2 menus. Choose **EntryNav2 (early beta)** and enter the head unit IP and its existing SSH password over wired ENET. The installer selects matching builds for W2408C1R, Q2051B1R and C1643D1R, with a beta fallback for unrecognized framework pairs. EntryNav2 uses the native iDrive view under **My Vehicle** on 1280 × 480 or 6.5-inch 800 × 480 displays. Select the panel size in the installer.

The available apps include BoostDeck, ShiftDeck, WarmDeck, LaunchDeck, HybridDeck, Engine Fault Codes, Road Jet, BEAMer, MHD+ Suite, Bootmod3 Suite, MG Flasher, and experimental xDrive Control. Vehicle data and tuning functions depend on the corresponding ECU and tune.

## Replacing older apps and future updates

The new installer backs up and removes the previous iDriveDeck or ID6Apps menus that it manages, then installs your selected set. Select every app you want to keep. Record the backup location and restart iDrive after installation. HUD Gauges is retired from the installer.

Full-screen ID6 adds **My Vehicle → ID6 App Updates**. That menu will be used for future signed USB app updates. Use the desktop installer for this release.

[Build and release source](source/docs/RELEASE-BUILD.md) covers the standard NBT Evo apps, full-screen apps, and EntryNav2 ports.

## Features


- **BoostDeck** has nine configurable gauges. Open **OPTIONS → BoostDeck settings** to set pressure (psi, bar, or kPa), speed (mph or km/h), and temperature (°F or °C) independently, or switch between bars and circular dials. Picking a value returns to settings and shows the saved choice. **Configure gauges** and **Engine profile** are in the same menu. Gasoline and diesel keep separate gauge assignments.
- **BoostDeck alerts** use fresh vehicle readings while the dashboard is open. Set separate coolant and oil-temperature limits from 220–260 °F in 5 °F steps, or a low oil-pressure limit from 5–25 psi in 5 psi steps. The choices show in your selected units. Each starts off. Choose **Screen only** or **Screen + soft chime** under **Alert mode**. Units, dial style, thresholds, and alert mode survive an iDrive restart.
- **WarmDeck** shows engine oil, coolant, and transmission temperatures, with targets of 85, 75, and 40 °C. It estimates the warm-up time when current readings show a rising trend, and displays an advisory RPM guide while warming up. It reads only while open. The temperature targets are WarmDeck's display thresholds. [WarmDeck package](packages/warmdeck.zip)
- **LaunchDeck** times 0–60 and 60–130 mph or 0–100 and 100–200 km/h. The menu shows a green **Ready** check, **Measuring** during a run, and the last three results for each mode. Display resolution is 0.01 s, with measurement precision limited by OBD speed sampling. [LaunchDeck package](packages/launchdeck.zip)
- **ShiftDeck** displays live speed, an RPM bar, and shift lights with a selectable RPM threshold. Press **OPTIONS** to switch between mph and km/h. The readout updates in 10 RPM steps. The shift-light target is adjustable from 6,000 to 8,000 RPM in 100 RPM steps.
- **MHD+ Suite** offers four map slots and puts **Exhaust Flap** and **Max Cooling** under **Options**. The main screen shows the last map and Max Cooling settings accepted by the DME during the current app session.
- **HybridDeck** displays plug-in hybrid battery charge, health, power, voltage, current, and temperatures. [HybridDeck package](packages/hybriddeck.zip)
- **MG Flasher** offers map slots, burble slots, and Max Cooling. [MG Flasher package](packages/mgflasher.zip)
- **Bootmod3 Suite** is a control menu for compatible CustomROM tunes. It offers four map slots, exhaust flap control, Active Sound, and burble presets.
- **xDrive Control (experimental)** temporarily selects rear-wheel drive on supported DSC versions. Mode changes require confirmed Park and zero speed. The F-series DSC_25 (0F1860) with LMV 0F1480 requires the engine running. The supported G-series DSCs require the engine off. See the [compatibility notes](source/docs/COMPATIBILITY.md#xdrive-control-experimental).
- **Engine Fault Codes** reads engine/DME fault codes and shows descriptions when available. After a confirmed clear, it rereads the fault memory and reports codes that remain. Park with ignition on and the engine off before confirming a clear.
- **Road Jet** uses throttle and steering to control the jet. It plays at up to 8 km/h (about 5 mph) and pauses when speed data is unavailable.
- **BEAMer** offers Front, Rear, Both, and Insane patterns on supported lighting modules. It checks for a valid speed of 8 km/h (about 5 mph) or less before each lighting step.

## Contributors and credits

- **MG Flasher** branding in the tuning menu comes from [MG Flasher](https://mgflasher.com/).
- **Ivan Hryhoriev** contributed the original WarmDeck prototype and warm-up dashboard design.
- **EntryNav2** builds draw on the MIT-licensed [EntryNav-Apps project](https://github.com/CallMeLewis/EntryNav-Apps).
- **NBT-AAD** by nanaisu supplied the QNX Screen interface used by the full-screen renderer. [NBT-AAD source and license](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)

## iDrive menus

### General menus

<table>
  <tr>
    <td><img src="images/boostdeck-circular-gauges.png" alt="BoostDeck with larger circular gauges" width="300"><br>BoostDeck circular gauges</td>
    <td><img src="images/hybriddeck.png" alt="HybridDeck showing high-voltage battery health and other readings" width="300"><br>HybridDeck</td>
  </tr>
  <tr>
    <td><img src="images/01-boostdeck.png" alt="BoostDeck on iDrive" width="300"><br>BoostDeck</td>
    <td><img src="images/02-shiftdeck.png" alt="ShiftDeck on iDrive" width="300"><br>ShiftDeck</td>
  </tr>
  <tr>
    <td><img src="images/10-launchdeck.png" alt="LaunchDeck ready for a 0–60 mph run" width="300"><br>LaunchDeck</td>
    <td><img src="images/11-warmdeck.png" alt="WarmDeck showing the three temperature targets reached" width="300"><br>WarmDeck</td>
  </tr>
  <tr>
    <td><img src="images/06-engine-fault-codes.png" alt="Engine Fault Codes on iDrive" width="300"><br>Engine Fault Codes</td>
    <td><img src="images/07-road-jet.png" alt="Road Jet on iDrive" width="300"><br>Road Jet</td>
  </tr>
  <tr>
    <td><img src="images/08-beamer.png" alt="BEAMer on iDrive" width="300"><br>BEAMer</td>
    <td><img src="images/09-xdrive-control.png" alt="xDrive Control main menu" width="300"><br>xDrive Control (experimental)</td>
  </tr>
</table>

### MHD specific

<table>
  <tr>
    <td><img src="images/03-mhd-suite.png" alt="MHD+ Suite on iDrive" width="300"><br>MHD+ Suite</td>
    <td><img src="images/04-exhaust-flap.png" alt="Exhaust Flap on iDrive" width="300"><br>Exhaust Flap</td>
  </tr>
  <tr>
    <td><img src="images/05-max-cooling.png" alt="Max Cooling on iDrive" width="300"><br>Max Cooling</td>
  </tr>
</table>

### MG Flasher

<table>
  <tr>
    <td><img src="images/mg-flasher-map-switcher.png" alt="MG Flasher map slot selection" width="300"><br>Map Switcher</td>
  </tr>
</table>

### Bootmod3 specific

<table>
  <tr>
    <td><img src="images/bootmod3-map-switcher.png" alt="Bootmod3 map switcher" width="300"><br>Map Switcher</td>
    <td><img src="images/bootmod3-exhaust-flap.png" alt="Bootmod3 Exhaust Flap" width="300"><br>Exhaust Flap</td>
  </tr>
  <tr>
    <td><img src="images/bootmod3-active-sound.png" alt="Bootmod3 Active Sound" width="300"><br>Active Sound</td>
    <td><img src="images/bootmod3-burbles.png" alt="Bootmod3 Burbles" width="300"><br>Burbles</td>
  </tr>
</table>
