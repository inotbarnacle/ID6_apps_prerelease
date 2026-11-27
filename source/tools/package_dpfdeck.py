#!/usr/bin/env python3
"""Package an explicitly supplied ARM build and generated DPFDeck resources.

ELF checks establish architecture/shape, not QNX ABI or vehicle compatibility.
Use a legitimate SDK and authorized private link inputs; no SDK is downloaded.
"""
import argparse
from hashlib import sha256
import json
from io import BytesIO
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile, ZipInfo
from PIL import Image

FILES = ('libDPFDeck.so','dpfdeck-collector-qnx','DPFDeck.manifest','DPFDeck.xml','DPFDeck_ALL_Images.zip')


def checked_arm_elf(data,plugin):
    if len(data)<52 or data[:7]!=b'\x7fELF\x01\x01\x01':
        raise ValueError('expected a complete ELF32 little-endian ARM build')
    header = struct.unpack('<16sHHIIIIIHHHHHH',data[:52])
    kind,machine,version,entry,phoff = header[1:6]
    ehsize,phsize,count = header[8:11]
    if machine!=40 or version!=1 or kind not in ((3,) if plugin else (2,3)) or ehsize!=52 or phsize!=32:
        raise ValueError('unexpected ARM ELF header')
    if not 0<count<=256 or phoff<52 or phoff+count*32>len(data):
        raise ValueError('incomplete ELF program headers')
    loads = []
    for offset in range(phoff,phoff+count*32,32):
        segment = struct.unpack('<8I',data[offset:offset+32])
        if segment[0]!=1:
            continue
        _,fileoff,address,_,filesize,memsize,flags,_ = segment
        if not memsize or filesize>memsize or fileoff+filesize>len(data):
            raise ValueError('invalid ELF load segment')
        loads.append((address,filesize,flags))
    if not loads or (not plugin and not any(address<=entry<address+size and flags&1 for address,size,flags in loads)):
        raise ValueError('ELF has no valid load/entry segment')


def package(qnx_out,resources,output):
    if output.exists():
        raise ValueError('output already exists')
    files = {}
    for name in FILES:
        path = (qnx_out if name in FILES[:2] else resources)/name
        if not path.is_file() or path.is_symlink():
            raise ValueError('missing file or symlink: '+name)
        data = path.read_bytes()
        if not data:
            raise ValueError('empty input: '+name)
        files['DPFDeck/'+name] = data
    checked_arm_elf(files['DPFDeck/libDPFDeck.so'],True)
    checked_arm_elf(files['DPFDeck/dpfdeck-collector-qnx'],False)
    manifest = ET.fromstring(files['DPFDeck/DPFDeck.manifest'])
    if manifest.findtext('name')!='DPFDeck' or manifest.findtext('config/dependencies/pluginAppConnector/id')!='123':
        raise ValueError('DPFDeck manifest identity mismatch')
    layout = ET.fromstring(files['DPFDeck/DPFDeck.xml'])
    app = layout.find('pluginApp')
    if app is None or app.get('text')!='DPFDeck':
        raise ValueError('missing DPFDeck layout')
    images = {}
    for model in app.findall('models/imageIdModel'):
        key,identifier = model.get('id'),model.get('imageId','')
        if not key or key in images or not identifier.isdigit():
            raise ValueError('invalid image model')
        images[key] = identifier+'.png'
    if not images:
        raise ValueError('missing layout images')
    with ZipFile(BytesIO(files['DPFDeck/DPFDeck_ALL_Images.zip'])) as archive:
        if len(archive.namelist())!=len(set(archive.namelist())) or set(archive.namelist())!=set(images.values()):
            raise ValueError('image archive does not match the layout')
        for component in app.findall('.//components/image'):
            name = images.get(component.get('model'))
            if name is None or '/' in name or not name[:-4].isdigit():
                raise ValueError('invalid image model')
            props = {p.get('id'):p.get('value') for p in component.findall('properties/property')}
            if not all(props.get(key,'').isdigit() for key in ('9','10')):
                raise ValueError('invalid image geometry')
            with Image.open(BytesIO(archive.read(name))) as image:
                if image.format!='PNG' or image.mode!='RGBA' or image.size!=(int(props['9']),int(props['10'])):
                    raise ValueError('image geometry does not match the layout')
                image.verify()
    sums = {name:sha256(data).hexdigest() for name,data in files.items()}
    build = {'format':'idrive-dpf-source-package-v1','inputs':sums,
             'architecture_check':'ELF32 little-endian ARM; not QNX ABI or vehicle proof',
             'validation':'requires exact firmware and separately authorized parked validation'}
    files['BUILD.json'] = (json.dumps(build,indent=2)+'\n').encode()
    files['SHA256SUMS'] = ''.join(sha256(data).hexdigest()+'  '+name+'\n' for name,data in sorted(files.items())).encode()
    # Exclusive creation is retained even if the destination appears after validation.
    with output.open('xb') as stream, ZipFile(stream,'w',ZIP_DEFLATED) as archive:
        for name,data in sorted(files.items()):
            archive.writestr(ZipInfo(name),data,compress_type=ZIP_DEFLATED)


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qnx-out',type=Path,required=True)
    parser.add_argument('--resources',type=Path,default=Path(__file__).resolve().parents[1]/'native/manifests')
    parser.add_argument('--out',type=Path,required=True)
    args = parser.parse_args()
    try:
        package(args.qnx_out,args.resources,args.out)
    except (OSError,ValueError,ET.ParseError,BadZipFile) as error:
        parser.exit(1,str(error)+'\n')
