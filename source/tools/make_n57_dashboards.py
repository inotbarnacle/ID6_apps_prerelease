"""Generate PowerDeck using the verified HMI content rectangle."""
import argparse
from functools import lru_cache
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

from PIL import Image, ImageDraw, ImageFont
from content_layout import WIDTH, HEIGHT, flow_widths

ROOT = Path(__file__).resolve().parents[1] / "native"
APPS = {"PowerDeck": (121, 220)}
INK, MUTED = "#f0f4fb", "#9eb0c2"


@lru_cache
def font(size):
    candidate = Path("/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf")
    return ImageFont.truetype(str(candidate) if candidate.is_file() else "DejaVuSansCondensed-Bold.ttf", size)


def text_image(size, text, points, color=INK):
    image = Image.new("RGBA", size)
    draw = ImageDraw.Draw(image)
    draw.text((size[0] / 2, size[1] / 2), text, font=font(points), anchor="mm", fill=color)
    return image


def digit_image(glyph):
    image = text_image((104, 98), glyph, 94)
    bounds = image.getbbox()
    result = Image.new("RGBA", (52, 98))
    if bounds:
        ink = image.crop(bounds)
        # Keep the fixed HMI digit spacing with wider fallback fonts too.
        if ink.width > 34:
            ink = ink.resize((34, ink.height), Image.Resampling.LANCZOS)
        result.alpha_composite(ink, ((52 - ink.width) // 2, bounds[1]))
    return result


def own_bitmaps(name):
    if name not in APPS:
        raise KeyError(name)
    background = Image.new("RGBA", (WIDTH, HEIGHT))
    draw = ImageDraw.Draw(background)
    for col in range(3):
        x = col * (WIDTH // 3)
        draw.rounded_rectangle((x + 3, 3, x + WIDTH // 3 - 6, 416),
                               radius=10, fill="#1b2531")
    yield 60000, background
    for i, title in enumerate(("ENGINE RPM", "DDE TORQUE", "POWER CALC. (kW)")):
        yield 60001 + i, text_image((WIDTH // 3 - 24, 40), title, 30, MUTED)
    for i, unit in enumerate(("RPM", "Nm", "kW", "PS")):
        yield 60005 + i, text_image((70, 32), unit, 30, MUTED)
    for index, glyph in enumerate("0123456789- ."):
        yield 60010 + index, digit_image(glyph)


def position(component, x, y, width, height):
    props = ET.SubElement(component, "properties")
    for key, value in ((20, x), (21, y), (9, width), (10, height), (2, "false"), (3, "true")):
        ET.SubElement(props, "property", id=str(key), value=str(value))


def document(name):
    connector, weight = APPS[name]
    doc = ET.Element("pluginApps")
    app = ET.SubElement(doc, "pluginApp", applicationType="VehicleInformation",
                        applicationWeight=str(weight), text=name)
    entry = ET.SubElement(app, "entryButton", id="1", action="10")
    props = ET.SubElement(entry, "properties")
    ET.SubElement(props, "property", id="23", value="2")
    actions = ET.SubElement(app, "actions")
    ET.SubElement(actions, "hmiAction", id="10", target="100")
    models = ET.SubElement(app, "models")
    ET.SubElement(models, "raDataModel", id="50", modelType="Richtext")
    states = ET.SubElement(app, "hmiStates")
    page = ET.SubElement(states, "hmiState", id="100", text=name)
    content = ET.SubElement(page, "components")
    def graphic(component, model, asset, x, y, width, height):
        ET.SubElement(models, "imageIdModel", id=str(model), imageId=str(asset))
        node = ET.SubElement(content, "image", id=str(component), model=str(model))
        position(node, x, y, width, height)
    graphic(200, 40, 60000, 0, 0, WIDTH, HEIGHT)
    column = WIDTH // 3
    for slot in range(3):
        x = slot * column
        graphic(210 + slot, 41 + slot, 60001 + slot, x + 12, 44, column - 24, 40)
        for digit in range(6):
            graphic(300 + slot * 10 + digit, 60 + slot * 10 + digit, 60020,
                    x + (column - 6 * 48) // 2 + digit * 48, 124, 48, 132)
        if slot != 2:
            graphic(220 + slot, 46 + slot, 60005 + slot,
                    x + (column - 70) // 2, 288, 70, 32)
    # Secondary metric horsepower is derived from the same kW reading.
    for digit in range(6):
        graphic(390 + digit, 90 + digit, 60020,
                2 * column + (column - 264) // 2 + digit * 30, 282, 30, 54)
    graphic(229, 49, 60008, 2 * column + (column - 264) // 2 + 204, 292, 70, 32)
    label = ET.SubElement(content, "label", id="250", model="50")
    position(label, 12, 360, WIDTH-24, 54)
    import power_graph_art
    power_graph_art.augment(app)
    options = page.find("optionComponents")
    if options is not None:
        page.remove(options)
        page.insert(0, options)
    flow_widths(app)
    return doc


def manifest(name):
    doc = ET.Element("plugin")
    for tag, value in (("name", name), ("type", "App"), ("file", "{pluginDir}/" + name)):
        ET.SubElement(doc, tag).text = value
    config = ET.SubElement(doc, "config")
    ET.SubElement(config, "autoStart").text = "yes"
    connector = ET.SubElement(ET.SubElement(config, "dependencies"), "pluginAppConnector")
    ET.SubElement(connector, "type").text = "Etch"
    ET.SubElement(connector, "id").text = str(APPS[name][0])
    ET.SubElement(connector, "asyncCallbacks").text = "yes"
    params = ET.SubElement(config, "parameters")
    ET.SubElement(params, "DescFile").text = "{pluginDir}/" + name + ".xml"
    ET.SubElement(params, "ImageDBFile").text = "{pluginDir}/" + name + "_ALL_Images.zip"
    return doc


def generate(output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    for name in APPS:
        for suffix, tree in (("xml", document(name)), ("manifest", manifest(name))):
            ET.indent(tree, space="  ")
            ET.ElementTree(tree).write(output / f"{name}.{suffix}", encoding="utf-8", xml_declaration=True)
        bitmaps = list(own_bitmaps(name))
        import power_graph_art
        bitmaps += list(power_graph_art.bitmaps())
        assert len({identifier for identifier, _ in bitmaps}) == len(bitmaps)
        with ZipFile(output / f"{name}_ALL_Images.zip", "w", ZIP_DEFLATED) as archive:
            for identifier, image in sorted(bitmaps):
                data = BytesIO()
                image.save(data, format="PNG", optimize=True)
                info = ZipInfo(f"{identifier}.png", date_time=(2026, 1, 1, 0, 0, 0))
                archive.writestr(info, data.getvalue(), compress_type=ZIP_DEFLATED)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "manifests")
    generate(parser.parse_args().out)
