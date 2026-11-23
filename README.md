# NBT Evo ID6 apps

Connect your parked car to a computer over wired ENET. Open an AI agent with terminal access on that computer and give it this repository and [AgentReadMe.md](AgentReadMe.md). The agent identifies your head unit, checks compatibility, and installs your chosen apps under **My Vehicle**.


## Unsupported firmware? Run the Trio Compatibility Collector

If your version is unsupported, enable temporary SSH with Bavarian SSH Client first. Run the [Trio Compatibility Collector](source/tools/trio-collector/Quick%20Start.md) and email the ZIP it creates to [nordicbmwdealer@gmail.com](mailto:nordicbmwdealer@gmail.com) so we can get a fix to you quickly.

## Desktop installer (beta)

[Download the macOS or Windows installer](https://github.com/inotbarnacle/ID6_apps_prerelease/releases/tag/installer-v0.7.6). Follow the [setup, install, and uninstall guide](Installer.md). Temporary SSH must be enabled before opening the installer.

[NBTApps](https://github.com/tom-pryor/nbtapps) also distributes these apps through its [community catalogue](https://github.com/tom-pryor/nbtapps-packages). The v0.7.5 release includes the package handoff for the catalogue maintainer. Check your firmware against the [compatibility matrix](source/docs/COMPATIBILITY.md) before installing. HUD Gauges and xDrive Control are experimental. **HUD Gauges are unavailable whenever native NBT-AAD (Android Auto) is installed**, even while Android Auto is stopped.

The installer also offers [@st1ng’s CarPlay Advanced](https://github.com/st1ng/nbtevo-carplay-advanced) in HUD-only mode for turn-by-turn directions. It downloads the original release and runs its installer as a separate operation.

## iDrive 6 compatibility

The porting workflow can be used to evaluate any iDrive 6 version. Prebuilt packages target the verified reference firmware and framework combination documented in the AgentReadMe. Other versions need a compatibility check and may need a car-specific build. HUD Gauges also requires a compatible factory windshield HUD.

## Tell your agent what to install

Choose **MHD+ Suite**, **Bootmod3 Suite**, or **MG Flasher**. [Download the dashboard package](packages/dashboard.zip). Also say whether to install **xDrive Control** and **HUD Gauges**. Both are experimental.

> My car is parked, iDrive is awake, and this computer is connected over wired ENET. Follow the AgentReadMe to install BoostDeck, ShiftDeck, and Engine Fault Codes. My tuning menu choice is **[MHD+ Suite / Bootmod3 Suite / MG Flasher]**. My xDrive Control (experimental) choice is **[yes / no]**. My HUD Gauges choice is **[yes / no]**. Verify firmware, framework hashes, and display resolution before uploading. Ask me when temporary SSH or a parked restart is needed.

[Quick Start](Quick%20Start.md) · [Uninstall](AgentReadMe.md#uninstall) · [Change notes](CHANGELOG.md) · [Native source](source/)

## Features

- **BoostDeck** has nine configurable gauges. Open **OPTIONS → BoostDeck settings** to set pressure (psi, bar, or kPa), speed (mph or km/h), and temperature (°F or °C) independently, or switch between bars and circular dials. Picking a value returns to settings and shows the saved choice. **Configure gauges** and **Engine profile** are in the same menu. Gasoline and diesel keep separate gauge assignments.
- **BoostDeck alerts** use fresh vehicle readings while the dashboard is open. Set separate coolant and oil-temperature limits from 220–260 °F in 5 °F steps, or a low oil-pressure limit from 5–25 psi in 5 psi steps. The choices show in your selected units. Each starts off. Choose **Screen only** or **Screen + soft chime** under **Alert mode**. Units, dial style, thresholds, and alert mode survive an iDrive restart.
- **WarmDeck** shows engine oil, coolant, and transmission temperatures, with targets of 85, 75, and 40 °C. It estimates the warm-up time when current readings show a rising trend, and displays an advisory RPM guide while warming up. It reads only while open. The temperature targets are WarmDeck's display thresholds. [WarmDeck package](packages/warmdeck.zip)
- **LaunchDeck** times 0–60 and 60–130 mph or 0–100 and 100–200 km/h. The menu shows a green **Ready** check, **Measuring** during a run, and the last three results for each mode. Display resolution is 0.01 s, with measurement precision limited by OBD speed sampling. [LaunchDeck package](packages/launchdeck.zip)
- **ShiftDeck** displays live speed, an RPM bar, and shift lights with a selectable RPM threshold. Press **OPTIONS** to switch between mph and km/h. This update includes performance improvements.
- **MHD+ Suite** offers four map slots and puts **Exhaust Flap** and **Max Cooling** under **Options**. The main screen shows the last map and Max Cooling settings accepted by the DME during the current app session.
- **HybridDeck** displays plug-in hybrid battery charge, health, power, voltage, current, and temperatures. [HybridDeck package](packages/hybriddeck.zip)
- **MG Flasher** offers map slots, burble slots, and Max Cooling. [MG Flasher package](packages/mgflasher.zip)
- **Bootmod3 Suite** is a control menu for compatible CustomROM tunes. It offers four map slots, exhaust flap control, Active Sound, and burble presets.
- **xDrive Control (experimental)** temporarily selects rear-wheel drive on supported DSC versions. Mode changes require confirmed Park and zero speed. The F-series DSC_25 (0F1860) with LMV 0F1480 requires the engine running. The supported G-series DSCs require the engine off. See the [compatibility notes](source/docs/COMPATIBILITY.md#xdrive-control-experimental).
- **Engine Fault Codes** reads engine/DME fault codes and shows descriptions when available. After a confirmed clear, it rereads the fault memory and reports codes that remain. Park with ignition on and the engine off before confirming a clear.
- **Road Jet**. A simple game that makes a jet move based on throttle input and steering angle. [Road Jet package](packages/road-jet.zip)
- **BEAMer** offers Front, Rear, Both, and Insane lighting patterns in Park at zero speed. Tested on an F3x car. Other Fxx cars may work if they have the supported lighting module. Gxx has not been tested. [BEAMer package](packages/beamer.zip)
- **HUD Gauges** projects a live curved tachometer and numeric boost in PSI onto the factory windshield HUD. The tach spans 0–8,000 RPM, with a thicker redline zone starting at 6,700 RPM. Enable **My Vehicle → HUD Gauges → Show HUD gauges** to keep the display on after leaving the menu for the current iDrive session. Uncheck it to clear the display. The overlay yields when it detects factory navigation guidance. [HUD Gauges package](packages/hud-gauges.zip)

## Contributors and credits

- **MG Flasher** branding in the tuning menu comes from [MG Flasher](https://mgflasher.com/).
- **Ivan Hryhoriev** contributed the original WarmDeck prototype and warm-up dashboard design.
- **CarPlay Advanced HUD** was created by [@st1ng](https://github.com/st1ng). The optional installer uses st1ng’s original release. [CarPlay Advanced source and license](https://github.com/st1ng/nbtevo-carplay-advanced)

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

### HUD Gauges

**Only tested on F3X vehicles**

<table>
  <tr>
    <td><img src="images/03_hud_white.jpg" alt="HUD Gauges on an F3X windshield in white" width="300"></td>
    <td><img src="images/04_hud_orange.jpg" alt="HUD Gauges on an F3X windshield in orange" width="300"></td>
  </tr>
</table>
