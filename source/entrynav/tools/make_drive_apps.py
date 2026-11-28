"""Generate RHMI resources for the standalone ShiftDeck, Bootmod3 Suite, MHD+ Suite and Engine Fault Codes apps."""
from pathlib import Path
from io import BytesIO
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo
from PIL import Image, ImageDraw
import shift_art as art

ROOT = Path(__file__).resolve().parents[1] / "plugin"


def save(doc, name):
    ET.indent(doc, space="  ")
    # LF on every system: tools/package_release.py refuses Windows line endings.
    with open(ROOT / name, "w", encoding="utf-8", newline="\n") as out:
        ET.ElementTree(doc).write(out, encoding="unicode", xml_declaration=True)


def entry(name, title, weight, connector, root_id, images=False):
    manifest = ET.parse(ROOT / "BoostDeck.manifest").getroot()
    manifest.find("name").text = name
    manifest.find("file").text = "{pluginDir}/" + name
    manifest.find("config/dependencies/pluginAppConnector/id").text = str(connector)
    params = manifest.find("config/parameters")
    params.find("DescFile").text = f"{{pluginDir}}/{name}.xml"
    if images:
        params.find("ImageDBFile").text = f"{{pluginDir}}/{name}_ALL_Images.zip"
    else:
        params.remove(params.find("ImageDBFile"))
    save(manifest, f"{name}.manifest")

    doc = ET.Element("pluginApps")
    app = ET.SubElement(doc, "pluginApp", applicationType="VehicleInformation",
                        applicationWeight=str(weight), text=title)
    entry_button = ET.SubElement(app, "entryButton", id="1", action="10")
    ET.SubElement(ET.SubElement(entry_button, "properties"), "property", id="23", value="2")
    actions = ET.SubElement(app, "actions")
    ET.SubElement(actions, "hmiAction", id="10", target=str(root_id))
    models = ET.SubElement(app, "models")
    states = ET.SubElement(app, "hmiStates")
    state = ET.SubElement(states, "hmiState", id=str(root_id), text=title)
    return doc, actions, models, states, state


def position(component, x, y, width, height):
    properties = ET.SubElement(component, "properties")
    for prop, value in ((20,x),(21,y),(9,width),(10,height)):
        ET.SubElement(properties, "property", id=str(prop), value=str(value))


def button(group, identifier, action, text):
    ET.SubElement(group, "button", id=str(identifier), action=str(action), text=text)


# ShiftDeck's Speed screen, in the order of its actions (46, 47); src/shift_deck.cpp keeps the same order.
SPEED_UNITS = ("MPH", "km/h")


def shift_app():
    name="ShiftDeck"
    doc, actions, models, states, page = entry(name,"ShiftDeck",207,104,100,images=True)
    ET.SubElement(actions,"hmiAction",id="40",target="101")
    for i in range(len(art.TARGETS)):
        ET.SubElement(actions,"raAction",id=str(400+i))
    ET.SubElement(actions,"hmiAction",id="48",target="102")
    for i in range(len(SPEED_UNITS)):
        ET.SubElement(actions,"raAction",id=str(46+i))
    ET.SubElement(models,"raDataModel",id="53",modelType="Richtext")
    ET.SubElement(models,"raDataModel",id="58",modelType="Richtext")

    options=ET.SubElement(page,"optionComponents")
    button(options,30,40,"Shift light setting")
    button(options,31,48,"Speed")
    content=ET.SubElement(page,"components")

    def graphic(model, asset, x, y, w, h):
        ET.SubElement(models,"imageIdModel",id=str(model),imageId=str(asset))
        image=ET.SubElement(content,"image",id=str(200+model-54),model=str(model))
        position(image,x,y,w,h)
        props=image.find("properties")
        ET.SubElement(props,"property",id="2",value="false")
        ET.SubElement(props,"property",id="3",value="true")

    graphic(54,art.SWEEP_BASE,52,287,art.BAR_WIDTH,90)
    graphic(55,art.LIGHT_BASE,52,10,1104,76)
    graphic(56,art.TARGET_BASE+art.DEFAULT_INDEX,1003,113,153,135)
    graphic(57,art.SHIFT_OFF,454,215,350,42)
    for i in range(4):
        graphic(60+i,art.DIGIT_BASE+(10 if i in (1,2) else 11),501+i*64,108,64,100)
    graphic(64,art.SPEED_LEGEND,53,108,200,179)
    graphic(65,art.RPM_LEGEND,764,169,68,35)
    graphic(66,art.SCALE,52,377,art.BAR_WIDTH,42)
    graphic(67,art.LOWER,52,420,1104,10)
    graphic(68,art.MARK_BASE+art.DEFAULT_INDEX,52,278,art.BAR_WIDTH,9)
    for i in range(3):
        graphic(70+i,art.DIGIT_BASE+(10 if i==2 else 11),53+i*67,139,67,109)

    settings=ET.SubElement(states,"hmiState",id="101",parentId="100",text="Shift light setting")
    items=ET.SubElement(settings,"components")
    ET.SubElement(items,"label",id="300",model="53")
    for i,rpm in enumerate(art.TARGETS):
        button(items,301+i,400+i,f"{rpm:,} RPM")

    speed=ET.SubElement(states,"hmiState",id="102",parentId="100",text="Speed")
    items=ET.SubElement(speed,"components")
    ET.SubElement(items,"label",id="310",model="58")
    for i,unit in enumerate(SPEED_UNITS):
        button(items,311+i,46+i,unit)

    save(doc,f"{name}.xml")
    art.save_assets(ROOT/f"{name}_ALL_Images.zip",ROOT.parent/"src"/"shift_assets.h")


# Bootmod3 Suite's option screens: (action that opens it, its state, its result model, title, [(action, button text)]).
# The worker's arguments for each button are in src/bm3_maps.cpp.
# No exhaust flap screen: bootmod3 sets the flap in each map, and the DME lets a tester's flap request go after about
# 20 s (the BM3 Live port's notes), so a one-off request would undo itself.
BM3_SCREENS = (
    (13, 103, 52, "Active Sound", ((28, "Off"), (29, "On"))),
    (14, 104, 53, "Burbles", ((30, "Off"), (31, "Aggression 4"), (32, "Aggression 8"), (33, "Aggression 12"))),
)


# MHD+ Suite's option screen, as BM3_SCREENS. The worker's arguments for each button are in src/mhd_maps.cpp.
# No exhaust flap screen: Live Adjustments documents no MHD flap command, and BMW's own flap request is let go by the
# DME after about 20 s, as for Bootmod3 Suite.
MHD_SCREENS = (
    (13, 103, 52, "Max Cooling", ((28, "Off"), (29, "On"))),
)


def map_app():
    # Shown as Bootmod3 Suite; the plugin keeps its 0.1.0 name, Bm3Maps, so installs update in place.
    tuner_app("Bm3Maps","Bootmod3 Suite",208,103,BM3_SCREENS,MAP_LOGO,MAP_TILES,MAP_BLANK,
              ((MAP_LOGO,map_logo("BOOTMOD3 SUITE")),(MAP_BLANK,Image.new("RGBA",(1,1))),
               *((MAP_TILES+active,map_tiles(active)) for active in range(5))))


def mhd_app():
    # MHD can't read back the active map, so its tiles have no ACTIVE variants.
    tuner_app("MhdMaps","MHD+ Suite",211,105,MHD_SCREENS,MHD_LOGO,MHD_TILES,MHD_BLANK,
              ((MHD_LOGO,map_logo("MHD+ SUITE")),(MHD_BLANK,Image.new("RGBA",(1,1))),(MHD_TILES,map_tiles(0))))


def tuner_app(name, app_title, weight, connector, screens, logo, tiles, blank, images):
    """A tuning suite: a logo, four map tiles that each start the worker, a result line, and option screens."""
    doc, actions, models, states, page = entry(name,app_title,weight,connector,100,images=True)
    ET.SubElement(models,"raDataModel",id="50",modelType="Richtext")
    options=ET.SubElement(page,"optionComponents")
    for i,(opener,state,model,title,choices) in enumerate(screens):
        ET.SubElement(actions,"hmiAction",id=str(opener),target=str(state))
        for action,_ in choices:
            ET.SubElement(actions,"raAction",id=str(action))
        ET.SubElement(models,"raDataModel",id=str(model),modelType="Richtext")
        button(options,312+i,opener,title)
        screen=ET.SubElement(states,"hmiState",id=str(state),parentId="100",text=title)
        items=ET.SubElement(screen,"components")
        for j,(action,text) in enumerate(choices):
            button(items,400+10*i+j,action,text)
        # The result under the choices: empty until the first press, so it leaves no gap above them.
        ET.SubElement(items,"label",id=str(320+i),model=str(model))
    content=ET.SubElement(page,"components")
    def graphic(component, model, asset, x, y, w, h):
        ET.SubElement(models,"imageIdModel",id=str(model),imageId=str(asset))
        image=ET.SubElement(content,"image",id=str(component),model=str(model))
        position(image,x,y,w,h)
        ET.SubElement(image.find("properties"),"property",id="2",value="false")
        ET.SubElement(image.find("properties"),"property",id="3",value="true")

    tiles_x=(1220-(4*MAP_TILE+3*MAP_GAP))//2
    graphic(200,60,logo,(1220-420)//2,30,420,48)
    graphic(201,61,tiles,tiles_x,96,4*MAP_TILE+3*MAP_GAP,MAP_TILE_H)
    ET.SubElement(models,"imageIdModel",id="62",imageId=str(blank))
    for slot in range(1,5):
        ET.SubElement(actions,"raAction",id=str(20+slot))
        # iDrive places native button text at the top left. Keep its focus
        # outline and action, and draw the centered labels on the tile art.
        tile=ET.SubElement(content,"button",id=str(150+slot),action=str(20+slot),imageModel="62")
        position(tile,tiles_x+(slot-1)*(MAP_TILE+MAP_GAP),96,MAP_TILE,MAP_TILE_H)
    position(ET.SubElement(content,"label",id="150",model="50"),tiles_x,96+MAP_TILE_H+22,4*MAP_TILE+3*MAP_GAP,53)
    save(doc,f"{name}.xml")

    with ZipFile(ROOT/f"{name}_ALL_Images.zip","w",ZIP_DEFLATED) as archive:
        for identifier,image in images:
            data=BytesIO()
            image.save(data,format="PNG",optimize=True)
            archive.writestr(ZipInfo(f"{identifier}.png"),data.getvalue(),ZIP_DEFLATED)


# Bootmod3 Suite's pictures. MAP_TILES is the four tiles with none active, MAP_TILES + n the same with map n marked active;
# src/bm3_maps.cpp shows the one that matches the worker's last answer.
MAP_LOGO, MAP_TILES, MAP_BLANK = 56000, 56010, 56002
# MHD+ Suite's, after Engine Fault Codes' 56700 range. Its tiles never mark a map.
MHD_LOGO, MHD_TILES, MHD_BLANK = 56800, 56801, 56802
MAP_TILE, MAP_TILE_H, MAP_GAP = 250, 230, 20
MAP_ACCENT = "#58ceff"


def map_logo(text):
    logo=Image.new("RGBA",(420,48))
    draw=ImageDraw.Draw(logo)
    draw.text((210,24),text,font=art.font(34),anchor="mm",fill=art.WHITE)
    return logo


def map_tiles(active):
    tiles=Image.new("RGBA",(4*MAP_TILE+3*MAP_GAP,MAP_TILE_H))
    draw=ImageDraw.Draw(tiles)
    for slot in range(1,5):
        x=(slot-1)*(MAP_TILE+MAP_GAP)
        on=slot==active
        draw.rounded_rectangle((x,0,x+MAP_TILE-1,MAP_TILE_H-1),radius=14,fill="#1b2a3a" if on else "#18202a",
                               outline=MAP_ACCENT if on else "#3a4b5f",width=3 if on else 2)
        draw.text((x+MAP_TILE//2,92),str(slot),font=art.font(112),anchor="mm",fill=MAP_ACCENT if on else art.WHITE)
        draw.text((x+MAP_TILE//2,166),"MAP",font=art.font(28),anchor="mm",fill=art.DIM)
        if on:
            draw.rounded_rectangle((x+MAP_TILE//2-58,186,x+MAP_TILE//2+58,214),radius=14,fill=MAP_ACCENT)
            draw.text((x+MAP_TILE//2,200),"ACTIVE",font=art.font(22),anchor="mm",fill="#0b1520")
    return tiles


CODE_ROWS = 4  # codes per page; src/engine_codes.cpp's kCodePageSize
CODES_PANEL = 56700
CODE_STATE = 56701  # then stored, pending and active, in CODE_STATE_COLORS order
CODE_STATE_COLORS = ("#5f8fbf", "#ffc34d", art.RED)


def codes_app():
    """Engine Fault Codes, laid out as upstream's: a header line, then four rows of code, state and description."""
    name="EngineCodes"
    doc, actions, models, states, page = entry(name,"Engine Fault Codes",209,102,100,images=True)
    ET.SubElement(actions,"hmiAction",id="25",target="101")
    for identifier in (23,27,29):
        ET.SubElement(actions,"raAction",id=str(identifier))
    for identifier in (80,*range(81,81+CODE_ROWS),89,90,*range(91,91+CODE_ROWS)):
        ET.SubElement(models,"raDataModel",id=str(identifier),modelType="Richtext")
    ET.SubElement(models,"imageIdModel",id="95",imageId=str(CODES_PANEL))

    options=ET.SubElement(page,"optionComponents")
    button(options,312,29,"Next page")
    button(options,313,25,"Clear engine codes...")
    content=ET.SubElement(page,"components")
    panel=ET.SubElement(content,"image",id="328",model="95")
    position(panel,36,9,1120,352)
    ET.SubElement(panel.find("properties"),"property",id="2",value="false")
    ET.SubElement(panel.find("properties"),"property",id="3",value="true")
    position(ET.SubElement(content,"label",id="320",model="80"),58,20,1067,55)
    for i in range(CODE_ROWS):
        position(ET.SubElement(content,"label",id=str(321+i),model=str(81+i)),62,83+67*i,255,55)
    read=ET.SubElement(content,"button",id="310",action="23",text="Read engine codes")
    position(read,49,370,460,52)
    for i in range(CODE_ROWS):
        position(ET.SubElement(content,"label",id=str(330+i),model=str(91+i)),326,83+67*i,801,58)
    # Each row's state mark, recoloured by src/engine_codes.cpp: CODE_STATE + none, stored, pending, active.
    for i in range(CODE_ROWS):
        ET.SubElement(models,"imageIdModel",id=str(96+i),imageId=str(CODE_STATE))
        mark=ET.SubElement(content,"image",id=str(334+i),model=str(96+i))
        position(mark,42,88+67*i,8,40)
        ET.SubElement(mark.find("properties"),"property",id="2",value="false")
        ET.SubElement(mark.find("properties"),"property",id="3",value="true")

    confirm=ET.SubElement(states,"hmiState",id="101",parentId="100",text="Clear engine codes?")
    items=ET.SubElement(confirm,"components")
    button(items,340,27,"Confirm clear engine codes")
    for identifier,model in ((350,89),(360,90),(370,80)):
        ET.SubElement(items,"label",id=str(identifier),model=str(model))
    save(doc,f"{name}.xml")

    panel=Image.new("RGBA",(1120,352))
    draw=ImageDraw.Draw(panel)
    draw.rounded_rectangle((0,0,1119,62),radius=12,fill="#222d3b")
    for i in range(CODE_ROWS):
        top=69+67*i
        draw.rounded_rectangle((0,top,1119,top+60),radius=10,fill="#181c23")
        draw.line((281,top+10,281,top+50),fill="#2c3542",width=2)
    marks=[Image.new("RGBA",(8,40))]
    for color in CODE_STATE_COLORS:
        mark=Image.new("RGBA",(8,40))
        ImageDraw.Draw(mark).rounded_rectangle((0,0,7,39),radius=3,fill=color)
        marks.append(mark)
    with ZipFile(ROOT/f"{name}_ALL_Images.zip","w",ZIP_DEFLATED) as archive:
        for identifier,image in ((CODES_PANEL,panel),*((CODE_STATE+i,mark) for i,mark in enumerate(marks))):
            data=BytesIO()
            image.save(data,format="PNG",optimize=True)
            archive.writestr(ZipInfo(f"{identifier}.png"),data.getvalue(),ZIP_DEFLATED)


if __name__=="__main__":
    shift_app()
    map_app()
    mhd_app()
    codes_app()
