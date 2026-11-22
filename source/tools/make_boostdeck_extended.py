"""Generate BoostDeck settings, dashboard XML, and circular dial resources."""
import argparse
from io import BytesIO
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from make_bars import COLORS, MULTICOLOR, THEMES, bar_images

ROOT = Path(__file__).resolve().parents[1] / "native"
XML = ROOT / "manifests/BoostDeck.xml"
SIGNALS = re.findall(r'^SIGNAL\(\w+, "([^"]+)"',
                     (ROOT / "src/common/boost_signals.def").read_text(), re.M)
SLOTS = 9
BACKGROUND = 56300
BAR_EMPTY = 55000
VERTICAL_EMPTY = 56100
ALERT_BACKGROUND = 56999
TRANSPARENT = 57000
DIAL_FIRST = 57001
DIAL_STEPS = 12
DIAL_SIZE = 96
# Layout rules (docs/LAYOUT.md): 6-unit grid, 12-unit gutters. Eight 258 x 192 tiles in four columns, one
# 168 x 396 tile on the right, and a 45-high alert bar below, spanning 1248 x 453 from y = 12.
GUTTER, TILE_W, TILE_H, TALL_W = 12, 258, 192, 168
WIDTH, HEIGHT = 4 * (TILE_W + GUTTER) + TALL_W, 2 * TILE_H + GUTTER
TOP, ALERT_H = 12, 45
BAR_SIZE, VERTICAL_SIZE = (210, 18), (42, 246)
TILE_FILL, TILE_RADIUS = (23, 27, 34, 255), 12
SETTINGS = (
    ("Pressure", 3), ("Speed", 2), ("Temperature", 2), ("Gauge style", 2),
    ("Alert mode", 2), ("Coolant high", 10), ("Oil temp high", 10),
    ("Oil pressure low", 6),
)


def tile(i):
    return ((TILE_W + GUTTER) * (i // 2), (TILE_H + GUTTER) * (i % 2)) if i < 8 else (4 * (TILE_W + GUTTER), 0)


def placed(parent, tag, identifier, model, x, y, w, h):
    node = ET.SubElement(parent, tag, id=str(identifier), model=str(model))
    props = ET.SubElement(node, "properties")
    for prop, value in ((20, x), (21, y + TOP), (9, w), (10, h)):
        ET.SubElement(props, "property", id=str(prop), value=str(value))
    if tag == "image":
        for prop, value in ((2, "false"), (3, "true")):
            ET.SubElement(props, "property", id=str(prop), value=value)


def button(parent, identifier, action, text="", model=None):
    node = ET.SubElement(parent, "button", id=str(identifier), action=str(action), text=text)
    if model is not None:
        node.set("model", str(model))


def radio_model(models, identifier):
    ET.SubElement(models, "raBoolModel", id=str(identifier))


def radio(parent, identifier, action, selected, text="", text_model=None):
    node = ET.SubElement(parent, "radiobutton", id=str(identifier), action=str(action),
                         model=str(selected), text=text)
    if text_model is not None:
        node.set("textModel", str(text_model))


def goto(actions, number, target):
    ET.SubElement(actions, "hmiAction", id=str(number), target=str(target))


def choose(actions, number, callback, return_action, target):
    group = ET.SubElement(ET.SubElement(actions, "combinedAction", id=str(number)), "actions")
    ET.SubElement(group, "raAction", id=str(callback))
    goto(group, return_action, target)


def page(states, identifier, parent, title):
    state = ET.SubElement(states, "hmiState", id=str(identifier), parentId=str(parent), text=title)
    return ET.SubElement(state, "components")


def richtext(models, identifier):
    ET.SubElement(models, "raDataModel", id=str(identifier), modelType="Richtext")


def image_model(models, identifier, initial):
    ET.SubElement(models, "imageIdModel", id=str(identifier), imageId=str(initial))


def update_xml():
    tree = ET.parse(XML)
    app = tree.find("pluginApp")
    actions, models, states = (app.find(name) for name in ("actions", "models", "hmiStates"))
    actions.clear()
    models.clear()
    states.clear()
    goto(actions, 10, 100)
    gauges = ET.SubElement(states, "hmiState", id="100", text="BoostDeck")
    opts = ET.SubElement(gauges, "optionComponents")
    goto(actions, 520, 520)
    button(opts, 33, 520, "BoostDeck settings")
    components = ET.SubElement(gauges, "components")
    image_model(models, 69, BACKGROUND)
    placed(components, "image", 209, 69, 0, 0, WIDTH, HEIGHT)
    image_model(models, 68, TRANSPARENT)
    richtext(models, 119)
    placed(components, "image", 208, 68, 0, HEIGHT + GUTTER, WIDTH, ALERT_H)
    placed(components, "label", 218, 119, 24, HEIGHT + GUTTER, WIDTH - 48, ALERT_H)

    settings = page(states, 520, 100, "BoostDeck settings")
    richtext(models, 118)
    ET.SubElement(settings, "label", id="118", model="118")
    for row, (name, count) in enumerate(SETTINGS):
        target = 521 + row
        goto(actions, target, target)
        richtext(models, 120 + row)
        button(settings, 120 + row, target, model=120 + row)
        options = page(states, target, 520, name)
        for option in range(count):
            offset = row * 20 + option
            richtext(models, 700 + offset)
            radio_model(models, 5000 + offset)
            choose(actions, 2000 + offset, 2400 + offset, 2800 + offset, 520)
            radio(options, 700 + offset, 2000 + offset, 5000 + offset,
                  text_model=700 + offset)
    for model, target, title in ((110, 104, "Gauge color"), (360, 501, "Engine profile"),
                                  (None, 500, "Configure gauges")):
        goto(actions, target, target)
        if model is not None:
            richtext(models, model)
        button(settings, 550 + target, target, "" if model else title, model)
    colors = page(states, 104, 520, "Gauge color")
    for i, name in enumerate(THEMES):
        radio_model(models, 5200 + i)
        choose(actions, 2200 + i, 40 + i, 3000 + i, 520)
        radio(colors, 390 + i, 2200 + i, 5200 + i, name)
    profile = page(states, 501, 520, "Engine profile")
    for i, caption in enumerate(("Gasoline", "Diesel (standard OBD PIDs)")):
        radio_model(models, 5210 + i)
        choose(actions, 2210 + i, 502 + i, 3010 + i, 520)
        radio(profile, 591 + i, 2210 + i, 5210 + i, caption)
    config = page(states, 500, 520, "Configure gauges")
    for signal in range(len(SIGNALS)):
        radio_model(models, 5300 + signal)
        choose(actions, 3500 + signal, 600 + signal, 4500 + signal, 500)
    for i in range(SLOTS):
        x, y = tile(i)
        for model in (300 + i, 340 + i, 370 + i):
            richtext(models, model)
        image_model(models, 320 + i, BAR_EMPTY if i < 8 else VERTICAL_EMPTY)
        image_model(models, 330 + i, TRANSPARENT)
        if i < 8:
            placed(components, "label", 260+i, 370+i, x+24, y+6, 210, 42)
            placed(components, "label", 310+i, 300+i, x+24, y+48, 210, 48)
            placed(components, "image", 280+i, 320+i, x+24, y+150, *BAR_SIZE)
            placed(components, "image", 290+i, 330+i, x+78, y+90, DIAL_SIZE, DIAL_SIZE)
        else:
            placed(components, "label", 260+i, 370+i, x+12, y+6, 132, 42)
            placed(components, "image", 280+i, 320+i, x+54, y+54, *VERTICAL_SIZE)
            placed(components, "image", 290+i, 330+i, x+30, y+132, DIAL_SIZE, DIAL_SIZE)
            placed(components, "label", 310+i, 300+i, x+24, y+318, 108, 60)
        target = 510 + i
        goto(actions, target, target)
        button(config, 540 + i, target, model=340+i)
        options = page(states, target, 500, f"Gauge {i+1}")
        for j, caption in enumerate(SIGNALS):
            radio(options, 3500 + i*40+j, 3500+j, 5300+j, caption)
    ET.indent(tree, space="  ")
    tree.write(XML, encoding="utf-8", xml_declaration=True)


def update_images(source, dest):
    from PIL import Image, ImageDraw
    with ZipFile(source) as original, ZipFile(dest, "w", ZIP_DEFLATED) as updated:
        background = Image.new("RGBA", (WIDTH, HEIGHT))
        draw = ImageDraw.Draw(background)
        for i in range(SLOTS):
            x, y = tile(i)
            tile_w, tile_h = (TILE_W, TILE_H) if i < 8 else (TALL_W, HEIGHT)
            draw.rounded_rectangle((x, y, x + tile_w - 1, y + tile_h - 1), radius=TILE_RADIUS, fill=TILE_FILL)
        for entry in original.infolist():
            number = int(entry.filename[:-4])
            if number >= ALERT_BACKGROUND or number == BACKGROUND or BAR_EMPTY <= number < BAR_EMPTY + 513:
                continue
            data = original.read(entry)
            if VERTICAL_EMPTY <= number < VERTICAL_EMPTY + 181:
                bitmap = BytesIO()
                Image.open(BytesIO(data)).convert("RGBA").resize(VERTICAL_SIZE, Image.Resampling.LANCZOS).save(bitmap, format="PNG", optimize=True)
                data = bitmap.getvalue()
            updated.writestr(entry, data)

        def write(identifier, image):
            bitmap = BytesIO()
            image.save(bitmap, format="PNG", optimize=True)
            updated.writestr(ZipInfo(f"{identifier}.png"), bitmap.getvalue(), ZIP_DEFLATED)

        write(BACKGROUND, background)
        for identifier, image in bar_images(BAR_SIZE):
            write(identifier, image)
        warning = Image.new("RGBA", (WIDTH, ALERT_H))
        ImageDraw.Draw(warning).rounded_rectangle((0, 0, WIDTH-1, ALERT_H-1), radius=8, fill="#74252a")
        write(ALERT_BACKGROUND, warning)
        write(TRANSPARENT, Image.new("RGBA", (1, 1)))

        face = Image.new("RGBA", (DIAL_SIZE, DIAL_SIZE))
        draw = ImageDraw.Draw(face)
        draw.ellipse((3,3,92,92), fill="#18212b", outline="#354656", width=2)
        draw.arc((9,9,86,86), 135, 405, fill="#596c80", width=5)

        def point(angle, radius):
            radians = math.radians(angle)
            center = DIAL_SIZE / 2
            return center + radius * math.cos(radians), center + radius * math.sin(radians)

        for tick in range(5):
            angle = 135 + 270*tick/4
            draw.line((point(angle, 30), point(angle, 36)), fill="#c2ced9", width=2)
        write(DIAL_FIRST, face)
        for palette, color in enumerate(tuple(fill for _, fill in COLORS) + MULTICOLOR):
            for step in range(DIAL_STEPS + 1):
                image = face.copy()
                draw = ImageDraw.Draw(image)
                angle = 135 + 270*step/DIAL_STEPS
                if step:
                    draw.arc((9,9,86,86), 135, angle, fill=color, width=5)
                draw.line(((48,48), point(angle,33)), fill=color, width=3)
                draw.ellipse((44,44,52,52), fill=color)
                identifier = DIAL_FIRST + 1 + palette*(DIAL_STEPS+1) + step
                write(identifier, image)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", type=Path)
    parser.add_argument("--images-out", type=Path)
    args = parser.parse_args()
    if bool(args.images) != bool(args.images_out) or (args.images and args.images == args.images_out):
        parser.error("provide distinct --images and --images-out paths")
    update_xml()
    if args.images:
        update_images(args.images, args.images_out)
        if args.images_out.stat().st_size>850_000:
            args.images_out.unlink()
            raise ValueError("Keep the BoostDeck image archive below 850 kB")
