# ID6Apps Desktop Installer (beta)

Download the [installer release](https://github.com/inotbarnacle/ID6_apps_prerelease/releases/tag/installer-v0.7.6). The macOS app supports Apple silicon and Intel Macs. On Windows, open the x64 folder for Intel or AMD PCs, or the ARM64 folder for Windows on ARM.

## Enable temporary SSH first

1. Park the car, wake iDrive, and connect the computer to the car with a wired ENET adapter.
2. Download the [Bavarian SSH Client](https://ssh.bavarian-aa.uk/) for your computer. Open it and choose **Enable SSH**. Leave the SSH IP field blank for automatic discovery unless you already know the head unit address.
3. Wait for the client to report that SSH is enabled. Keep iDrive awake and the network connected. Temporary SSH ends when the head unit reboots.

## Check compatibility and install

1. Unzip the download and open **ID6Apps Installer**. It opens a page in your browser on the local computer.
2. Choose the adapter. Leave the head unit IP blank for automatic discovery. Enter the IP in **Connection settings** if you know it or discovery fails. The installer uses the default SSH password.
3. Select the menus you want. Choose MHD+ Suite, Bootmod3 Suite, MG Flasher, or no tuning suite. LaunchDeck and HUD Gauges are optional. HUD Gauges and xDrive Control are experimental and depend on the hardware in the car. Road Jet and BEAMer require a 1280 × 480 display.
4. Choose **Check compatibility**. Verify the SSH fingerprint for your head unit when prompted. If automatic detection cannot read the firmware or display size, enter the full NBTevo code or select the known resolution under **Connection settings**. A successful check reports the detected firmware, display size, and diagnostic gateway. Firmware variants qualify when both live framework files match the hashes of the same approved pair. See the [compatibility matrix](source/docs/COMPATIBILITY.md) for the evidence behind each profile.
5. Choose **Install selected menus**. Wait for verification and record the backup path. Restart iDrive while parked, then open the menus under **My Vehicle**.

The installer uses the same RHMI layout at 1280 × 480, 1440 × 540 and 1920 × 720. iDrive scales the shared 1440 × 540 logical canvas to the panel. The 800 × 480 and 960 × 480 panels have less logical content width, so the installer stages compact artwork for those panels. HUD menu sizing is separate from the windshield overlay. [Andreas Boehm’s layout measurements](https://github.com/AndreasBoehm/idrive6-layout-preview) · [Issue #113](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/113)

## Optional CarPlay Advanced HUD

1. Use wired ENET, a parked car with working factory CarPlay and internet access on the computer.
2. Choose **Operation → CarPlay Advanced HUD → Preview CarPlay installation**. Read the reported cluster type, boot-hook changes and backup plan.
3. Choose **Install CarPlay HUD**. Restart iDrive while parked to open **Media/Radio → CarPlay Advanced Settings**. Reconnect the iPhone to test directions.

The installer retrieves and verifies [@st1ng’s CarPlay Advanced v1.3.0](https://github.com/st1ng/nbtevo-carplay-advanced/releases/tag/v1.3.0) and invokes its HUD-only installation procedure. The boot script and existing installation are backed up on the head unit under `/var/opt/carplay-advanced-backups`. Use the [upstream uninstall instructions](https://github.com/st1ng/nbtevo-carplay-advanced#install) to remove its boot hook and files. Menu visibility requires factory app coding. CarPlay Advanced and NBT-AAD use shared cluster and HUD channels, so verify directions with one projection app active at a time.

**ID6 HUD Gauges are unavailable whenever native NBT-AAD (Android Auto) is installed**, including when its daemon is stopped. The overlay also stays off when it detects the CarPlay Advanced HUD hook.

## Uninstall

1. Before removing an app, set the vehicle controls to the state you want to keep. For example, turn off the HUD overlay while HUD Gauges is still installed. Re-enable temporary SSH if iDrive has since rebooted.
2. Open the installer, change **Operation** to **Uninstall**, and select only the menus you want to remove. Choose **Uninstall selected menus**.
3. Record the backup path shown by the installer. Restart iDrive while parked and check **My Vehicle**. Follow the [restore directions](AgentReadMe.md#uninstall) to move the original folder back if needed.
