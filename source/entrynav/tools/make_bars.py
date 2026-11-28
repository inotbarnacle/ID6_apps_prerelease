"""Generate shared, compact bar frames and the BoostDeck RHMI screens."""
from io import BytesIO
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
BARS = (
    ('Boost', 'psi', 0, 35), ('AFR', '', 9, 19), ('Timing', 'deg', -20, 50),
    ('Intake air', r'\260F', -20, 100), ('Transmission', r'\260F', 0, 150),
    ('Rail pressure', 'psi', 0, 250), ('Oil pressure', 'psi', 0, 8), ('Coolant', r'\260F', 0, 130),
)
COLORS = (
    ('BMW Orange', '#ff4f1a'), ('Blue', '#58ceff'), ('Green', '#6cdb93'), ('White', '#e2eaf0'),
    ('Purple', '#ad72ff'), ('Pink', '#ff6cba'), ('Cyan', '#20f1e6'), ('Red', '#ff4545'),
)
MULTICOLOR = ('#58ceff', '#5de0bf', '#cbacff', '#ffbe70',
              '#6cdb93', '#60c5ff', '#ffb76a', '#6fd9ba')
THEMES = tuple(name for name, _ in COLORS) + ('Multicolor',)
# The Temperature screen's choices, in the order of their actions (70, 71). The readings are kept in Celsius.
TEMPERATURE_UNITS = (('Celsius', r'\260C'), ('Fahrenheit', r'\260F'))
STEPS, FIRST = 32, 55000

# The gauge page is laid out like an iDrive list: two columns of four rows, the engine readings on the left and the
# temperatures and oil on the right. Each row has the name and the value as labels, so the head unit draws both in its
# own font, and a thin bar along the bottom that is also the row's divider. The picture behind (DIVIDER) is only the
# line between the columns. ROW_ORDER lists BARS indices by position, down the left column, then the right one.
DIVIDER = 55590
ROW_ORDER = (0, 1, 2, 5, 3, 4, 6, 7)
GRID_X, GRID_Y, GRID_W, GRID_H = 24, 13, 1172, 400
COLUMN_GAP, ROW_H = 60, 100
COLUMN_W = (GRID_W - COLUMN_GAP) // 2
NAME_X, VALUE_X, LABEL_Y, LABEL_H = 6, 270, 12, 66
BAR_W, BAR_H, BAR_Y = COLUMN_W, 4, 88
# The bar's track is the divider: a light line that fades out over its last third, like iDrive's list dividers.
TRACK, TRACK_END, LINE_ALPHA = (255, 255, 255, 56), (255, 255, 255, 8), 52

def row_origin(position):
    """The top left corner of the row at a position, in screen coordinates."""
    return GRID_X + (position // 4)*(COLUMN_W + COLUMN_GAP), GRID_Y + (position % 4)*ROW_H

def divider_picture():
    """The line between the columns, faded out at both ends."""
    image = Image.new('RGBA', (2, GRID_H))
    for y in range(GRID_H):
        fade = min(1, y / 80, (GRID_H - 1 - y) / 80)
        image.putpixel((0, y), (255, 255, 255, round(LINE_ALPHA*fade)))
        image.putpixel((1, y), (255, 255, 255, round(LINE_ALPHA*fade)))
    return image

def bar_picture(fill, step):
    image = Image.new('RGBA', (BAR_W, BAR_H))
    draw = ImageDraw.Draw(image)
    fade_from = BAR_W*2 // 3
    for x in range(BAR_W):
        t = max(0, (x - fade_from) / (BAR_W - 1 - fade_from))
        color = tuple(round(a + (b - a)*t) for a, b in zip(TRACK, TRACK_END))
        draw.line((x, 0, x, BAR_H-1), fill=color)
    if step:
        draw.rectangle((0, 0, round(BAR_W*step/STEPS)-1, BAR_H-1), fill=fill)
    return image

def add_props(component, x, y, w, h, image=False):
    props = ET.SubElement(component, 'properties')
    for prop, value in [(20,x), (21,y), (9,w), (10,h)] + ([(2,'false'),(3,'true')] if image else []):
        ET.SubElement(props, 'property', id=str(prop), value=str(value))

def label(parent, identifier, model):
    return ET.SubElement(parent, 'label', id=str(identifier), model=str(model))

def button(parent, identifier, action, text):
    ET.SubElement(parent,'button',id=str(identifier),action=str(action),text=text)

def build_assets():
    def store(archive, identifier, image):
        data = BytesIO()
        image.save(data,format='PNG',optimize=True)
        archive.writestr(ZipInfo(f'{identifier}.png'),data.getvalue(),ZIP_DEFLATED)

    with ZipFile(ROOT/'plugin/BoostDeck_ALL_Images.zip','w', compression=ZIP_DEFLATED) as archive:
        for color, fill in enumerate(tuple(fill for _, fill in COLORS) + MULTICOLOR):
            for step in range(STEPS+1):
                if color and not step:
                    continue
                store(archive, FIRST+color*STEPS+step, bar_picture(fill, step))
        store(archive, DIVIDER, divider_picture())

def main():
    root = ET.Element('pluginApps')
    app = ET.SubElement(root,'pluginApp',applicationType='VehicleInformation',applicationWeight='210',text='BoostDeck')
    entry = ET.SubElement(app,'entryButton',id='1',action='10')
    ET.SubElement(ET.SubElement(entry,'properties'),'property',id='23',value='2')
    actions = ET.SubElement(app,'actions')
    for identifier, target in ((10,100),(31,104),(32,105)):
        ET.SubElement(actions,'hmiAction',id=str(identifier),target=str(target))
    for identifier in range(40,40+len(THEMES)):
        ET.SubElement(actions,'raAction',id=str(identifier))
    for identifier in range(70,70+len(TEMPERATURE_UNITS)):
        ET.SubElement(actions,'raAction',id=str(identifier))
    models = ET.SubElement(app,'models')
    states = ET.SubElement(app,'hmiStates')
    gauges = ET.SubElement(states,'hmiState',id='100',text='BoostDeck')
    options = ET.SubElement(gauges,'optionComponents')
    button(options,33,31,'Gauge color')
    button(options,34,32,'Temperature')
    content = ET.SubElement(gauges,'components')
    ET.SubElement(models,'imageIdModel',id='69',imageId=str(DIVIDER))
    add_props(ET.SubElement(content,'image',id='209',model='69'),GRID_X+COLUMN_W+COLUMN_GAP//2-1,GRID_Y,2,GRID_H,True)
    # Each name is a label the app fills once from kDialScales (models 120 on), so the head unit draws it.
    for i in range(len(BARS)):
        ET.SubElement(models,'raDataModel',id=str(50+i),modelType='Richtext')
        ET.SubElement(models,'imageIdModel',id=str(60+i),imageId=str(FIRST))
        ET.SubElement(models,'raDataModel',id=str(120+i),modelType='Richtext')
        bar = ET.SubElement(content,'image',id=str(200+i),model=str(60+i))
        name = label(content,160+i,120+i)
        reading = label(content,150+i,50+i)
        x,y = row_origin(ROW_ORDER.index(i))
        add_props(name,x+NAME_X,y+LABEL_Y,VALUE_X-NAME_X-10,LABEL_H)
        add_props(reading,x+VALUE_X,y+LABEL_Y,COLUMN_W-VALUE_X,LABEL_H)
        add_props(bar,x,y+BAR_Y,BAR_W,BAR_H,True)
    ET.SubElement(models,'raDataModel',id='110',modelType='Richtext')
    color_screen = ET.SubElement(states,'hmiState',id='104',parentId='100',text='Gauge color')
    content = ET.SubElement(color_screen,'components')
    label(content,389,110)
    for i,name in enumerate(THEMES):
        button(content,390+i,40+i,name)
    ET.SubElement(models,'raDataModel',id='111',modelType='Richtext')
    unit_screen = ET.SubElement(states,'hmiState',id='105',parentId='100',text='Temperature')
    content = ET.SubElement(unit_screen,'components')
    label(content,409,111)
    for i,(name,_) in enumerate(TEMPERATURE_UNITS):
        button(content,410+i,70+i,name)
    ET.indent(root,space='  ')
    # LF on every system: tools/package_release.py refuses Windows line endings.
    with open(ROOT/'plugin/BoostDeck.xml','w',encoding='utf-8',newline='\n') as out:
        ET.ElementTree(root).write(out,encoding='unicode',xml_declaration=True)
    scales = [f'    {{{lo}, {hi}, "{name}", "{unit}"}}' for name,unit,lo,hi in BARS]
    colors = ', '.join(f'"{name}"' for name in THEMES)
    units = ', '.join(f'{{"{name}", "{unit}"}}' for name,unit in TEMPERATURE_UNITS)
    (ROOT/'src/dial_scales.h').write_text(
        '// Generated by tools/make_bars.py.\n'
        f'static const unsigned kDialSteps = {STEPS};\n'
        f'static const unsigned kBarFirst = {FIRST};\n'
        f'static const unsigned kMulticolor = {len(COLORS)};\n'
        f'static const char *const kColorNames[] = {{{colors}}};\n'
        'static const struct TemperatureUnit { const char *name, *unit; } kTemperatureUnits[] = '
        f'{{{units}}};\n'
        'static const struct DialScale { double minimum, maximum; const char *name, *unit; } kDialScales[] = {\n'
        + ',\n'.join(scales) + '\n};\n', newline='\n')
    build_assets()

if __name__ == '__main__':
    main()
