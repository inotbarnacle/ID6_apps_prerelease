#!/usr/bin/env python3
"""Render host-test model snapshots with generated artwork, not a QNX renderer."""
import argparse
from io import BytesIO
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZipFile

from PIL import Image, ImageDraw
from make_shift_profiles import ROOT, font


def preview(images, snapshots, xml):
    root = ET.fromstring(xml)
    states = root.find(".//hmiState[@id='100']/components")
    initial = {int(node.get("id")): int(node.get("imageId")) for node in root.findall(".//models/imageIdModel")}
    output = Image.new("RGBA", (1200, 1320), "#101722")
    draw = ImageDraw.Draw(output)
    cases = (("gasoline", "Gasoline: 6250 RPM, existing target reached"),
             ("diesel", "Diesel: 2500 RPM, target 3900, display 5000"),
             ("stale", "Stale RPM: shift light suppressed"))
    with ZipFile(images) as archive:
        for index, (name, caption) in enumerate(cases):
            models = dict(initial)
            models.update({int(model): int(image) for model, image in
                           (row.split() for row in (snapshots/f"{name}.models").read_text().splitlines())})
            y = index*440
            draw.text((15, y+3), caption, fill="#f0f4fb", font=font(20))
            draw.text((15, y+28), "HOST SIMULATION - no QNX or vehicle validation", fill="#90a4ba", font=font(15))
            for node in states.findall("image"):
                props = {p.get("id"): int(p.get("value")) for p in node.findall("properties/property")
                         if (p.get("value") or "").lstrip("-").isdigit()}
                asset = models[int(node.get("model"))]
                with Image.open(BytesIO(archive.read(f"{asset}.png"))) as image:
                    tile = image.convert("RGBA").resize((props["9"], props["10"]), Image.Resampling.LANCZOS)
                    output.alpha_composite(tile, (props["20"], y+50+props["21"]))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", type=Path, default=ROOT/"manifests/ShiftDeck_ALL_Images.zip")
    parser.add_argument("--snapshots", type=Path, default=ROOT/"build/shift-preview-models")
    parser.add_argument("--out", type=Path, default=ROOT/"build/shift-profiles-preview.png")
    args = parser.parse_args()
    preview(args.images, args.snapshots, (ROOT/"manifests/ShiftDeck.xml").read_bytes()).save(args.out)


if __name__ == "__main__":
    main()
