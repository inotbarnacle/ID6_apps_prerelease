#!/usr/bin/env python3
"""Stage checked QNX packages and create compact RHMI artwork for the desktop installer."""
import argparse
import json
from fractions import Fraction
from hashlib import md5, sha256
from io import BytesIO
from pathlib import Path
import re
import shutil
import tarfile
import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image

GROUPS = {
    "dashboard": ("BoostDeck", "ShiftDeck", "EngineCodes", "MhdMaps"),
    "bootmod3": ("Bootmod3Suite",),
    "mgflasher": ("MgFlasher",),
    "hybriddeck": ("HybridDeck",),
    "xdrive": ("XDriveControl",),
    "beamer": ("BEAMer",),
    "road-jet": ("RoadJet",),
    "launchdeck": ("LaunchDeck",),
    "warmdeck": ("WarmDeck",),
    "speedlock-bypass": ("SpeedlockBypass",),
}
COMPACT = {"BoostDeck", "ShiftDeck", "EngineCodes", "MhdMaps", "Bootmod3Suite", "MgFlasher",
           "LaunchDeck", "WarmDeck", "HybridDeck", "XDriveControl"}
GEOMETRY = re.compile(rb'(<property id=")(9|10|20|21)(" value=")(-?\d+)(")')
COMPACT_PANELS = ((800, 480), (960, 480))


def content_width(width, height):
    # Measured by Andreas Boehm in idrive6-layout-preview and reported in ID6 issue #113.
    return 540 * width // height - 180


def compact_xml(data, factor, *, horizontal_only=False):
    def scale(match):
        if horizontal_only and match[2] in {b"10", b"21"}:
            return match[0]
        value = round(int(match[4]) * factor)
        if match[2] in {b"9", b"10"}:
            value = max(1, value)
        return match[1] + match[2] + match[3] + str(value).encode() + match[5]

    return GEOMETRY.sub(scale, data)


def check_bounds(data, right, app):
    root = ET.fromstring(data)
    for component in root.findall(".//hmiState/components/*"):
        values = {prop.get("id"): int(prop.get("value"))
                  for prop in component.findall("./properties/property")
                  if (prop.get("value") or "").lstrip("-").isdigit()}
        if "9" in values and values.get("20", 0) + values["9"] > right:
            raise ValueError(f"{app}: component {component.get('id')} exceeds {right} units")
        if "10" in values and values.get("21", 0) + values["10"] > 477:
            raise ValueError(f"{app}: component {component.get('id')} exceeds 477 units")


def compact_images(data, factor):
    output = BytesIO()
    with ZipFile(BytesIO(data)) as source, ZipFile(output, "w", ZIP_DEFLATED) as target:
        for name in source.namelist():
            raw = source.read(name)
            with Image.open(BytesIO(raw)) as image:
                size = tuple(max(1, round(length * factor)) for length in image.size)
                if size != image.size:
                    converted = BytesIO()
                    image.convert("RGBA").resize(size, Image.Resampling.LANCZOS).save(converted, format="PNG")
                    raw = converted.getvalue()
            target.writestr(ZipInfo(name), raw, compress_type=ZIP_DEFLATED)
    return output.getvalue()


def compact_entry_archive(data, width=800, height=480):
    compact_width = content_width(width, height)
    factor = Fraction(compact_width, content_width(1280, 480))
    output = BytesIO()
    with tarfile.open(fileobj=BytesIO(data)) as source:
        members = source.getmembers()
        files = {member.name: source.extractfile(member).read() for member in members if member.isfile()}
        checksum = next(name for name in files if name.endswith("/MD5SUMS"))
        root = checksum.split("/", 1)[0] + "/"
        entries = [line.split("  ", 1) for line in files[checksum].decode().splitlines()]
        for digest, path in entries:
            if md5(files[root + path]).hexdigest() != digest:
                raise ValueError(f"EntryNav2 member checksum mismatch: {root + path}")
        for name, member in files.items():
            if "/Plugins/" not in name:
                continue
            if name.endswith(".xml"):
                files[name] = compact_xml(member, factor)
                check_bounds(files[name], compact_width, name)
            elif name.endswith("_ALL_Images.zip"):
                files[name] = compact_images(member, factor)
        files[checksum] = "".join(f"{md5(files[root + path]).hexdigest()}  {path}\n"
                                 for _, path in entries).encode()
        with tarfile.open(fileobj=output, mode="w", format=tarfile.USTAR_FORMAT) as target:
            for member in members:
                if member.isfile():
                    contents = files[member.name]
                    member.size = len(contents)
                    target.addfile(member, BytesIO(contents))
                else:
                    target.addfile(member)
    return output.getvalue()


def stage(repo, qnx_fileops, installer, fullscreen=None, entrynav=None):
    payload = installer / "payload"
    layouts = payload / "layouts"
    if layouts.exists():
        shutil.rmtree(layouts)
    for group, apps in GROUPS.items():
        with ZipFile(repo / "packages" / f"{group}.zip") as archive:
            sums = {name: digest for digest, name in
                    (row.split("  ", 1) for row in archive.read("SHA256SUMS").decode().splitlines())}
            for app in apps:
                directory = payload / app
                if directory.exists():
                    shutil.rmtree(directory)
                directory.mkdir(parents=True)
                for name in archive.namelist():
                    if not name.startswith(app + "/") or len(Path(name).parts) != 2 or name.endswith("/"):
                        continue
                    data = archive.read(name)
                    if sha256(data).hexdigest() != sums[name]:
                        raise ValueError(f"Checksum mismatch for {name}")
                    (directory / Path(name).name).write_bytes(data)
                if app not in COMPACT:
                    continue
                xml = directory / f"{app}.xml"
                image = directory / f"{app}_ALL_Images.zip"
                source = xml.read_bytes()
                check_bounds(source, content_width(1440, 540), app)
                for panel in COMPACT_PANELS:
                    width = content_width(*panel)
                    factor = Fraction(width, content_width(1440, 540))
                    target = layouts / f"{panel[0]}x{panel[1]}"
                    target.mkdir(parents=True, exist_ok=True)
                    scaled = compact_xml(source, factor)
                    check_bounds(scaled, width, app)
                    (target / xml.name).write_bytes(scaled)
                    if image.is_file():
                        (target / image.name).write_bytes(compact_images(image.read_bytes(), factor))
    if fullscreen:
        for directory in fullscreen.glob("ID6*"):
            if not directory.is_dir() or not (directory / f"{directory.name}.manifest").is_file():
                raise ValueError(f"Invalid full-screen plugin folder: {directory}")
            app = directory.name
            shutil.copytree(directory, payload / app, dirs_exist_ok=True)
            xml = directory / f"{app}.xml"
            source = xml.read_bytes()
            base_width = content_width(1280, 480)
            check_bounds(source, base_width, app)
            for width, height in COMPACT_PANELS:
                compact_width = content_width(width, height)
                target = layouts / f"{width}x{height}"
                target.mkdir(parents=True, exist_ok=True)
                scaled = compact_xml(source, Fraction(compact_width, base_width), horizontal_only=True)
                check_bounds(scaled, compact_width, app)
                (target / xml.name).write_bytes(scaled)
    if entrynav:
        digests = json.loads((entrynav / "SHA256.json").read_text())
        out = payload / "entrynav"
        out.mkdir(exist_ok=True)
        compact = out / "800x480"
        compact.mkdir(exist_ok=True)
        compact_digests = {}
        for name, digest in digests.items():
            if not re.fullmatch(r"[a-z0-9-]+-[0-9]+\.[0-9]+\.[0-9]+(?:-(?:beta|test)\.[0-9]+)?-armhf\.tar", name):
                raise ValueError(f"Unexpected EntryNav2 archive: {name}")
            data = (entrynav / name).read_bytes()
            if sha256(data).hexdigest() != digest:
                raise ValueError(f"EntryNav2 checksum mismatch: {name}")
            (out / name).write_bytes(data)
            if "-test." in name:
                small = compact_entry_archive(data)
                (compact / name).write_bytes(small)
                compact_digests[name] = sha256(small).hexdigest()
        (compact / "SHA256.json").write_text(json.dumps(compact_digests, indent=2) + "\n")
        shutil.copyfile(entrynav / "SHA256.json", out / "SHA256.json")
        profiles = entrynav / "profiles.json"
        if profiles.is_file():
            shutil.copyfile(profiles, out / "profiles.json")
    (payload / "tools").mkdir(exist_ok=True)
    shutil.copyfile(qnx_fileops, payload / "tools/gaugedeck-fileops")
    (installer / "native").mkdir(exist_ok=True)
    shutil.copyfile(repo / "source/native/fileops.c", installer / "native/fileops.c")
    shutil.copyfile(repo / "source/docs/installer-profiles.json", installer / "installer-profiles.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path)
    parser.add_argument("qnx_fileops", type=Path)
    parser.add_argument("--fullscreen", type=Path, help="Full-screen bundle to stage for all supported panels")
    parser.add_argument("--entrynav", type=Path, help="Verified EntryNav2 archives and SHA256.json")
    args = parser.parse_args()
    stage(args.repo, args.qnx_fileops, Path(__file__).resolve().parent, args.fullscreen, args.entrynav)
