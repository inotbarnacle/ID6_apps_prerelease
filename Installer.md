# iDriveDeck Desktop Installer v0.7.18

[Download the installer](https://github.com/inotbarnacle/iDriveDeck/releases/tag/installer-v0.7.18). The macOS app supports Apple silicon and Intel Macs. On Windows, open the x64 folder for Intel or AMD PCs, or the ARM64 folder for Windows on ARM.

## Enable temporary SSH on NBT Evo

1. Park the car, wake iDrive, and connect the computer to the car with a wired ENET adapter.
2. Download the [Bavarian SSH Client](https://ssh.bavarian-aa.uk/) for your computer. Open it and choose **Enable SSH**. Leave the SSH IP field blank for automatic discovery unless you already know the head unit address.
3. Wait for the client to report that SSH is enabled. Keep iDrive awake and the network connected. Temporary SSH ends when the head unit reboots.

## Check compatibility and install

1. Unzip the download and open **ID6Apps Installer**. It opens a page in your browser on the local computer.
2. For NBT Evo, choose the adapter. Leave the head unit IP blank for automatic discovery. Enter the IP in **Connection settings** if you know it or discovery fails. The installer uses the default SSH password.
3. Select the menus you want. Choose MHD+ Suite, Bootmod3 Suite, MG Flasher, or no tuning suite. WarmDeck shows engine oil, coolant, and transmission warm-up progress. xDrive Control is experimental and depends on the DSC controller.
4. Choose **Check compatibility**. Verify the SSH fingerprint for your head unit when prompted. For NBT Evo, the check reports the firmware profile status, display size, and diagnostic gateway. See the [compatibility matrix](https://github.com/inotbarnacle/iDriveDeck/blob/main/source/docs/COMPATIBILITY.md) for the evidence behind each profile.
5. Choose **Install selected menus**. Wait for verification and record the backup path. Restart iDrive while parked, then open the menus under **My Vehicle**. Select every app you want to keep because the installer replaces the previous managed menu set.

Display layouts follow Andreas Boehm’s measured RHMI coordinate rules.

DPFDeck is an experimental optional app in source builds that include a reviewed
DPF payload; it is not included in the installer release linked above. Its
unchecked selection requires a 1280 × 480, 1440 × 540 or 1920 × 720 display.
It monitors advertised standard DPF pressure/temperature and coolant, with an
optional regeneration status display hidden by default. Soot/ash remain
unavailable. See the [DPFDeck guide](source/docs/DPFDECK.md)
for build status, validation and rollback. It adds no approved firmware profile.

## Uninstall

1. Before removing an app, set the vehicle controls to the state you want to keep. Re-enable temporary SSH if iDrive has since rebooted.
2. Open the installer, change **Operation** to **Uninstall**, and select only the menus you want to remove. Choose **Uninstall selected menus**.
3. Record the backup path shown by the installer. Restart iDrive while parked and check **My Vehicle**. Follow the [restore directions](https://github.com/inotbarnacle/iDriveDeck/blob/main/AgentReadMe.md#uninstall) to move the original folder back if needed.

## Head unit choices

Select **NBT Evo** for the standard menus or choose **Install full-screen
apps** to install the independent dashboards together. The full-screen
installer supports 800 × 480, 960 × 480, 1280 × 480, 1440 × 540 and
1920 × 720. It detects the panel and installs the matching layout.
Unverified NBT Evo firmware is available as early beta. Installing backs up
the previous ID6Apps menu set and retires managed menus outside the new
selection. HUD Gauges is retired and excluded. The full-screen installer can
also install full-screen Bootmod3 Suite or MG Flasher instead of MHD+ Suite,
and can add full-screen xDrive Control or HybridDeck.
Full-screen ShiftDeck has a 6,000–8,000 RPM
target picker and a **Speed units** toggle under OPTIONS. The RPM number
updates in 10 RPM steps. Standalone NBT Evo
ShiftDeck has the same RPM range under OPTIONS. Turn the iDrive knob to move
the full-screen highlight and press it to activate the selected control.

On NBT Evo, the optional **DCT temperature** setting is in BoostDeck Settings
and in WarmDeck under OPTIONS. It switches the transmission-temperature source
for both apps and is off by default. The DCT source is pending vehicle validation.
If the transmission does not return a usable value, the temperature stays blank.
Use **OPTIONS → Temperature units** in WarmDeck to switch between Celsius and Fahrenheit.

Select **EntryNav2 (early beta)** for the native 1280 × 480 menus. Connect
over wired ENET and supply the head unit IP and existing SSH password. The
installer prefers an exact framework match and offers the reference beta
builds for other firmware. Each archive still checks platform, storage,
connector conflicts, and loader dependencies before installing. Previous
managed menus are backed up before replacement.

The W2408C1R, Q2051B1R and C1643D1R test builds passed ARM QEMU plugin-loader
checks. Confirm menu loading and vehicle functions while parked.

## Future updates on full-screen ID6

The full-screen bundle adds **My Vehicle → ID6 App Updates** for future signed USB app updates. Use the desktop installer to install v0.7.18.
