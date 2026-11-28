# SpeedlockUnlock (Optional Standalone Plugin)

`SpeedlockUnlock` is an optional, standalone, hidden NBT EVO ID5/ID6 plugin that disables the RHMI/Plugin in-motion speedlock (`CRhmiSpeedlockServer`) once at boot so custom ID6 apps stay accessible while moving in regions where BMW restricts them.

> **Opt-in & legal notice:** Disabling in-motion UI interlocks may be restricted on public roads depending on your jurisdiction. This plugin is strictly opt-in, is not installed by default, and is the vehicle owner's responsibility to use safely and lawfully.

## Why CAFD coding and `reg_hu.efs.ffs` edits aren't enough in some markets

- **CAFD coding (`SPEEDLOCK_*`):** `CHmiSpeedlockService` maintains two independent speed-threshold pairs. CAFD coding updates the regular HMI thresholds, whereas RHMI/Plugin apps use a separate threshold pair initialized to `0 km/h` and populated from the HMI registry instead of CAFD.
- **Patching `reg_hu.efs.ffs` (hive `254`, keys `0x8210` / `0x8211`):** Works initially, but BMW ConnectedDrive periodically runs `GetPortalAccessData` over telematics and re-provisions regional defaults (for example, Australia `S810` resets `0x8210 = "03"` and `0x8211 = "0"`), re-enabling the lock.

## How `SpeedlockUnlock` works

1. **Auto-starts inside `HmiMain3D`:** `SpeedlockUnlock.manifest` (`autoStart=yes`, connector ID `119`) loads `libSpeedlockUnlock.so` in-process at boot and calls `SpeedlockUnlockApp::onInit()`.
2. **Non-blocking, boot-only execution:** `onInit()` posts a `bmw::Runnable` with a 1-second delay (retrying every 2 seconds up to 30 times). Once it succeeds, the runnable deletes itself (`delete this`) — leaving **no background thread, no timer, and 0% CPU usage** for the rest of the boot session.
3. **Dynamic lookup (no hardcoded `HmiMain3D` addresses):** Resolves `hmiplugin::getInstanceCacheByIdentifier` via `dlsym(RTLD_DEFAULT, ...)`, asks for `"IRhmiSpeedlockService"`, and walks `CPluginInstanceCache`'s bucket map to read the live `IRhmiSpeedlockService*` already resolved by `HmiMain3D`'s main thread.
4. **Flips the master override flag:** Calls `IRhmiSpeedlockService::vtable[4](true)` (`setSpeedlockDisabled(true)`), which sets `CRhmiSpeedlockServer::m_bSpeedlockDisabled = true` and calls `evaluateTriggerConditions()` to unlock all registered apps and menu entries immediately.
5. **Survives ConnectedDrive refreshes:** Both `getSpeedlockEffects()` and `evaluateSpeedlockState()` return `0` immediately when `m_bSpeedlockDisabled` is set, before consulting speed, gear, or registry keys `0x8210`/`0x8211`. ConnectedDrive `GetPortalAccessData` never clears this flag.
6. **Hidden from the iDrive menu:** `SpeedlockUnlock.xml` sets `<property id="3" value="false" />` (`VISIBLE=false`) on its entry button so no icon appears in iDrive.
7. **Zero flash wear & untouched OEM files:** Does not modify `HmiMain3D` or `reg_hu.efs.ffs`, and writes its one-line startup log to RAM at `/tmp/speedlock_unlock.log`.

## Compatibility & ABI assumptions

Verified on hardware and road-tested on **NBTevo `ID5_2211`** (F20 M140i, Australian market `S810`, QNX 6.5 / GCC 4.4.2).

While no `HmiMain3D` code addresses are hardcoded, three ABI assumptions were verified on `ID5_2211` and should be checked if porting to significantly different builds:

- `CPluginInstanceCache` `std::unordered_map` bucket array at `+0x18` and bucket count at `+0x1c` (GCC 4.4 `libstdc++` ABI in `libhmiplugin.so`).
- `CPluginInstanceCacheTLS::m_instance` at `+0x08`.
- `IRhmiSpeedlockService::vtable[4]` (`+0x10`) = `setSpeedlockDisabled(bool)`.

## Build & Desktop Installer

- **Native build:** `source/native/build.sh` builds `out/libSpeedlockUnlock.so` alongside the other plugins using the QNX SDP 6.5 ARM toolchain.
- **Desktop installer:** `SpeedlockUnlock` is wired into `source/tools/desktop-installer/` as an unchecked opt-in option under both standard NBT Evo and full-screen NBT Evo install/uninstall, staged from `packages/speedlock-unlock.zip` by `stage-payload.py`.
- **Manual install:** Place these three files in `/var/opt/hmi/data/ID5/Plugins/SpeedlockUnlock/`:

- `SpeedlockUnlock.manifest` (`0644`)
- `SpeedlockUnlock.xml` (`0644`)
- `libSpeedlockUnlock.so` (`0755`)

Verify that connector ID `119` does not collide with any other plugin in `/var/opt/hmi/data/ID5/Plugins/`, then restart iDrive while parked.

## Verify & Uninstall

- **Verify:** After restart, `/tmp/speedlock_unlock.log` should contain:
  ```text
  pid=<pid> t=<ts> attempt=0 ptr=<app> SpeedlockUnlock::onInit
  pid=<pid> t=<ts> attempt=1 ptr=<svc> disabled RHMI speedlock via CPluginInstanceCache IRhmiSpeedlockService::vtable[4](true)
  ```
- **Uninstall:** Remove `/var/opt/hmi/data/ID5/Plugins/SpeedlockUnlock` and restart iDrive.
