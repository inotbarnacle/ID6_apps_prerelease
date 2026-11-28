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
    actions = ET.SubElement(app, "actions")
    ET.SubElement(actions, "hmiAction", id="10", target="100")
    ET.SubElement(actions, "raAction", id="5750")
    ET.SubElement(actions, "raAction", id="5752")
    models = ET.SubElement(app, "models")
    state = ET.SubElement(ET.SubElement(app, "hmiStates"), "hmiState", id="100", text="WarmDeck")
    ET.SubElement(models, "raDataModel", id="55", modelType="Richtext")
    ET.SubElement(models, "raDataModel", id="56", modelType="Richtext")
    options = ET.SubElement(state, "optionComponents")
    ET.SubElement(options, "button",
                  id="5750", action="5750", model="55", text="DCT temperature")
    ET.SubElement(options, "button", id="5752", action="5752", model="56", text="Temperature units")
    content = ET.SubElement(state, "components")

    def place(tag, identifier, model, geometry, text=None):
        component = ET.SubElement(content, tag, id=str(identifier))
        if model is not None:
            component.set("model", str(model))
        if text is not None:
            component.set("text", text)
        add_props(component, *geometry, image=tag == "image")

    ET.SubElement(models, "imageIdModel", id="65", imageId="58150")
    place("image", 200, 65, (32, 14, 1160, 428))
    for i in range(5):
        ET.SubElement(models, "raDataModel", id=str(50+i), modelType="Richtext")
    for i, name in enumerate(("Engine oil","Coolant","Transmission")):
        x = 54+i*382
        ET.SubElement(models, "imageIdModel", id=str(60+i), imageId="58100")
        place("label", 220+i, None, (x, 48, 320, 45), name)
        place("label", 230+i, 50+i, (x, 101, 320, 72))
        place("image", 240+i, 60+i, (x, 190, 294, 16))
        ET.SubElement(models, "raDataModel", id=str(70+i), modelType="Richtext")
        place("label", 250+i, 70+i, (x, 222, 320, 48))
    place("label", 260, 53, (54, 304, 1116, 50))
    place("label", 261, 54, (54, 363, 1116, 50))
    ET.indent(tree, space="  ")
    ET.ElementTree(tree).write(PLUGIN/"WarmDeck.xml",encoding="utf-8",xml_declaration=True)
    with ZipFile(PLUGIN/"WarmDeck_ALL_Images.zip","w",ZIP_DEFLATED) as archive:
        def save(identifier, image):
            buffer=BytesIO()
            image.save(buffer,format="PNG",optimize=True)
            archive.writestr(ZipInfo(f"{identifier}.png"),buffer.getvalue(),ZIP_DEFLATED)

        image=Image.new("RGBA",(1160,428))
        draw=ImageDraw.Draw(image)
        for i in range(3):
            x=382*i
            draw.rounded_rectangle((x,0,x+380,272),radius=14,fill="#171e26")
        draw.rounded_rectangle((0,277,1159,427),radius=14,fill="#19222d")
        save(58150,image)
        for step in range(21):
            image=Image.new("RGBA",(294,16))
            draw=ImageDraw.Draw(image)
            draw.rounded_rectangle((0,0,293,15),radius=5,fill="#303e4e")
            if step:
                color="#59c4ec" if step<14 else "#f1ab66" if step<20 else "#71d5a0"
                draw.rounded_rectangle((0,0,round(294*step/20)-1,15),radius=5,fill=color)
            save(58100+step,image)


if __name__=="__main__":
    main()
