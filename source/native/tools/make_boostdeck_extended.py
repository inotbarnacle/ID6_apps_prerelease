"""Lay out BoostDeck's extended tiles (oil temperature, battery voltage) in a 5 x 2 grid.

Rewrites plugin/BoostDeck.xml in place: the eight existing tiles narrow from four to
five columns and two new tiles are added in the fifth column. With --images, the
background frame (image 56300) inside an existing BoostDeck_ALL_Images.zip is
redrawn to match; every other image in the archive is copied unchanged, and the
Active E column on the right is copied pixel for pixel from the old background.

    python3 tools/make_boostdeck_extended.py
    python3 tools/make_boostdeck_extended.py --images plugin/BoostDeck_ALL_Images.zip

The label font defaults to Bahnschrift (Windows) or DejaVu Sans; pass --font to use
the font of the original artwork.
"""
import argparse
import os
from io import BytesIO
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[1]
XML = ROOT / 'plugin/BoostDeck.xml'

# Screen placement of the background image (component 209, model 69).
BG_X, BG_Y, BG_W, BG_H = 24, 13, 1172, 400
BACKGROUND_IMAGE = 56300
BAR_EMPTY = 55000
# Measured from the published background: 240 x 192 tiles, 18 px column gap,
# 16 px row gap, 12 px corner radius, Active E column from x = 1032.
TILE_W, TILE_H, COL_GAP, ROW_GAP, RADIUS = 193, 192, 12, 16, 12
ETHANOL_X = 1032
TILE_FILL = (23, 27, 34, 255)
TEXT_FILL = (186, 200, 216, 255)
TEXT_LEFT, CAP_TOP, CAP_HEIGHT = 17, 25, 19

# (caption, label component, label model, bar component, bar model), column by column.
TILES = (
    ('BOOST', 150, 50, 200, 60), ('AFR', 151, 51, 201, 61),
    ('TIMING', 152, 52, 202, 62), ('INTAKE AIR', 153, 53, 203, 63),
    ('TRANSMISSION', 154, 54, 204, 64), ('RAIL PRESSURE', 155, 55, 205, 65),
    ('OIL PRESSURE', 156, 56, 206, 66), ('COOLANT', 157, 57, 207, 67),
    ('OIL TEMP', 160, 70, 210, 72), ('BATTERY', 161, 71, 211, 73),
)
NEW_TEXT_MODELS, NEW_IMAGE_MODELS = (70, 71), (72, 73)
assert TILE_W * 5 + COL_GAP * 4 <= ETHANOL_X - COL_GAP


def tile_origin(k):
    """Top-left corner of tile k inside the background image."""
    return (k // 2) * (TILE_W + COL_GAP), (k % 2) * (TILE_H + ROW_GAP)


def set_props(component, x, y, w, h, image=False):
    for old in component.findall('properties'):
        component.remove(old)
    props = ET.SubElement(component, 'properties')
    for prop, value in [(20, x), (21, y), (9, w), (10, h)] + ([(2, 'false'), (3, 'true')] if image else []):
        ET.SubElement(props, 'property', id=str(prop), value=str(value))


def update_xml(path=XML):
    tree = ET.parse(path)
    app = tree.getroot().find('pluginApp')
    models = app.find('models')
    have = {m.get('id') for m in models}
    for identifier in NEW_TEXT_MODELS:
        if str(identifier) not in have:
            ET.SubElement(models, 'raDataModel', id=str(identifier), modelType='Richtext')
    for identifier in NEW_IMAGE_MODELS:
        if str(identifier) not in have:
            ET.SubElement(models, 'imageIdModel', id=str(identifier), imageId=str(BAR_EMPTY))
    gauges = next(s for s in app.find('hmiStates') if s.get('id') == '100')
    components = gauges.find('components')
    by_id = {c.get('id'): c for c in components}
    ethanol_bar = by_id['208']
    for k, (_, label_id, label_model, bar_id, bar_model) in enumerate(TILES):
        bar = by_id.get(str(bar_id))
        label = by_id.get(str(label_id))
        if bar is None:
            bar = ET.Element('image', id=str(bar_id), model=str(bar_model))
            components.insert(list(components).index(ethanol_bar), bar)
        if label is None:
            label = ET.Element('label', id=str(label_id), model=str(label_model))
            components.insert(list(components).index(ethanol_bar), label)
        x, y = tile_origin(k)
        set_props(bar, BG_X + x + 15, BG_Y + y + 140, TILE_W - 30, 15, True)
        set_props(label, BG_X + x + 15, BG_Y + y + 60, TILE_W - 24, 66)
    ET.indent(tree.getroot(), space='  ')
    # Bytes, so the file keeps LF line endings on every platform.
    Path(path).write_bytes(ET.tostring(tree.getroot(), encoding='utf-8', xml_declaration=True))


def find_font(requested):
    candidates = [requested] if requested else [
        r'C:\Windows\Fonts\bahnschrift.ttf',
        '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
        '/Library/Fonts/Arial Bold.ttf',
    ]
    for candidate in candidates:
        if candidate and os.path.exists(candidate):
            return candidate
    raise SystemExit('No label font found; pass --font PATH')


def caption_font(path):
    from PIL import ImageFont
    size = CAP_HEIGHT
    while True:
        font = ImageFont.truetype(path, size)
        try:  # variable fonts such as Bahnschrift: narrower tiles need the semi-condensed cut
            font.set_variation_by_name('SemiBold SemiCondensed')
        except (OSError, ValueError):
            pass
        top, bottom = font.getbbox('B', anchor='ls')[1], font.getbbox('B', anchor='ls')[3]
        if bottom - top >= CAP_HEIGHT:
            return font
        size += 1


def draw_background(base_png, font_path):
    from PIL import Image, ImageDraw
    old = Image.open(BytesIO(base_png)).convert('RGBA')
    if old.size != (BG_W, BG_H):
        raise SystemExit(f'Unexpected background size {old.size}')
    image = Image.new('RGBA', (BG_W, BG_H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    font = caption_font(font_path)
    for k, (caption, *_rest) in enumerate(TILES):
        x, y = tile_origin(k)
        draw.rounded_rectangle((x, y, x + TILE_W - 1, y + TILE_H - 1), radius=RADIUS, fill=TILE_FILL)
        width = draw.textlength(caption, font=font)
        if TEXT_LEFT + width > TILE_W - 8:
            raise SystemExit(f'Caption {caption!r} is {width:.0f} px wide and does not fit a {TILE_W} px tile')
        draw.text((x + TEXT_LEFT, y + CAP_TOP + CAP_HEIGHT), caption, font=font, fill=TEXT_FILL, anchor='ls')
    image.paste(old.crop((ETHANOL_X, 0, BG_W, BG_H)), (ETHANOL_X, 0))
    data = BytesIO()
    image.save(data, format='PNG', optimize=True)
    return data.getvalue()


def update_images(path, output, font_path):
    with ZipFile(path) as source:
        entries = [(info, source.read(info)) for info in source.infolist()]
    name = f'{BACKGROUND_IMAGE}.png'
    if not any(info.filename == name for info, _ in entries):
        raise SystemExit(f'{path} has no {name}')
    temporary = Path(str(output) + '.tmp')
    with ZipFile(temporary, 'w', compression=ZIP_DEFLATED) as target:
        for info, data in entries:
            if info.filename == name:
                data = draw_background(data, font_path)
            target.writestr(ZipInfo(info.filename, info.date_time), data, ZIP_DEFLATED)
    os.replace(temporary, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--images', type=Path, help='BoostDeck_ALL_Images.zip whose background is redrawn')
    parser.add_argument('--images-out', type=Path, help='write the archive here instead of in place')
    parser.add_argument('--font', help='TrueType font for the tile captions')
    parser.add_argument('--skip-xml', action='store_true', help='leave plugin/BoostDeck.xml unchanged')
    args = parser.parse_args()
    if not args.skip_xml:
        update_xml()
    if args.images:
        update_images(args.images, args.images_out or args.images, find_font(args.font))


if __name__ == '__main__':
    main()
