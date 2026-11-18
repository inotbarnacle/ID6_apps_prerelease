# NBT Evo ID6 apps

Connect your parked car to a computer over wired ENET. Open an AI agent with terminal access on that computer and give it this repository and [AgentReadMe.md](AgentReadMe.md). The agent identifies your head unit, checks compatibility, and installs your chosen apps under **My Vehicle**.

## Desktop installer (beta)

[Download the macOS or Windows installer](https://github.com/inotbarnacle/ID6_apps_prerelease/releases). Follow the [setup, install, and uninstall guide](Installer.md). Temporary SSH must be enabled before opening the installer.

## iDrive 6 compatibility

The porting workflow can be used to evaluate any iDrive 6 version. Prebuilt packages target the verified reference firmware and framework combination documented in the AgentReadMe. Other versions need a compatibility check and may need a car-specific build. HUD Gauges also requires a compatible factory windshield HUD.

## Tell your agent what to install

Choose **MHD+ Suite** or **Bootmod3 Suite**. [Download the dashboard package](packages/dashboard.zip). Also say **yes or no to **xDrive Control (experimental)**** and **yes or no to HUD Gauges**. Bootmod3 Suite is in beta.

> My car is parked, iDrive is awake, and this computer is connected over wired ENET. Follow the AgentReadMe to install BoostDeck, ShiftDeck, and Engine Fault Codes. My tuning menu choice is **[MHD+ Suite / Bootmod3 Suite]**. My xDrive Control (experimental) choice is **[yes / no]**. My HUD Gauges choice is **[yes / no]**. Verify firmware, framework hashes, and display resolution before uploading. Ask me when temporary SSH or a parked restart is needed.

[Quick Start](Quick%20Start.md) · [Uninstall](AgentReadMe.md#uninstall) · [Change notes](CHANGELOG.md) · [Native source](source/)

## Features

- **BoostDeck** has faster gauge updates and a new bar layout. It displays boost, AFR, ignition timing, intake air temperature, transmission temperature, rail pressure, oil pressure, coolant temperature, and Active Ethanol. Press **OPTIONS** for gauge colors, including Multicolor, and to switch between PSI and Fahrenheit or bar and Celsius.
- **ShiftDeck** displays live speed, an RPM bar, and shift lights with a selectable RPM threshold. Press **OPTIONS** to switch between mph and km/h. This update includes performance improvements.
- **MHD+ Suite** offers four map slots and puts **Exhaust Flap** and **Max Cooling** under **Options**.
- **Bootmod3 Suite** is a new beta control menu for compatible CustomROM tunes. It offers four map slots, exhaust flap control, Active Sound, and burble presets.
- **xDrive Control (experimental)** temporarily selects rear-wheel drive on supported DSC versions. **The engine must be off before switching from AWD to RWD or from RWD to AWD.** On F-series DSC_25 (0F1860), switch to RWD with the engine running in Park instead; see the [compatibility notes](source/docs/COMPATIBILITY.md#xdrive-control-experimental).
- **Engine Fault Codes** reads engine/DME fault codes and shows descriptions when available. It can read codes even when the DME refuses to clear them.
- **Road Jet**. A simple game that makes a jet move based on throttle input and steering angle. [Road Jet package](packages/road-jet.zip)
- **BEAMer** offers Front, Rear, Both, and Insane lighting patterns in Park at zero speed. Tested on an F3x car. Other Fxx cars may work if they have the supported lighting module. Gxx has not been tested. [BEAMer package](packages/beamer.zip)
- **HUD Gauges** projects a live curved tachometer and numeric boost in PSI onto the factory windshield HUD. The tach spans 0–8,000 RPM, with a thicker redline zone starting at 6,700 RPM. Enable **My Vehicle → HUD Gauges → Show HUD gauges** to keep the display on after leaving the menu and restore it at the next boot. Uncheck it to clear the display. The overlay yields when it detects factory navigation guidance. [HUD Gauges package](packages/hud-gauges.zip)

## iDrive menus

### General menus

<table>
  <tr>
    <td><img src="images/01-boostdeck.png" alt="BoostDeck on iDrive" width="300"><br>BoostDeck</td>
    <td><img src="images/02-shiftdeck.png" alt="ShiftDeck on iDrive" width="300"><br>ShiftDeck</td>
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
