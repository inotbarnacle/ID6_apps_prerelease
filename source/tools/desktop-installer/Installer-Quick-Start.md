# ID6Apps Desktop Installer v0.7.6

The macOS app supports Apple silicon and Intel Macs. On Windows, open the x64 folder for Intel or AMD PCs, or the ARM64 folder for Windows on ARM.

## Enable temporary SSH first

1. Park the car, wake iDrive, and connect the computer to the car with a wired ENET adapter.
2. Download the [Bavarian SSH Client](https://ssh.bavarian-aa.uk/) for your computer. Open it and choose **Enable SSH**. Leave the SSH IP field blank for automatic discovery unless you already know the head unit address.
3. Wait for the client to report that SSH is enabled. Keep iDrive awake and the network connected. Temporary SSH ends when the head unit reboots.

## Check compatibility and install

1. Unzip the download and open **ID6Apps Installer**. It opens a page in your browser on the local computer.
2. Choose the adapter. Leave the head unit IP blank for automatic discovery. Enter the IP in **Connection settings** if you know it or discovery fails. The installer uses the default SSH password.
3. Select the menus you want. Choose MHD+ Suite, Bootmod3 Suite, or no tuning suite. WarmDeck shows engine oil, coolant, and transmission warm-up progress. xDrive Control is experimental and depends on the DSC controller.
4. Choose **Check compatibility**. Verify the SSH fingerprint for your head unit when prompted. A successful check reports the exact matched firmware, display size, and diagnostic gateway. See the [compatibility matrix](https://github.com/inotbarnacle/ID6_apps_prerelease/blob/main/source/docs/COMPATIBILITY.md) for the evidence behind each profile.
5. Choose **Install selected menus**. Wait for verification and record the backup path. Restart iDrive while parked, then open the menus under **My Vehicle**.

Display layouts follow Andreas Boehm’s measured RHMI coordinate rules. HUD Gauges is
experimental and starts only when enabled for the current iDrive session. HUD Gauges
checks for native NBT-AAD and the CarPlay Advanced HUD hook and stays off if it detects
either. The capture-only J16383I and O18184I firmware profiles require further validation.
[Layout measurements](https://github.com/AndreasBoehm/idrive6-layout-preview)

## Uninstall

1. Before removing an app, set the vehicle controls to the state you want to keep. For example, turn off the HUD overlay while HUD Gauges is still installed. Re-enable temporary SSH if iDrive has since rebooted.
2. Open the installer, change **Operation** to **Uninstall**, and select only the menus you want to remove. Choose **Uninstall selected menus**.
3. Record the backup path shown by the installer. Restart iDrive while parked and check **My Vehicle**. Follow the [restore directions](https://github.com/inotbarnacle/ID6_apps_prerelease/blob/main/AgentReadMe.md#uninstall) to move the original folder back if needed.

## CarPlay Advanced HUD

Choose CarPlay Advanced HUD under Operation to preview or install
[st1ng's HUD-only add-on](https://github.com/st1ng/nbtevo-carplay-advanced).
It downloads and verifies the upstream release. Use wired ENET, internet access
and an existing working CarPlay setup. Restart iDrive while parked after
installation. The upstream installer saves backups on the head unit.
