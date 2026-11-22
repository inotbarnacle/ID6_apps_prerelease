"""Generate the WarmDeck layout and original RHMI artwork."""
from io import BytesIO
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageDraw

from make_bars import add_props

PLUGIN = Path(__file__).resolve().parents[1] / "native" / "manifests"


def main():
    tree = ET.Element("pluginApps")
    app = ET.SubElement(tree, "pluginApp", applicationType="VehicleInformation",
                        applicationWeight="206", text="WarmDeck")
    entry = ET.SubElement(app, "entryButton", id="1", action="10")
    ET.SubElement(ET.SubElement(entry, "properties"), "property", id="23", value="2")
    ET.SubElement(ET.SubElement(app, "actions"), "hmiAction", id="10", target="100")
    models = ET.SubElement(app, "models")
    state = ET.SubElement(ET.SubElement(app, "hmiStates"), "hmiState", id="100", text="WarmDeck")
    content = ET.SubElement(state, "components")

    def place(tag, identifier, model, geometry, text=None):
        component = ET.SubElement(content, tag, id=str(identifier))
        if model is not None:
            component.set("model", str(model))
        if text is not None:
            component.set("text", text)
        add_props(component, *geometry, image=tag == "image")

    # Layout rules: 6-unit grid, 12-unit gutters, 24-unit card padding.
    ET.SubElement(models, "imageIdModel", id="65", imageId="58150")
    place("image", 200, 65, (0, 12, 1248, 453))
    for i in range(5):
        ET.SubElement(models, "raDataModel", id=str(50+i), modelType="Richtext")
    for i, (name, target) in enumerate((("Engine oil",85),("Coolant",75),("Transmission",40))):
        x = 24+i*420
        ET.SubElement(models, "imageIdModel", id=str(60+i), imageId="58100")
        place("label", 220+i, None, (x, 36, 360, 48), name)
        place("label", 230+i, 50+i, (x, 90, 360, 72))
        place("image", 240+i, 60+i, (x, 174, 360, 18))
        place("label", 250+i, None, (x, 204, 360, 48), f"Target {target} C")
    place("label", 260, 53, (24, 324, 1200, 48))
    place("label", 261, 54, (24, 384, 1200, 48))
    ET.indent(tree, space="  ")
    ET.ElementTree(tree).write(PLUGIN/"WarmDeck.xml",encoding="utf-8",xml_declaration=True)
    with ZipFile(PLUGIN/"WarmDeck_ALL_Images.zip","w",ZIP_DEFLATED) as archive:
        def save(identifier, image):
            buffer=BytesIO()
            image.save(buffer,format="PNG",optimize=True)
            archive.writestr(ZipInfo(f"{identifier}.png"),buffer.getvalue(),ZIP_DEFLATED)

        image=Image.new("RGBA",(1248,453))
        draw=ImageDraw.Draw(image)
        for i in range(3):  # three 408-wide cards, then a box as wide as all three
            draw.rounded_rectangle((420*i,0,420*i+407,275),radius=14,fill="#171e26")
        draw.rounded_rectangle((0,288,1247,452),radius=14,fill="#19222d")
        save(58150,image)
        for step in range(21):
            image=Image.new("RGBA",(360,18))
            draw=ImageDraw.Draw(image)
            draw.rounded_rectangle((0,0,359,17),radius=5,fill="#303e4e")
            if step:
                color="#59c4ec" if step<14 else "#f1ab66" if step<20 else "#71d5a0"
                draw.rounded_rectangle((0,0,round(360*step/20)-1,17),radius=5,fill=color)
            save(58100+step,image)


if __name__=="__main__":
    main()
