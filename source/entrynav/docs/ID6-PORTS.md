# Additional EntryNav2 apps

The ports use EntryNav2's ARMv7 hard-float HMI SDK, native RHMI layouts,
individual My Vehicle entries and Linux worker launcher. The QNX renderer
and QNX MOST client are platform-specific. Linux workers use HSFZ through
the diagnostic gateway, sharing the existing diagnostic lease. The native
menus and the desktop installer use the gateway address saved by EntryNav-Apps.

The additional menus are WarmDeck, LaunchDeck, HybridDeck, Road Jet, BEAMer,
xDrive Control and MG Flasher. BoostDeck, ShiftDeck, Engine Fault Codes,
MHD+ Suite and Bootmod3 Suite use the upstream EntryNav2 code. ShiftDeck has
21 selectable targets from 6,000 to 8,000 RPM and stores the choice alongside
its speed-unit preference. Road Jet and BEAMer require a valid speed of 8 km/h
(about 5 mph) or less. BEAMer still checks lighting-module compatibility.
xDrive retains its Park, capability and readback checks.

Use a private EntryNav2 sysroot. This work uses the pinned GNU ARM GCC 4.7
toolchain and the standard EntryNav2 release packager. Set
`ENTRYNAV_PROFILE_DIR` to the firmware-specific metadata directory when
building an additional release. It contains `frameworks.md5` and
`firmware.txt`. The desktop installer selects an additional bundle only
if both SHA-256 framework fingerprints match its local profile metadata.

The three test releases use the stock runtime files from BMW SWFL
`00003bb8` and HMI files from `00003bbf`.

| Firmware | Build stamp | System SWFL revision | HMI SWFL revision | Release |
| --- | --- | --- | --- | --- |
| W2408C1R | `202402191339` | `002_176_001` | `002_176_001` | `0.1.3-test.1` |
| Q2051B1R | `202012141616` | `002_156_007` | `002_156_007` | `0.1.3-test.2` |
| C1643D1R | `201610270005` | `001_025_014` | `001_025_009` | `0.1.3-test.3` |

See `install/profiles` for the firmware-specific build metadata. The 12
menus are separate native RHMI entries for the 1280 × 480 layout. The
full-display NBT Evo overlay uses QNX and is separate from these EntryNav2
menus.

## Verification

- `make port-syntax port-workers` checks the local native source and worker build.
- `make arm` builds the ARM plugins, workers and test programs against a sysroot.
- `python3 tests/run_arm.py` in the QEMU environment checks the framework pair,
  loads all menu plugins and compares their virtual override placements.
- `tools/package_release.py` packages the apps individually using the
  EntryNav-Apps release envelope. It does not publish anything.

Vehicle features depend on the installed DME, EGS, modules and tune.
QEMU worker and plugin tests cover the software lifecycle and simulated
diagnostics. They do not verify the physical display compositor, the actual
vehicle's response or a change to an ECU.
