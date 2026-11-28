# iDriveDeck release builds

The v0.7.19 tag includes the desktop installer, native QNX source and EntryNav2 ports used for this release. The v0.7.18 Git tag was created with the public release documentation and the earlier native source tree. Use the v0.7.19 tree for current development.

| Component | Source |
| --- | --- |
| Standard NBT Evo menus, workers, and resource generators | [native](../native/) and [tools](../tools/) |
| Full-screen NBT Evo menus, renderer, and USB updater | [fullscreen](../fullscreen/) |
| EntryNav2 plugins, workers, and package installer | [entrynav](../entrynav/) |
| macOS and Windows desktop installer | [desktop-installer](../tools/desktop-installer/) |

## Build inputs

- NBT Evo uses a licensed QNX SDP 6.5 toolchain, ARM GCC 4.4.2 target selection, and authorized NBT Evo link inputs. See [native build instructions](BUILD.md) and [full-screen build instructions](../fullscreen/README.md). Generate full-screen companion manifests with `python3 source/fullscreen/native/generate-companions.py source/native`.
- EntryNav2 ports use ARM hard-float Linux builds against the relevant EntryNav2 sysroot. See the [EntryNav2 port notes](../entrynav/docs/ID6-PORTS.md). The three beta package profiles reuse their existing release names for framework selection. The desktop installer verifies the archive SHA-256 and the setup script checks each member before installation.
- The release supplies an [installer payload archive](https://github.com/inotbarnacle/iDriveDeck/releases/tag/installer-v0.7.19) containing the staged app payloads. It contains the project packages and installer profile list. It omits proprietary firmware and framework libraries.

## Build the desktop installer from the release payloads

From the repository root on macOS, copy the archive's `payload/` and `installer-profiles.json` next to `source/tools/desktop-installer/main.go`. Copy `source/native/fileops.c` into `source/tools/desktop-installer/native/` for the filesystem fixture. Then run:

```sh
installer="$PWD/source/tools/desktop-installer"
sh "$installer/build-release.sh" "$installer/template" "$PWD/dist"
```

To regenerate staging from standalone QNX packages, a staged full-screen bundle, and EntryNav2 ARM tar archives, run `python3 "$installer/stage-payload.py" --help`. Archived releases use the same project and in-car app names to preserve upgrade and backup behavior. Host toolchains, regenerated graphics and code signing can affect binary and archive bytes.
