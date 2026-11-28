"""Build graphite artwork for the native control menus and refresh both installer payloads."""
import argparse
from hashlib import md5, sha256
from io import BytesIO
import json
from pathlib import Path
import tarfile
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
MENU_DATA = {
    'bootmod3': ('Bootmod3Suite', 'bm3-maps', 'Bm3Maps'),
    'mgflasher': ('MgFlasher', 'mgflasher', 'MgFlasher'),
    'xdrive': ('XDriveControl', 'xdrive-control', 'XDriveControl'),
}
PANEL, EDGE, ACCENT, INK, MUTED = '#121a26', '#2f4055', '#72caff', '#f4f8ff', '#a8b8cc'


def zip_files(raw):
    with ZipFile(BytesIO(raw)) as archive:
        return {name: archive.read(name) for name in archive.namelist()}


def zip_data(files, modes=None):
    result = BytesIO()
    with ZipFile(result, 'w', ZIP_DEFLATED) as archive:
        for name, data in files.items():
            if modes is None:
                archive.writestr(name, data)
            else:
                original = modes.get(name, ZipInfo(name))
                archive.writestr(original, data, compress_type=ZIP_DEFLATED)
    return result.getvalue()


def png(image):
    result = BytesIO()
    image.save(result, format='PNG', optimize=True)
    return result.getvalue()


def geometry(node, x, y, width, height, image=False):
    properties = ET.SubElement(node, 'properties')
    for key, value in ((20, x), (21, y), (9, width), (10, height)):
        ET.SubElement(properties, 'property', id=str(key), value=str(value))
    if image:
        ET.SubElement(properties, 'property', id='2', value='false')
        ET.SubElement(properties, 'property', id='3', value='true')


def tile(width, height, font, number=None, label=None):
    image = Image.new('RGBA', (width, height))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, width-1, height-1), radius=12, fill=PANEL, outline=EDGE, width=2)
    if number is not None:
        draw.text((width//2, height//2-16), str(number), font=font, fill=INK, anchor='mm')
        draw.text((width//2, height*3//4), label, font=font.font_variant(size=23), fill=MUTED, anchor='mm')
    return image


def theme(xml, assets, app, font):
    tree = ET.fromstring(xml)
    models = tree.find('./pluginApp/models')
    images = dict(assets)

    def model(number, image):
        ET.SubElement(models, 'imageIdModel', id=str(number), imageId=str(number))
        images[f'{number}.png'] = png(image)

    native_map_tiles = ('Bootmod3Suite', 'Bm3Maps')
    if app in native_map_tiles:
        filename = models.find("imageIdModel[@id='61']").get('imageId')+'.png'
        source = Image.open(BytesIO(images[filename]))
        art = Image.new('RGBA', source.size)
        root = tree.find(".//hmiState[@id='100']/components")
        buttons = root.findall('button')
        base = root.find("image[@model='61']/properties")
        origin = int(base.find("property[@id='20']").get('value'))
        for number, button in enumerate(buttons, 1):
            props = {item.get('id'): int(item.get('value')) for item in button.findall('properties/property')}
            art.alpha_composite(tile(props['9'], props['10'], font, number, 'MAP'), (props['20']-origin, 0))
        images[filename] = png(art)
    if app == 'MgFlasher':
        for slot in range(5):
            images[f'{59100+slot}.png'] = png(tile(200, 208, font, slot, 'SLOT'))

    banner = tile(356, 64, font)
    draw = ImageDraw.Draw(banner)
    draw.rounded_rectangle((10, 10, 14, 53), radius=2, fill=ACCENT)
    logo_id = '59105.png' if app == 'MgFlasher' else '56000.png'
    if app == 'XDriveControl':
        draw.text((27, 32), 'xDrive Control', fill=INK, font=font.font_variant(size=36), anchor='lm')
    else:
        logo = Image.open(BytesIO(images[logo_id])).convert('RGBA')
        logo.thumbnail((316, 50), Image.Resampling.LANCZOS)
        banner.alpha_composite(logo, (22+(326-logo.width)//2, (64-logo.height)//2))
    model(59400, banner)
    for index, state in enumerate(tree.findall('.//hmiState')):
        components = state.find('components')
        buttons = components.findall('button')
        labels = components.findall('label')
        if any(button.find('properties') is not None for button in buttons):
            # These map grids already supply coordinates and working focus order.
            continue
        plaque = ET.Element('image', id=str(59410+index), model='59400')
        geometry(plaque, 822, 10, 356, 64, image=True)
        components.insert(0, plaque)
        if len(buttons) > 12:
            continue
        fixed = [label for label in labels if label.get('text')]
        dynamic = [label for label in labels if label.get('model')]
        for row, label in enumerate(fixed):
            geometry(label, 28, 77+row*35, 1150, 34)
        for row, label in enumerate(dynamic):
            geometry(label, 28, 388+row*32, 1150, 32)
        for row, button in enumerate(buttons):
            caption = button.get('text')
            if not caption:
                raise ValueError(f'{app} button {button.get("id")} needs a static caption')
            card = tile(548, 43, font)
            button_font = font.font_variant(size=31)
            caption_width = button_font.getlength(caption)
            if caption_width > 509:
                button_font = button_font.font_variant(size=max(18, int(31*509/caption_width)))
            ImageDraw.Draw(card).text((16, 21), caption, font=button_font, fill=INK, anchor='lm')
            identifier = 59500+index*16+row
            model(identifier, card)
            button.set('imageModel', str(identifier))
            geometry(button, 28+(row%2)*575, 83+35*len(fixed)+(row//2)*48, 548, 43)
    ET.indent(tree, space='  ')
    return ET.tostring(tree, encoding='utf-8', xml_declaration=True), zip_data(images)


def add_image_database(manifest, app):
    root = ET.fromstring(manifest)
    parameters = root.find('./config/parameters')
    if parameters.find('ImageDBFile') is None:
        ET.SubElement(parameters, 'ImageDBFile').text = '{pluginDir}/'+app+'_ALL_Images.zip'
    ET.indent(root, space='  ')
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


def native_release(font):
    for group, (app, _, _) in MENU_DATA.items():
        path = ROOT/'packages'/f'{group}.zip'
        with ZipFile(path) as archive:
            modes = {member.filename: member for member in archive.infolist()}
            files = {name: archive.read(name) for name in modes}
        xml = ROOT/'source/native/manifests'/f'{app}.xml'
        image_name = f'{app}/{app}_ALL_Images.zip'
        art = zip_files(files[image_name]) if image_name in files else {}
        files[f'{app}/{app}.xml'], files[image_name] = theme(xml.read_bytes(), art, app, font)
        manifest = f'{app}/{app}.manifest'
        files[manifest] = add_image_database(files[manifest], app)
        old_sums = [row.split('  ', 1)[1] for row in files['SHA256SUMS'].decode().splitlines()]
        files['SHA256SUMS'] = ''.join(
            f'{sha256(files[name]).hexdigest()}  {name}\n' for name in sorted(set(old_sums+[image_name]))
        ).encode()
        pending = path.with_suffix('.next')
        pending.write_bytes(zip_data(files, modes))
        pending.replace(path)


def entry_release(entry, output, font):
    original = "    else\n        stop \"iDrive's app framework differs"
    patched = "    elif [ \"${ENTRYNAV_EARLY_BETA:-0}\" = 1 ]; then\n        info \"Early beta: unverified framework pair; checking loader dependencies\"\n" + original
    output.mkdir(parents=True, exist_ok=True)
    sums = {}
    for path in sorted(entry.glob('*-armhf.tar')):
        plugin = next((app for _, ident, app in MENU_DATA.values() if path.name.startswith(ident+'-')), None)
        with tarfile.open(path) as packed:
            members = {m.name: (m, packed.extractfile(m).read() if m.isfile() else None) for m in packed}
        prefix = path.name.removesuffix('-armhf.tar')
        setup_name = prefix+'/setup.sh'
        meta, data = members[setup_name]
        setup = data.decode()
        if original not in setup:
            raise ValueError(f'{path.name} lacks the expected framework gate')
        members[setup_name] = (meta, setup.replace(original, patched, 1).encode())
        for name, (meta, data) in list(members.items()):
            if data and name != setup_name and name.endswith('/setup.sh') and original.encode() in data:
                members[name] = (meta, data.replace(original.encode(), patched.encode(), 1))
        if plugin:
            folder = f'{prefix}/opt/hmi/data/ro/Plugins/{plugin}'
            xml_name = f'{folder}/{plugin}.xml'
            image_name = f'{folder}/{plugin}_ALL_Images.zip'
            xml_meta, xml = members[xml_name]
            image_meta, raw = members.get(image_name, (tarfile.TarInfo(image_name), None))
            # The legacy upstream archives retain their own layout and appearance.
            if '-test.' in prefix:
                xml, images = theme(xml, zip_files(raw) if raw else {}, plugin, font)
                image_meta.mode = 0o644
                members[xml_name] = (xml_meta, xml)
                members[image_name] = (image_meta, images)
                manifest_name = f'{folder}/{plugin}.manifest'
                meta, manifest = members[manifest_name]
                members[manifest_name] = (meta, add_image_database(manifest, plugin))
        md5_name = prefix+'/MD5SUMS'
        meta, old = members[md5_name]
        covered = {line.split('  ', 1)[1].removeprefix('./') for line in old.decode().splitlines()}
        if plugin and '-test.' in prefix:
            covered.add(image_name.removeprefix(prefix+'/'))
        members[md5_name] = (meta, ''.join(
            f'{md5(members[prefix+"/"+name][1]).hexdigest()}  {name}\n' for name in sorted(covered)
        ).encode())
        new = output/path.name
        with tarfile.open(new, 'w') as archive:
            for meta, data in members.values():
                if data is not None:
                    meta.size = len(data)
                archive.addfile(meta, BytesIO(data) if data is not None else None)
        sums[path.name] = sha256(new.read_bytes()).hexdigest()
    (output/'SHA256.json').write_text(json.dumps(sums, indent=2)+'\n')
    (output/'profiles.json').write_bytes((entry/'profiles.json').read_bytes())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('entry_archives', type=Path)
    parser.add_argument('entry_output', type=Path)
    parser.add_argument('font', type=Path)
    args = parser.parse_args()
    font = ImageFont.truetype(str(args.font), 57)
    native_release(font)
    entry_release(args.entry_archives, args.entry_output, font)
