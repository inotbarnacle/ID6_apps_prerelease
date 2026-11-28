#!/usr/bin/env python3
"""Build one dist/<id>-<version>-armhf.tar archive per plugin for Flare Drive and setup.sh.

Build the ARM programs and the screens first (bash toolchain/tc.sh make arm, make assets). Each archive holds one
folder, <id>-<version>/, laid out like the head unit:

    setup.sh VERSION plugin.json MD5SUMS frameworks.md5
    opt/hmi/data/ro/Plugins/<App>/        the app, its screens, images and worker, ena-gateway, and setup.sh,
                                          VERSION, plugin.json
    etc/firewall/entrynav-<id>.rules

setup.sh and the rule file are install/setup.sh and install/firewall.rules with the plugin's details filled in. MD5SUMS
covers every other file (setup.sh check verifies it on the car, which has no sha256sum). The archives are
reproducible: fixed owner, modes and dates, sorted entries, USTAR as Flare Drive's contract asks.

    python3 tools/package_release.py [OUT] [ID ...]
"""

import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARM = ROOT / "build-arm"
PLUGIN = ROOT / "plugin"
PROFILE = Path(os.environ.get("ENTRYNAV_PROFILE_DIR", ROOT / "install"))
VERSION = re.compile(r"^\d+\.\d+\.\d+(-(beta|test)\.\d+)?$")
ELF_ARM = b"\x7fELF\x01\x01\x01"


class Plugin:
    """former_name: the name before a rename. setup.sh still recognises the firewall rule an install under that name
    left, by its first line. The id, folder and worker keep their old names, so installs and feeds carry on."""

    def __init__(self, id, app, name, worker, settings="", former_name=""):
        self.id, self.app, self.name, self.worker, self.settings = id, app, name, worker, settings
        self.former_name = former_name


PLUGINS = [
    Plugin("boostdeck", "BoostDeck", "BoostDeck", "boostdeck-collector", "boostdeck-temperature"),
    Plugin("shiftdeck", "ShiftDeck", "ShiftDeck", "shift-deck", "shiftdeck-speed shift-target"),
    Plugin("bm3-maps", "Bm3Maps", "Bootmod3 Suite", "bm3-maps", "bm3-marker-address", former_name="BM3 Maps"),
    Plugin("mhd-maps", "MhdMaps", "MHD+ Suite", "mhd-maps"),
    Plugin("engine-codes", "EngineCodes", "Engine Fault Codes", "engine-codes"),
    Plugin("warmdeck", "WarmDeck", "WarmDeck", "warmdeck-collector", ""),
    Plugin("launchdeck", "LaunchDeck", "LaunchDeck", "launch-drive-worker", "LaunchDeck.records LaunchDeck.60-130 LaunchDeck.0-100 LaunchDeck.100-200 LaunchDeck.prefs"),
    Plugin("hybriddeck", "HybridDeck", "HybridDeck", "hybriddeck-collector", ""),
    Plugin("road-jet", "RoadJet", "Road Jet", "road-jet", ""),
    Plugin("beamer", "BEAMer", "BEAMer", "beamer", ""),
    Plugin("xdrive-control", "XDriveControl", "xDrive Control", "xdrive-control", ""),
    Plugin("mgflasher", "MgFlasher", "MG Flasher", "mgflasher", "mgflasher-live-profile.txt"),
]


def fail(message):
    raise SystemExit(f"package_release: {message}")


def read(path: Path) -> bytes:
    if not path.is_file():
        fail(f"{path} is missing: build it first")
    return path.read_bytes()


def arm(path: Path) -> bytes:
    data = read(path)
    if not data.startswith(ELF_ARM) or int.from_bytes(data[18:20], "little") != 40:  # EM_ARM
        fail(f"{path.relative_to(ROOT)} is not a 32-bit ARM ELF")
    return data


def unix_text(path: Path) -> bytes:
    data = read(path)
    if b"\r\n" in data:
        fail(f"{path.relative_to(ROOT)} has Windows line endings")
    return data


def filled(path: Path, plugin: Plugin) -> bytes:
    """A template with the plugin's details in place of @ID@, @APP@, @NAME@, @FORMER_NAME@, @WORKER@ and
    @SETTINGS@."""
    text = unix_text(path).decode()
    for key, value in (("ID", plugin.id), ("APP", plugin.app), ("NAME", plugin.name),
                       ("FORMER_NAME", plugin.former_name), ("WORKER", plugin.worker), ("SETTINGS", plugin.settings)):
        text = text.replace(f"@{key}@", value)
    left = re.search(r"@[A-Z_]+@", text)
    if left:
        fail(f"{path.relative_to(ROOT)}: {left.group(0)} has no value")
    return text.encode()


def release_files(plugin: Plugin, version):
    """archive path inside the top folder -> (bytes, mode)."""
    files = {}
    description = {
        "schema": 1,
        "id": plugin.id,
        "name": plugin.name,
        "version": version,
        "firmware": unix_text(PROFILE / "firmware.txt").decode().split(),
        "installed_in": f"/opt/hmi/data/ro/Plugins/{plugin.app}",
    }
    meta = {
        "setup.sh": (filled(ROOT / "install/setup.sh", plugin), 0o755),
        "VERSION": (f"{version}\n".encode(), 0o644),
        "plugin.json": ((json.dumps(description, indent=2) + "\n").encode(), 0o644),
    }
    files.update(meta)
    files["frameworks.md5"] = (unix_text(PROFILE / "frameworks.md5"), 0o644)
    base = f"opt/hmi/data/ro/Plugins/{plugin.app}"
    for name, entry in meta.items():
        files[f"{base}/{name}"] = entry
    files[f"{base}/lib{plugin.app}.so"] = (arm(ARM / f"lib{plugin.app}.so"), 0o644)
    files[f"{base}/{plugin.worker}"] = (arm(ARM / plugin.worker), 0o755)
    files[f"{base}/ena-gateway"] = (arm(ARM / "ena-gateway"), 0o755)
    manifest = unix_text(PLUGIN / f"{plugin.app}.manifest")
    files[f"{base}/{plugin.app}.manifest"] = (manifest, 0o644)
    files[f"{base}/{plugin.app}.xml"] = (unix_text(PLUGIN / f"{plugin.app}.xml"), 0o644)
    images = PLUGIN / f"{plugin.app}_ALL_Images.zip"
    if images.name.encode() in manifest:
        files[f"{base}/{images.name}"] = (read(images), 0o644)
    files[f"etc/firewall/entrynav-{plugin.id}.rules"] = (filled(ROOT / "install/firewall.rules", plugin), 0o644)
    sums = "".join(f"{hashlib.md5(data).hexdigest()}  {name}\n" for name, (data, _) in sorted(files.items()))
    files["MD5SUMS"] = (sums.encode(), 0o644)
    return files


def source_epoch():
    try:
        return int(subprocess.run(["git", "log", "-1", "--format=%ct"], cwd=ROOT, capture_output=True, text=True,
                                  check=True).stdout.strip())
    except (OSError, subprocess.CalledProcessError, ValueError):
        return 0


def build(plugin: Plugin, version, out: Path, epoch) -> Path:
    files = release_files(plugin, version)
    top = f"{plugin.id}-{version}"
    folders = set()
    for name in files:
        parts = name.split("/")[:-1]
        folders.update("/".join(parts[:i]) for i in range(1, len(parts) + 1))
    payload = io.BytesIO()
    with tarfile.open(fileobj=payload, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for name in [""] + sorted(folders):
            item = tarfile.TarInfo(f"{top}/{name}".rstrip("/"))
            item.type, item.mode, item.mtime = tarfile.DIRTYPE, 0o755, epoch
            tar.addfile(item)
        for name, (data, mode) in sorted(files.items()):
            item = tarfile.TarInfo(f"{top}/{name}")
            item.size, item.mode, item.mtime = len(data), mode, epoch
            tar.addfile(item, io.BytesIO(data))
    out.mkdir(parents=True, exist_ok=True)
    archive = out / f"{top}-armhf.tar"
    archive.write_bytes(payload.getvalue())
    return archive


def main(argv):
    out = Path(argv[1]) if len(argv) > 1 else ROOT / "dist"
    wanted = set(argv[2:])
    unknown = wanted - {p.id for p in PLUGINS}
    if unknown:
        fail(f"no plugin called {', '.join(sorted(unknown))}")
    version = os.environ.get("ENTRYNAV_RELEASE_VERSION", (ROOT / "VERSION").read_text()).strip()
    if not VERSION.match(version):
        fail(f"VERSION {version!r} is not X.Y.Z, X.Y.Z-beta.N or X.Y.Z-test.N")
    epoch = source_epoch()
    for plugin in PLUGINS:
        if wanted and plugin.id not in wanted:
            continue
        archive = build(plugin, version, out, epoch)
        data = archive.read_bytes()
        print(f"{archive}  {len(data)} bytes  sha256 {hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main(sys.argv)
