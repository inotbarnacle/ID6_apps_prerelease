<div align="center">
  <h1>EntryNav-Apps</h1>
  <p>Engine gauges, a shift light, bootmod3 and MHD controls, and fault codes for BMW iDrive 6.</p>
  <p>
    <a href="https://github.com/CallMeLewis/EntryNav-Apps/releases"><img src="https://img.shields.io/github/v/release/CallMeLewis/EntryNav-Apps?display_name=tag&amp;sort=semver" alt="Latest release"></a>
    <a href="https://github.com/CallMeLewis/EntryNav-Apps/actions/workflows/ci.yml"><img src="https://github.com/CallMeLewis/EntryNav-Apps/actions/workflows/ci.yml/badge.svg" alt="CI status"></a>
    <a href="LICENSE"><img src="https://img.shields.io/badge/licence-MIT-blue" alt="Licence: MIT"></a>
  </p>
</div>

EntryNav-Apps adds five apps to BMWs with the **EntryNav2** head unit, the iDrive 6 unit with a 1280×480 screen and
CarPlay over Wi-Fi. They appear under *My Vehicle* in iDrive and read the engine through the car's diagnostic gateway.
Each app is its own plugin: install any of them, and remove one without touching the others.

| App | What it shows |
|---|---|
| [BoostDeck](#boostdeck) | Boost, air-fuel ratio, ignition timing, intake air, gearbox, rail pressure, oil pressure and coolant |
| [ShiftDeck](#shiftdeck) | A full-width rev bar with shift lights, and road speed |
| [Bootmod3 Suite](#bootmod3-suite) | bootmod3's four map slots, Active Sound and burbles, on a car running a bootmod3 CustomROM |
| [MHD+ Suite](#mhd-suite) | MHD's four maps and Max Cooling, on a car running an MHD+ multi-map tune |
| [Engine Fault Codes](#engine-fault-codes) | The engine computer's fault codes, with a description of each, and a way to clear them |

They're ported from [inotbarnacle's apps](https://github.com/inotbarnacle/ID6_apps_prerelease) for the NBT Evo. This
port runs on the EntryNav2 only: on an NBT Evo, use the originals.

> [!WARNING]
> EntryNav-Apps is experimental, and installing it changes the head unit's system. Install it with the car parked,
> entirely at your own risk. Bootmod3 Suite writes to the engine computer's memory, and MHD+ Suite sends commands to
> MHD's code in it. The authors accept no responsibility for any damage to your car or loss of its warranty.

## Installing

The easy way is **[Flare Drive](https://github.com/CallMeLewis/flare-drive-app)** 2.2.0 or later, the companion Android app. Each app has its own page under *Plugins*,
where Flare Drive downloads a signed release, checks that your car can run it, installs it and finds the diagnostic
gateway. Updates and removal happen there too. The [CHANGELOG](CHANGELOG.md) lists what changed in each release, and
[Releases](https://github.com/CallMeLewis/EntryNav-Apps/releases) has every archive with its SHA-256.

<details>
<summary><b>Installing by hand</b></summary>

You need an ENET cable, a PC with an SSH client, the head unit's root password and the app's archive. EntryNav-Apps
doesn't come with the password. Do it parked, with the ignition on. For BoostDeck:

1. Copy the archive to the head unit's `/tmp` and unpack it there: `tar -xf boostdeck-VERSION-armhf.tar -C /tmp`.
2. Run `sh /tmp/boostdeck-VERSION/setup.sh check`. It checks the car and writes nothing.
3. Run `sh /tmp/boostdeck-VERSION/setup.sh install`. Besides the app, it installs a firewall rule that lets it reach
   the diagnostic gateway, then finds the gateway and saves its address for every app.
4. Restart iDrive: hold the volume knob for about 30 seconds. The app appears under *My Vehicle*.

| App | Archive | Installed in |
|---|---|---|
| BoostDeck | `boostdeck-VERSION-armhf.tar` | `/opt/hmi/data/ro/Plugins/BoostDeck` |
| ShiftDeck | `shiftdeck-VERSION-armhf.tar` | `/opt/hmi/data/ro/Plugins/ShiftDeck` |
| Bootmod3 Suite | `bm3-maps-VERSION-armhf.tar` | `/opt/hmi/data/ro/Plugins/Bm3Maps` |
| MHD+ Suite | `mhd-maps-VERSION-armhf.tar` | `/opt/hmi/data/ro/Plugins/MhdMaps` |
| Engine Fault Codes | `engine-codes-VERSION-armhf.tar` | `/opt/hmi/data/ro/Plugins/EngineCodes` |

Once installed, each app's `setup.sh` sits in its folder, for example `/opt/hmi/data/ro/Plugins/BoostDeck/setup.sh`:

| Command | |
|---|---|
| `status` | Whether it's installed, whether its files are intact, and the gateway in use |
| `gateway [IP]` | Finds the gateway again, or checks and saves the one given. Every app uses the address |
| `tidy` | Deletes an earlier version iDrive was still using, after a restart |
| `uninstall [--purge]` | Removes the app. `--purge` also deletes its settings, and the gateway's address once no other app is left |

`check` stops unless iDrive's app framework is the exact version the apps were checked against
(`install/frameworks.md5`). It also stops when anything in the way wasn't installed by EntryNav-Apps, or when the
system partition is already writable.

</details>

## The apps

### BoostDeck

Eight engine readings in two iDrive-style lists, each with a bar along its row, updated about every 35 ms while the
page is open.

| Under OPTIONS | |
|---|---|
| *Gauge color* | The bars' colour: one of eight, or Multicolor for a different colour per reading |
| *Temperature* | Celsius or Fahrenheit |

The BMW-specific readings come from a 2016 340i with the B58 engine. Other engines may answer fewer of them or scale
them differently, so compare each reading with a logger you trust before relying on it. The
[signal table](docs/SIGNALS.md) lists every request, and [porting to another car](docs/PORTING.md) says how to check
a new one.

### ShiftDeck

A rev bar from 0 to 8,000 rpm with a row of shift lights, and road speed. It reads engine speed about every 20 ms, and
the bar eases between readings.

| Under OPTIONS | |
|---|---|
| *Shift light setting* | The shift point, from 6,000 to 8,000 rpm in 100 rpm increments |
| *Speed* | MPH or km/h |

It uses the standard engine speed and road speed that nearly every car reports.

### Bootmod3 Suite

Switches between bootmod3's four map slots and marks the active one. Picking a tile writes that slot and reads it
back. Called BM3 Maps until 0.1.1.

| Under OPTIONS | |
|---|---|
| *Active Sound* | Off or On. Shows whether the engine computer accepted it |
| *Burbles* | Off, or aggression 4, 8 or 12. Written to the ROM's burble table and switch, and read back |

Map switching isn't a BMW function: bootmod3's code in the engine computer does it. So before anything else, the app
reads bootmod3's CustomROM marker from the engine computer's memory, and goes on only if it names one of the 150 ROMs
in [`src/bm3_profiles.h`](src/bm3_profiles.h). A stock engine computer, an MHD tune or an unlisted ROM gets a message,
and nothing is written. For MHD, use [MHD+ Suite](#mhd-suite).

- Pick only slots that hold a map for the fuel in the tank. The app can't tell what each slot contains.
- There's no exhaust flap setting: bootmod3 sets the flap in each map.
- Some engine computers refuse bootmod3's memory reads with "security access denied". Then the app can't work on that
  car.
- Where the marker sits in memory depends on the engine computer. The app tries each address bootmod3's own app does,
  and `/var/local/data/EntryNavApps/bm3-marker-address` (eight hex digits) sets one by hand.

### MHD+ Suite

Switches between MHD's four maps. Picking a tile asks the engine computer for that map, and the app counts the request
only when the engine computer echoes it back.

| Under OPTIONS | |
|---|---|
| *Max Cooling* | Off or On. On needs the engine running |

Map switching isn't a BMW function: MHD's code in the engine computer does it, so it needs an MHD+ multi-map tune. An
engine computer without one refuses the request, and the app says so.

- MHD has no known way to read the active map back. So no tile is marked, and the app says the engine computer accepted
  the map, not that the map is active.
- Pick only maps made for the fuel in the tank. The app can't tell what each map contains.
- The check engine light may flash after a switch. That's MHD showing the map change.
- There's no exhaust flap setting.
- The commands come from cauliflowermossgrape's Live Adjustments, which checked them on one MG1 engine computer. Other
  engine computers are untried.

### Engine Fault Codes

*Read engine codes* reads the engine computer's fault memory and shows four codes a page, each as Active, Pending or
Stored, with a description when one is known.

| Under OPTIONS | |
|---|---|
| *Next page* | The next four codes |
| *Clear engine codes...* | Asks first, then clears the fault memory and reads it again. Use it with the ignition on and the engine off |

The descriptions in [`src/dme_descriptions.h`](src/dme_descriptions.h) were collected on a 340i, so other engines show
some codes without one.

## How it works

Each app is a plugin that iDrive loads at start-up, with a worker program beside it that does the diagnostics, so
iDrive's own process never waits on the car. The workers reach the engine computer through the car's diagnostic
gateway on TCP 6801. They find the gateway themselves, on the link-local network or on the one that tethering on the
ENET cable hands out, and find it again when it moves.

- The apps live in `/opt/hmi/data/ro/Plugins` on the system partition, each with its own firewall rule in
  `/etc/firewall`, because the head unit's firewall drops outgoing connections.
- Their settings and the gateway's address live in `/var/local/data/EntryNavApps`, on the data partition.
- An app's folder only changes by a whole-folder rename, so a power cut leaves the previous version or the new one.

<details>
<summary><b>What the EntryNav2 needs that the NBT Evo didn't</b></summary>

| | NBT Evo (upstream) | EntryNav2 (this port) |
|---|---|---|
| System | QNX | ARMv7 Linux, glibc 2.15 |
| App folder | `/var/opt/hmi/data/ID5/Plugins` | `/opt/hmi/data/ro/Plugins` on the system partition. `/var/local` doesn't allow programs to run |
| Settings | beside the apps | `/var/local/data/EntryNavApps` |
| `bmw::PluginApp` | 352 bytes | larger (a Linux semaphore), so the apps reserve 0x200 bytes |
| Gateway access | open, at a fixed address | the head unit's firewall drops outgoing connections, so port 6801 needs a rule, and the apps search for the gateway |
| Installer | files copied over SSH | one archive per app with its own `setup.sh`, or Flare Drive |
| Tuning controls | MHD+ Suite or Bootmod3 Suite | both, each its own plugin |

</details>

## Building it yourself

You need Docker, and the same images as AA-EntryNav: `entrynav2-tc:4.7` to compile and `aa-entrynav-qemu:local` to run
the checks. Both use a copy of the car's system in a Docker volume, `entrynav2-rootfs` by default (`HU_ROOTFS` names
another). That copy holds BMW files and never goes into git.

| Command | What it does |
|---|---|
| `make test` | The PC tests: the decoders, and each worker against a simulated gateway (Linux or WSL) |
| `make assets` | The screens and their image archives (Python with Pillow) |
| `make check` | The PC tests, the apps' C++ compiled for errors, ShellCheck, and the committed screens matching their generators. CI runs each as its own job on every push |
| `bash toolchain/tc.sh make arm` | The ARM apps, workers and test programs |
| `bash toolchain/qemu.sh python3 tests/run_arm.py` | Loads each app the way iDrive does, with the car's own libraries under QEMU |
| `python3 tools/package_release.py` | One release archive per app, in `dist/` |
| `python3 tests/run_setup.py` | `setup.sh` on a pretend head unit built from the copy of the car's system |
| `python3 tools/check.py` | Every check above, in the order a release needs them |

CI can't hold the copy of the car's system, so the ARM build and its checks run only in `tools/check.py`, which
`tools/publish_release.py` runs before every release.

### Publishing a release

Every app shares one version, `VERSION`, and every release ships all of them: a change to one app raises `VERSION`, and
the others ship again unchanged. `tools/publish_release.py publish` runs `tools/check.py`, then sends each app's archive
to Flare Drive's plugin catalog with `flare-publish`, and `plugins/<id>.toml` is each app's manifest. It then tags
`vX.Y.Z` and puts the archives on its GitHub Release, with the version's CHANGELOG section as its text. The publishing
Worker checks and stores the archive and signs the catalog, and the list older Flare Drive versions read
(`https://updates.darkzone.dev/<id>/`) is rebuilt from it. It needs Flare Drive's repo cloned beside this one (or `FLARE_DRIVE_REPO`), the publishing service
token and the release signing key, and refuses a dirty or unpushed tree and a version
whose CHANGELOG section lacks an app's Flare Drive note. `stability` changes a release in the catalog; `retire` is
refused. Flare Drive's `docs/publishing.md` describes the whole process.

A retry checks the catalog, publishes only missing apps, and downloads the existing archives with size and SHA-256
checks to complete the GitHub Release. This also works when all apps are already published. MHD+ Suite uses the
catalog alone; older Flare Drive versions have no per-plugin list for it.

## Credits and licence

EntryNav-Apps is a port of [ID6_apps_prerelease](https://github.com/inotbarnacle/ID6_apps_prerelease) by inotbarnacle.
Bootmod3 Suite's protocol and ROM profiles come from [cauliflowermossgrape](https://github.com/cauliflowermossgrape)'s
port of BM3 Live by ezl0l, under the MIT licence in [third-party](third-party/cauliflowermossgrape-LICENSE).
`tools/make_bm3_profiles.py` regenerates the ROM table from that port's `bm3_profiles.js`. MHD+ Suite's commands come
from the same author's Live Adjustments app, from a1-mapping's captures.

EntryNav-Apps is under the [MIT licence](LICENSE), like the project it comes from. It comes with no warranty.

EntryNav-Apps is an independent project. It is not made or endorsed by BMW, by bootmod3's makers or by MHD's. BMW and
iDrive are trademarks of BMW AG.
