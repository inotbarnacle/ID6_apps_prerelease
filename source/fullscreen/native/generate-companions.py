"""Build independent full-screen control apps from their native RHMI action maps."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import re
from xml.etree import ElementTree as ET

from PIL import Image

native = Path(__file__).resolve().parent
generated = native / 'generated'
apps = [
    ('Bootmod3Suite', 40, 239, 243),
    ('MgFlasher', 41, 240, 242),
    ('XDriveControl', 42, 241, 241),
    ('HybridDeck', 43, 242, 240),
]


def item(parent, tag, **attrs):
    return ET.SubElement(parent, tag, {name:str(value) for name, value in attrs.items()})


def position(button, x, y, width, height):
    properties = item(button,'properties')
    for key, value in zip((20,21,9,10), (x,y,width,height)):
        item(properties,'property',id=key,value=value)


def caption(value):
    return value.replace('–','-').replace('—','-')


mg_root_captions = {
    'Refresh current settings': 'Refresh settings',
    'DME profile for Custom Code 7.3-7.3.4': 'DME profile',
    'Ethanol override value': 'Ethanol value',
    'Radiator flap (Gen 2)': 'Radiator flap',
}


def generate(source):
    rows = []
    buttons = []
    definitions = []
    for name, kind, connector, weight in apps:
        full = 'ID6Full' + name
        tree = ET.parse(source/'manifests'/f'{name}.xml')
        root = tree.find('pluginApp')
        root.set('applicationWeight',str(weight))
        actions = root.find('actions')
        for state in root.findall('hmiStates/hmiState'):
            state_id = int(state.get('id'))
            components = state.find('components')
            options = state.find('optionComponents')
            if options is not None:
                for button in list(options.findall('button')):
                    options.remove(button)
                    components.append(button)
                state.remove(options)
            for image in components.findall('image'):
                components.remove(image)
            if name == 'MgFlasher' and state_id == 100:
                map_buttons = root.findall("hmiStates/hmiState[@id='101']/components/button")
                navigation = components.find("button[@action='11']")
                components.remove(navigation)
                for offset, original in enumerate(map_buttons):
                    button = deepcopy(original)
                    button.set('id',str(3990+offset))
                    components.insert(offset,button)
            controls = components.findall('button')
            maps = (4 if name == 'Bootmod3Suite' and state_id == 100 else
                    5 if name == 'MgFlasher' and state_id in (100,101) else 0)
            title = state.get('text',name)
            if name == 'XDriveControl' and state_id == 100:
                title = 'xDrive Control'
            hint = next((label.get('text') for label in components.findall('label') if label.get('text')), '')
            models = [int(label.get('model')) for label in components.findall('label') if label.get('model')]
            status_model = models[0] if models else 0
            if name == 'MgFlasher' and state_id == 190:
                hint = 'Select the profile for MG Custom Code 7.3-7.3.4.'
            if name == 'HybridDeck':
                status_model = 58
                hint = ''
            start = len(buttons)
            for offset, button in enumerate(controls):
                action = int(button.get('action'))
                text = button.get('text')
                if not text and name == 'Bootmod3Suite' and 21 <= action <= 24:
                    text=f'Map {action-20}'
                if not text and name == 'MgFlasher' and 21 <= action <= 25:
                    text=f'Map slot {action-21}'
                if not text:
                    raise ValueError(f'{name} page {state_id}: action {action} needs a caption')
                remote_id = 8000+len(buttons)
                display_text = caption(text)
                if name == 'MgFlasher' and state_id == 100:
                    display_text = mg_root_captions.get(display_text, display_text)
                buttons.append((remote_id, display_text))
                item(actions,'raAction',id=remote_id)
                button.set('text',text)
                button.set('selectAction',str(remote_id))
                button.attrib.pop('imageModel',None)
                properties = button.find('properties')
                if properties is not None:
                    button.remove(properties)
                if offset < maps:
                    width = (590-10*(maps-1))//maps
                    position(button,2*(26+offset*(width+10)),50,2*width,138)
                elif maps:
                    columns = 3 if name == 'Bootmod3Suite' else 4
                    width = (590-10*(columns-1))//columns
                    index = offset-maps
                    position(button,2*(26+(index%columns)*(width+10)),206+50*(index//columns),2*width,44)
                elif len(controls) <= 12:
                    position(button,62+565*(offset%2),100+48*(offset//2),545,43)
            rows.append((kind,state_id,status_model,start,len(controls),maps,caption(title),caption(hint)))
        manifest = (source/'manifests'/f'{name}.manifest').read_text()
        manifest = manifest.replace(f'<name>{name}</name>',f'<name>{full}</name>')
        manifest = manifest.replace(f'/'+name, f'/'+full)
        manifest = re.sub(r'<id>\d+</id>',f'<id>{connector}</id>',manifest,count=1)
        manifest = re.sub(r'<ImageDBFile>[^<]*</ImageDBFile>','',manifest)
        (generated/f'{full}.manifest').write_text(manifest)
        ET.indent(tree,space='  ')
        tree.write(generated/f'{full}.xml',encoding='utf-8',xml_declaration=True)
        definitions.append(f'{full} {name} {kind}\n')
    (generated/'companions.txt').write_text(''.join(definitions))
    with (generated/'companions.h').open('w') as out:
        out.write('/* Generated by generate-companions.py. */\n')
        out.write('struct companion_button { unsigned select; const char *label; };\n')
        out.write('struct companion_page { unsigned kind, id, status_model, first, count, maps; const char *title, *hint; };\n')
        for name, items in [('companion_buttons',buttons),('companion_pages',rows)]:
            struct_name='companion_button' if name=='companion_buttons' else 'companion_page'
            out.write(f'static const struct {struct_name} {name}[] = {{\n')
            for values in items:
                out.write('    {'+', '.join(json.dumps(value,ensure_ascii=False) if isinstance(value,str) else str(value) for value in values)+'},\n')
            out.write('};\n')
        out.write('''static inline const struct companion_page *companion_lookup(unsigned kind,unsigned id) {
    for (unsigned i=0;i<sizeof companion_pages/sizeof companion_pages[0];++i)
        if (companion_pages[i].kind==kind && companion_pages[i].id==id) return &companion_pages[i];
    return 0;
}
''')
    with (generated/'companion_logos.h').open('w') as out:
        for name in ('mg-flasher','bootmod3'):
            with Image.open(native/'images'/f'{name}-wordmark.png') as source:
                rgba=source.convert('RGBA')
                rgba.thumbnail((268,44),Image.Resampling.LANCZOS)
            symbol=name.replace('-','_')
            out.write(f'enum {{ {symbol}_width={rgba.width}, {symbol}_height={rgba.height} }};\n')
            out.write(f'static const unsigned char {symbol}_rgba[] = {{\n')
            data=rgba.tobytes()
            for i in range(0,len(data),32):
                out.write('    '+', '.join(map(str,data[i:i+32]))+',\n')
            out.write('};\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('standard_native',type=Path)
    generate(parser.parse_args().standard_native)
