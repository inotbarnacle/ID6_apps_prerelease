"""Labeled host preview using production BoostDeck XML, images and model updates."""
import argparse
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile

from PIL import Image, ImageDraw, ImageFont


def render(xml, images, models, output):
    text, dynamic_images = {}, {}
    raw = models.read_bytes()
    if len(raw) > 65536:
        raise ValueError('oversized captured models')
    for line in raw.decode('latin1').splitlines():
        key, kind, value = line.split('\t', 2)
        target = text if kind == 's' else dynamic_images if kind == 'i' else None
        if target is None or key in text or key in dynamic_images or not key.isdigit():
            raise ValueError('unexpected captured model')
        target[key] = value if kind == 's' else int(value)
    root = ET.parse(xml)
    image_models = {node.get('id'): int(node.get('imageId')) for node in root.findall('.//imageIdModel')}
    image_models.update(dynamic_images)
    canvas = Image.new('RGBA', (1280, 523), '#0b1016')
    draw = ImageDraw.Draw(canvas)
    draw.text((10, 7), 'Host preview / synthetic data / not an iDrive capture',
              font=ImageFont.load_default(size=18), fill='#a8b3c2')
    with ZipFile(images) as archive:
        for node in root.findall(".//hmiState[@id='100']/components/*"):
            props = {p.get('id'): p.get('value') for p in node.findall('properties/property')}
            if not all(key in props for key in ('20', '21', '9', '10')):
                continue
            x, y, width, height = (int(props[key]) for key in ('20', '21', '9', '10'))
            y += 36
            model = node.get('model')
            if node.tag == 'image' and model in image_models:
                with Image.open(BytesIO(archive.read(str(image_models[model])+'.png'))) as asset:
                    canvas.alpha_composite(asset.convert('RGBA').resize((width, height)), (x, y))
            elif node.tag == 'label' and model in text:
                value = text[model]
                face = ImageFont.load_default(size=34 if 300 <= int(model) <= 308 else 26)
                box = draw.textbbox((0, 0), value, font=face)
                if box[2]-box[0] > width or box[3]-box[1] > height:
                    raise ValueError('captured text exceeds host label '+node.get('id'))
                draw.text((x, y), value, font=face, fill='#e2eaf0')
    canvas.convert('RGB').save(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xml', type=Path, default=Path(__file__).resolve().parents[1]/'native/manifests/BoostDeck.xml')
    parser.add_argument('--images', type=Path, required=True)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    render(args.xml, args.images, args.models, args.out)
