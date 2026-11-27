"""Append N57 shift-window controls and private artwork without moving upstream components."""
import argparse
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1] / "native"


def update_xml(path):
    import shift_art as art
    tree = ET.parse(path)
    app = tree.find("pluginApp")
    actions, models, states = (app.find(name) for name in ("actions", "models", "hmiStates"))
    main = states.find("hmiState[@id='100']")
    content = main.find("components")
    for number in range(121, 126):
        if actions.find(f"raAction[@id='{number}']") is None:
            ET.SubElement(actions, "raAction", id=str(number))
    if actions.find("hmiAction[@id='120']") is None:
        ET.SubElement(actions, "hmiAction", id="120", target="102")
    for identifier in (80, 83):
        if models.find(f"raDataModel[@id='{identifier}']") is None:
            ET.SubElement(models, "raDataModel", id=str(identifier), modelType="Richtext")
    for identifier, image in ((81, art.N57_LOWER_MARKER), (82, art.N57_UPPER_MARKER),
                              (84, art.N57_WINDOW_LEGEND)):
        if models.find(f"imageIdModel[@id='{identifier}']") is None:
            ET.SubElement(models, "imageIdModel", id=str(identifier), imageId=str(image))
    options = main.find("optionComponents")
    if options.find("button[@id='412']") is None:
        node = ET.SubElement(options, "button", id="412", action="120", text="N57 shift window")
        ET.SubElement(ET.SubElement(node, "properties"), "property", id="3", value="false")

    def position(node, x, y, width, height):
        props = ET.SubElement(node, "properties")
        for key, value in ((20, x), (21, y), (9, width), (10, height), (2, "false"), (3, "false")):
            ET.SubElement(props, "property", id=str(key), value=str(value))

    for component, model, rpm in ((250, 81, 4600), (251, 82, 4800)):
        if content.find(f"image[@id='{component}']") is None:
            position(ET.SubElement(content, "image", id=str(component), model=str(model)),
                     52 + art.n57_axis_x(rpm) - 3, 239, 64, 9)
    if content.find("label[@id='252']") is None:
        position(ET.SubElement(content, "label", id="252", model="80"), 850, 120, 300, 54)
    if content.find("image[@id='253']") is None:
        position(ET.SubElement(content, "image", id="253", model="84"), 850, 75, 300, 135)
    if states.find("hmiState[@id='102']") is None:
        settings = ET.SubElement(states, "hmiState", id="102", parentId="100", text="N57 shift window")
        nodes = ET.SubElement(settings, "components")
        ET.SubElement(nodes, "label", id="450", model="83")
        for offset, caption in enumerate(("Lower -100 RPM", "Lower +100 RPM", "Upper -100 RPM",
                                          "Upper +100 RPM", "Reset: 4600-4800 RPM")):
            ET.SubElement(nodes, "button", id=str(451 + offset), action=str(121 + offset), text=caption)
    ET.indent(tree, space="  ")
    tree.write(path, encoding="utf-8", xml_declaration=True)


def update_images(source, dest):
    import shift_art as art
    from PIL import Image, ImageChops
    added = {}
    for identifier, bitmap in art.n57_bitmaps():
        image = BytesIO()
        bitmap.save(image, format="PNG", optimize=True)
        added[f"{identifier}.png"] = image.getvalue()
    with ZipFile(source) as original, ZipFile(dest, "w", ZIP_DEFLATED) as out:
        for step in range(129):
            image = Image.open(BytesIO(original.read(f"{56400 + step}.png"))).convert("RGBA")
            mask = Image.new("L", image.size)
            for color in ("#409af6", "#ffbf52"):
                difference = ImageChops.difference(image.convert("RGB"), Image.new("RGB", image.size, color))
                r, g, b = difference.split()
                matches = ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 0 if v else 255)
                mask = ImageChops.lighter(mask, matches)
            for base, color in ((art.N57_SWEEP_NORMAL, "#409af6"), (art.N57_SWEEP_WARNING, art.YELLOW)):
                tinted = image.copy()
                tinted.paste(color, mask=mask)
                tinted.putalpha(image.getchannel("A"))
                data = BytesIO()
                tinted.save(data, format="PNG", optimize=True)
                added[f"{base + step}.png"] = data.getvalue()
        for entry in original.infolist():
            if entry.filename not in added:
                out.writestr(entry, original.read(entry))
        for name, data in added.items():
            entry = ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            out.writestr(entry, data, ZIP_DEFLATED)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", type=Path)
    parser.add_argument("--images-out", type=Path)
    args = parser.parse_args()
    if bool(args.images) != bool(args.images_out) or (args.images and args.images == args.images_out):
        parser.error("provide distinct --images and --images-out paths")
    update_xml(ROOT / "manifests/ShiftDeck.xml")
    if args.images:
        import shift_art as art
        update_images(args.images, args.images_out)
        (ROOT / "src/plugins/shift_assets.h").write_text(art.constants_text())
