"""Render a labeled host preview from production XML and captured model updates."""
import argparse
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile

from PIL import Image, ImageDraw, ImageFont


def font(size):
    for name in ('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
                 '/usr/share/fonts/TTF/DejaVuSans.ttf'):
        if Path(name).is_file():
            return ImageFont.truetype(name,size)
    return ImageFont.load_default(size=size)


def render(resources,models,output):
    values = {}
    for line in models.read_text(encoding='latin1').splitlines():
        key,value=line.split('\t',1)
        if key not in set(map(str,range(50,62))) or key in values:
            raise ValueError('unexpected captured model')
        values[key]=value
    root = ET.parse(resources/'DPFDeck.xml')
    image_models = {model.get('id'):model.get('imageId') for model in root.findall('.//imageIdModel')}
    canvas = Image.new('RGBA',(1280,523),'#0b1016')
    draw = ImageDraw.Draw(canvas)
    draw.text((10,7),'Host preview / synthetic data / not an iDrive capture',font=font(18),fill='#a8b3c2')
    with ZipFile(resources/'DPFDeck_ALL_Images.zip') as archive:
        for component in root.findall(".//hmiState[@id='100']/components/*"):
            props = {p.get('id'):p.get('value') for p in component.findall('properties/property')}
            x,y,w,h=(int(props[key]) for key in ('20','21','9','10'))
            x+=10;y+=36
            if component.tag=='image':
                with Image.open(BytesIO(archive.read(image_models[component.get('model')]+'.png'))) as image:
                    canvas.alpha_composite(image.convert('RGBA'),(x,y))
            elif component.tag=='label':
                value=component.get('text',values.get(component.get('model'),''))
                size=34 if component.get('model') in set(map(str,range(50,56))) else 26
                face=font(size)
                box=draw.textbbox((0,0),value,font=face)
                if box[2]-box[0]>w or box[3]-box[1]>h:
                    raise ValueError('host preview text exceeds label '+component.get('id'))
                draw.text((x,y),value,font=face,fill='#e2eaf0')
    canvas.convert('RGB').save(output)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources',type=Path,default=Path(__file__).resolve().parents[1]/'native/manifests')
    parser.add_argument('--models',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    render(args.resources,args.models,args.out)
