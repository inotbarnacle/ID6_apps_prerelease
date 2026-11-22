#!/usr/bin/env python3
"""Stage checked QNX packages and create compact RHMI artwork for the desktop installer."""
import argparse
from fractions import Fraction
from hashlib import sha256
from io import BytesIO
from pathlib import Path
import re
import shutil
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
    "hud-gauges": ("HudTest",),
    "warmdeck": ("WarmDeck",),
}
COMPACT = {"BoostDeck", "ShiftDeck", "EngineCodes", "MhdMaps", "Bootmod3Suite", "MgFlasher",
           "LaunchDeck", "HudTest", "WarmDeck", "HybridDeck"}
CONTENT_HEIGHT = 477
GEOMETRY = re.compile(rb'(<property id=")(9|10|20|21)(" value=")(-?\d+)(")')


def content_width(width, height):
    # Measured by Andreas Boehm in idrive6-layout-preview and reported in ID6 issue #113.
    return 540 * width // height - 180


def compact_xml(data, factor):
    def scale(match):
        value = round(int(match[4]) * factor)
        if match[2] in {b"9", b"10"}:
            value = max(1, value)
        return match[1] + match[2] + match[3] + str(value).encode() + match[5]

    return GEOMETRY.sub(scale, data)


def center_vertically(data):
    """Shrunk layouts are shorter than the content area: centre each page's components in it."""
    root = ET.fromstring(data)
    for state in root.iter("hmiState"):
        boxes = []
        for component in state.findall("components/*"):
            props = {p.get("id"): p for p in component.findall("./properties/property")}
            if {"21", "10"} <= props.keys():
                boxes.append((props["21"], int(props["21"].get("value")), int(props["10"].get("value"))))
        if not boxes:
            continue
        top, bottom = min(y for _, y, _ in boxes), max(y + h for _, y, h in boxes)
        shift = (CONTENT_HEIGHT - (bottom - top)) // 2 - top
        for prop, y, _ in boxes:
            prop.set("value", str(y + max(0, shift)))
    ET.indent(root, space="  ")
    out = BytesIO()
    ET.ElementTree(root).write(out, encoding="utf-8", xml_declaration=True)
    return out.getvalue()


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


def stage(repo, qnx_fileops, installer):
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
                for panel in ((800, 480), (960, 480)):
                    width = content_width(*panel)
                    factor = Fraction(width, content_width(1440, 540))
                    target = layouts / f"{panel[0]}x{panel[1]}"
                    target.mkdir(parents=True, exist_ok=True)
                    scaled = center_vertically(compact_xml(source, factor))
                    check_bounds(scaled, width, app)
                    (target / xml.name).write_bytes(scaled)
                    if image.is_file():
                        (target / image.name).write_bytes(compact_images(image.read_bytes(), factor))
    (payload / "tools").mkdir(exist_ok=True)
    shutil.copyfile(qnx_fileops, payload / "tools/gaugedeck-fileops")
    (installer / "native").mkdir(exist_ok=True)
    shutil.copyfile(repo / "source/native/fileops.c", installer / "native/fileops.c")
    shutil.copyfile(repo / "source/docs/installer-profiles.json", installer / "installer-profiles.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path)
    parser.add_argument("qnx_fileops", type=Path)
    args = parser.parse_args()
    stage(args.repo, args.qnx_fileops, Path(__file__).resolve().parent)
