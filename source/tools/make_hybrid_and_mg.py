"""Generate the HybridDeck display and the MG Flasher native menu resources."""

import argparse
from io import BytesIO
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageDraw, ImageFont

MANIFESTS = Path(__file__).resolve().parents[1] / "native/manifests"


def add(parent, tag, **attrs):
    return ET.SubElement(parent, tag, {key: str(value) for key, value in attrs.items()})


def model(parent, number):
    add(parent, "raDataModel", id=number, modelType="Richtext")


def page(states, number, title, parent=None):
    attrs = {"id": number, "text": title}
    if parent is not None:
        attrs["parentId"] = parent
    return add(states, "hmiState", **attrs)


def layout(node, x, y, width, height):
    properties = add(node, "properties")
    for key, value in ((20, x), (21, y), (9, width), (10, height)):
        add(properties, "property", id=key, value=value)
    if node.tag == "image":
        add(properties, "property", id=2, value="false")
        add(properties, "property", id=3, value="true")


def app(title, weight):
    root = ET.Element("pluginApps")
    plugin = add(root, "pluginApp", applicationType="VehicleInformation",
                 applicationWeight=weight, text=title)
    entry = add(plugin, "entryButton", id=1, action=10)
    add(add(entry, "properties"), "property", id=23, value=2)
    actions, models, states = (add(plugin, tag) for tag in ("actions", "models", "hmiStates"))
    add(actions, "hmiAction", id=10, target=100)
    return root, actions, models, states


def write_xml(root, path):
    ET.indent(root, space="  ")
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def font(size):
    for name in ("DejaVuSans.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default(size=size)


def mg_menu(images):
    root, actions, models, states = app("MG Flasher", 216)
    main = add(page(states, 100, "MG Flasher"), "components")
    add(actions, "raAction", id=20)
    add(main, "button", id=20, action=20, text="Refresh current settings")
    model(models, 50)
    add(main, "label", id=50, model=50)
    groups = (
        ("Switchable maps", 21, [f"Slot {i}" for i in range(5)]),
        ("Burbles", 30, [f"Slot {i}" for i in range(6)]),
        ("Max Cooling", 40, ["Off", "On"]),
    )
    logo_id, logo_model = 59105, 65
    logo_file = MANIFESTS.parent / "assets" / "mg-flasher-wordmark.png"
    with Image.open(logo_file) as asset:
        logo = asset.convert("RGBA")
    add(models, "imageIdModel", id=logo_model, imageId=logo_id)
    tiles = [(logo_id, logo)]
    for i, (title, first, labels) in enumerate(groups, 1):
        state, navigation, result = 100+i, 10+i, 50+i
        add(actions, "hmiAction", id=navigation, target=state)
        add(main, "button", id=navigation, action=navigation, text=title)
        options = add(page(states, state, title, 100), "components")
        model(models, result)
        result_label = add(options, "label", id=result, model=result)
        if i == 1:
            layout(add(options, "image", id=201, model=logo_model),
                   468, 12, 306, 90)  # centred over the five tiles
            layout(result_label, 0, 372, 1248, 72)
        for action, label in enumerate(labels, first):
            add(actions, "raAction", id=action)
            if i != 1:
                add(options, "button", id=action, action=action, text=label)
                continue
            slot = action - first
            image_id = 59100 + slot
            add(models, "imageIdModel", id=60+slot, imageId=image_id)
            layout(add(options, "button", id=action, action=action, imageModel=60+slot),
                   slot*252, 120, 240, 240)  # five 240-wide tiles, 12-unit gutters
            tile = Image.new("RGBA", (240, 240))
            draw = ImageDraw.Draw(tile)
            draw.rounded_rectangle((1,1,238,238), radius=23, fill="#141719", outline="#485057", width=2)
            draw.text((120,90), str(slot), font=font(69), anchor="mm", fill="#f1f3f6")
            draw.text((120,190), "SLOT", font=font(22), anchor="mm", fill="#aab3bb")
            tiles.append((image_id, tile))
    write_xml(root, MANIFESTS/"MgFlasher.xml")
    with ZipFile(images, "w", ZIP_DEFLATED) as archive:
        for identifier, tile in tiles:
            bitmap = BytesIO()
            tile.save(bitmap, format="PNG", optimize=True)
            archive.writestr(ZipInfo(f"{identifier}.png"), bitmap.getvalue(), ZIP_DEFLATED)


def hybrid_deck(images):
    root, actions, models, states = app("HybridDeck", 207)
    add(actions, "raAction", id=21)
    state = page(states, 100, "HybridDeck")
    options = add(state, "optionComponents")
    add(options, "button", id=21, action=21, text="Refresh")
    components = add(state, "components")
    add(models, "imageIdModel", id=60, imageId=59000)
    layout(add(components, "image", id=200, model=60), 0, 12, 1248, 396)  # 4 x 2 cards of 303 x 192, 12-unit gutters

    image = Image.new("RGBA", (1248, 396), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    values = ("HV charge", "Pack voltage", "Pack current", "Battery coolant",
              "Engine coolant", "12 V module voltage", "Battery health", "Battery power")
    for cell, signal in enumerate((0, 7, 6, 1, 2, 3, 4, 5)):
        x, y = cell%4*315, cell//4*204
        draw.rounded_rectangle((x,y,x+302,y+191), radius=10, fill="#1b2935", outline="#3a5265", width=2)
        draw.rectangle((x+24,y+24,x+63,y+27), fill="#59c6b8")
        layout(add(components, "label", id=220+signal, text=values[signal]), x+24,y+42,255,42)
        model(models, 50+signal)
        layout(add(components, "label", id=240+signal, model=50+signal), x+24,y+108,255,48)
    model(models, 58)
    layout(add(components, "label", id=250, model=58), 24,420,1224,45)
    write_xml(root, MANIFESTS/"HybridDeck.xml")
    bitmap = BytesIO()
    image.save(bitmap, format="PNG", optimize=True)
    with ZipFile(images, "w", ZIP_DEFLATED) as archive:
        archive.writestr(ZipInfo("59000.png"), bitmap.getvalue(), ZIP_DEFLATED)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", type=Path, default=MANIFESTS/"HybridDeck_ALL_Images.zip")
    parser.add_argument("--mg-images", type=Path, default=MANIFESTS/"MgFlasher_ALL_Images.zip")
    args = parser.parse_args()
    mg_menu(args.mg_images)
    hybrid_deck(args.images)
