# NBT Evo compatibility approach

NBT-AAD installs a native daemon and a small RHMI control menu. Most of the
video presentation happens in the daemon's own QNX Screen window through
MM-Renderer. That design relies less on a particular HMI executable layout.
The head unit still needs working app registration for its menu, a usable
projection network, and suitable display and media services. [NBT-AAD install guide](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/blob/main/install.md) · [NBT-AAD video manager](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/blob/main/milestone-7/src/video_manager.cpp)

CarPlay Advanced defaults to HUD-only mode on ID6. It hooks route guidance
and can choose MOST or Ethernet KOMBI based on the available cluster path.
Its full HMI patch mode is stricter. It uses exact software hashes or scans
for the required sites and checks the words it will modify. A broadly
installable HUD-only build says little about whether fixed-address changes
are safe on the same firmware. [CarPlay Advanced supported software](https://github.com/st1ng/nbtevo-carplay-advanced#supported-software)

Each full-screen app uses its own RHMI plugin and a shared QNX Screen renderer.
Check framework compatibility, the display layout, telemetry and projection separately.

- **Framework compatibility.** Examine the two captured BMW ARM frameworks
  for required dynamic symbols. Report their hashes for attribution. A
  matching symbol name alone cannot establish calling conventions, class
  layout, or safe initialization. Verify loading and registration on a
  parked target before treating a new firmware as supported.
- **Display layout.** Stage the layout for the measured panel. Existing 8:3
  panels use one logical layout. The narrower 800 × 480 and 960 × 480
  panels use compact layouts. [Layout measurements](https://github.com/AndreasBoehm/idrive6-layout-preview)
- **Telemetry.** Resolve the diagnostic gateway independently of firmware.
  The integrated read-only workers use the existing HSFZ and supported
  MOST fallback policies. Individual ECU signals can still be absent.
- **Projection.** Check the actual Wi-Fi interface, Bonjour path and video
  decoder separately. Optional projection support should not affect the
  in-car gauge menus.

## Checks completed

Upstream reported that its baseline plugins were built against the N18032I
framework pair and registered in an isolated ARM QNX emulator with a
1280 × 480 IDEmu profile. These results precede the WarmDeck readiness,
ShiftDeck profiles, 24-byte drive frame and surface-v4 changes. The current contribution has host tests only; a matching
licensed QNX build, ARM emulator and parked runtime checks remain outstanding.
No additional firmware or framework pair is approved by these source changes.
