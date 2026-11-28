"""Generate the native RHMI menus and the Mac preview from one catalogue."""
import json
import re
from copy import deepcopy
from fractions import Fraction
from pathlib import Path
from xml.etree import ElementTree as ET
from features import MHD, BEAMER, UPDATES, BOOST_SETTINGS

root = Path(__file__).resolve().parents[1]
catalog = json.loads((root / 'catalog.json').read_text())
(root / 'preview/catalog.js').write_text('const catalog = ' + json.dumps(catalog) + ';\n')
output = root / 'native/generated'
output.mkdir(exist_ok=True)

def element(parent, tag, **fields):
    return ET.SubElement(parent, tag, {key: str(value) for key, value in fields.items()})

def geometry(component, position):
    properties = element(component, 'properties')
    for key, value in zip((20, 21, 9, 10), position):
        element(properties, 'property', id=key, value=value)
    return component

def surface_button(actions, components, *, id, text, action, position, selection=None):
    select = 6000 + (action if selection is None else selection)
    element(actions, 'raAction', id=select)
    return geometry(element(components, 'button', id=id, text=text, action=action, selectAction=select), position)

def app(name, title, connector, weight):
    tree = ET.Element('pluginApps')
    plugin = element(tree, 'pluginApp', applicationType='VehicleInformation',
                     applicationWeight=weight, text=title)
    entry = element(plugin, 'entryButton', id=1, action=10)
    element(element(entry, 'properties'), 'property', id=23, value=2)
    actions = element(plugin, 'actions')
    element(actions, 'hmiAction', id=10, target=100)
    models = element(plugin, 'models')
    states = element(plugin, 'hmiStates')
    page = element(states, 'hmiState', id=100, text=title)
    manifest = f'''<?xml version="1.0" encoding="UTF-8"?>
<plugin><name>{name}</name><type>App</type><file>{{pluginDir}}/{name}</file>
<config><autoStart>yes</autoStart><dependencies><pluginAppConnector><type>Etch</type>
<id>{connector}</id><asyncCallbacks>yes</asyncCallbacks></pluginAppConnector></dependencies>
<parameters><DescFile>{{pluginDir}}/{name}.xml</DescFile></parameters></config></plugin>\n'''
    (output / (name + '.manifest')).write_text(manifest)
    return tree, actions, models, states, element(page, 'components')

def write(name, tree):
    # RHMI uses one remote-ID namespace for states and visible components.
    tags = {'hmiState', 'entryButton', 'button', 'label', 'image'}
    ids = [node.get('id') for node in tree.iter() if node.tag in tags]
    if len(ids) != len(set(ids)):
        raise ValueError(f'{name} has duplicate RHMI state or component IDs')
    ET.indent(tree, space='  ')
    ET.ElementTree(tree).write(output / (name + '.xml'), encoding='utf-8', xml_declaration=True)
    for width in (800, 960):
        compact = deepcopy(tree)
        scale = Fraction(540 * width // 480 - 180, 1260)
        for prop in compact.findall('.//property'):
            if prop.get('id') in ('9', '20'):
                prop.set('value', str(round(int(prop.get('value')) * scale)))
        folder = output / 'layouts' / f'{width}x480'
        folder.mkdir(parents=True, exist_ok=True)
        ET.ElementTree(compact).write(folder / (name + '.xml'), encoding='utf-8', xml_declaration=True)

actions = ET.Element('actions')
models = ET.Element('models')
states = ET.Element('hmiStates')
for i, item in enumerate(catalog):
    target = 200 + i
    detail = element(states, 'hmiState', id=target, text=item['name'])
    components = element(detail, 'components')
    if item['id'] == 'BoostDeck':
        surface_button(actions, components, id=910, text='Settings', action=214, position=(760,18,150,43))
        element(element(detail,'optionComponents'),'button',id=911,text='BoostDeck settings',action=214)
    if item['id'] == 'WarmDeck':
        options = element(detail, 'optionComponents')
        element(options, 'button', id=1880, action=5750,
                model=1880, text='DCT temperature')
        element(actions, 'raAction', id=5752)
        element(models, 'raDataModel', id=1882, modelType='Richtext')
        element(options, 'button', id=1882, action=5752, model=1882, text='Temperature units')
    if item['id'] == 'ShiftDeck':
        element(actions, 'hmiAction', id=216, target=216)
        options = element(detail, 'optionComponents')
        element(options, 'button', id=917, text='Engine profile and targets', action=216)
        element(actions, 'raAction', id=5751)
        element(models, 'raDataModel', id=1881, modelType='Richtext')
        element(options, 'button', id=918, text='Speed units', model=1881, action=5751)
        surface_button(actions, components, id=916, text='Engine profile and targets', action=216, position=(958,8,275,48))
        settings = element(element(states, 'hmiState', id=216, parentId=target, text='ShiftDeck settings'), 'components')
        for index in range(4):
            element(models, 'raDataModel', id=1883+index, modelType='Richtext')
            element(settings, 'label', id=1890+index, model=1883+index)
        for index, (label, action) in enumerate((('Gasoline profile',5800),('Diesel profile',5801),
                ('Diesel target -100 RPM',5820),('Diesel target +100 RPM',5821),
                ('Diesel display 4,000 RPM',5810),('Diesel display 5,000 RPM',5811),
                ('Diesel display 6,000 RPM',5812))):
            element(actions, 'raAction', id=action)
            element(settings, 'button', id=3800+index, text=label, action=action)
        element(actions, 'hmiAction', id=219, target=219)
        element(settings, 'button', id=3807, text='Gasoline target', action=219)
        picker = element(element(states, 'hmiState', id=219, parentId=216, text='Gasoline target'), 'components')
        for index, rpm in enumerate(range(6000, 8001, 100)):
            action = 4700 + index
            nested = element(element(actions, 'combinedAction', id=action), 'actions')
            element(nested, 'raAction', id=5700 + index)
            element(nested, 'hmiAction', id=14700 + index, target=target)
            element(picker, 'button', id=3700 + index, text=f'{rpm:,} RPM', action=action)
    feature_buttons = {'MhdMaps': MHD, 'BEAMer': BEAMER}.get(item['id'])
    if feature_buttons:
        for title, action, x, y, width, height in feature_buttons:
            element(actions, 'raAction', id=action)
            surface_button(actions, components, id=600 + action, text=title, action=action,
                           position=(x * 2, (y - 20) * 2, width * 2, height * 2))
        if item['id'] == 'MhdMaps':
            model = 1400 + i * 20
            element(models, 'raDataModel', id=model, modelType='Richtext')
            geometry(element(components, 'label', id=540, model=model), (52, 290, 1100, 54))
    if item['id'] == 'RoadJet':
        geometry(element(components, 'label', id=544, text='Pedal climbs. Steering changes lanes.'),
                 (52, 120, 1100, 54))
    if item['id'] == 'LaunchDeck':
        element(actions, 'raAction', id=231)
        element(models, 'raDataModel', id=615, modelType='Richtext')
        surface_button(actions, components, id=521, text='Change mode', action=231, position=(810,348,395,52))
        geometry(element(components, 'label', id=522, model=615), (435, 348, 365, 52))
    if item['id'] == 'EngineCodes':
        element(actions, 'raAction', id=230)
        surface_button(actions, components, id=520, text='Next codes', action=230, position=(660,348,260,58))
        element(actions, 'hmiAction', id=232, target=217)
        surface_button(actions, components, id=523, text='Clear codes', action=232, position=(968,348,260,58))
        confirm = element(states, 'hmiState', id=217, parentId=target, text='Clear engine codes')
        controls = element(confirm, 'components')
        geometry(element(controls, 'label', id=524, text='Erases stored codes and freeze-frame data.'),
                 (52,75,1100,60))
        geometry(element(controls, 'label', id=525, text='Park with ignition on and engine off.'),
                 (52,145,1100,60))
        clear = element(element(actions, 'combinedAction', id=233, sync='true'), 'actions')
        element(clear, 'raAction', id=234)
        element(clear, 'hmiAction', id=235, target=target)
        element(actions, 'hmiAction', id=236, target=target)
        surface_button(actions, controls, id=527, text='Cancel', action=236, position=(145,253,340,70))
        surface_button(actions, controls, id=526, text='Confirm clear engine codes', action=233,
                       selection=234, position=(650,253,440,70))
        for row in range(4):
            model = 1400 + 20 * i + row
            element(models, 'raDataModel', id=model, modelType='Richtext')
            geometry(element(components, 'label', id=1020 + 40*i + row, model=model),
                     (28, 42 + row * 45, 740, 36))
    live = {
        'BoostDeck': tuple('Gauge ' + str(i+1) for i in range(9)),
        'ShiftDeck': ('Road speed', 'Engine RPM'),
        'WarmDeck': ('Engine oil', 'Coolant', 'Transmission'),
        'LaunchDeck': ('Speed', 'Timer', 'Last time', 'Previous 2', 'Previous 3'),
    }
    labels = live.get(item['id'], ())
    if labels:
        for j, label in enumerate(labels):
            x, y = 28 + (j % 3) * 403, 42 + (j // 3) * 138
            geometry(element(components, 'label', id=1000 + 40*i + j, text=label), (x, y, 365, 52))
            model = 1400 + 20*i + j
            element(models, 'raDataModel', id=model, modelType='Richtext')
            geometry(element(components, 'label', id=1020 + 40*i + j, model=model), (x, y+53, 365, 65))
    if item['id'] == 'WarmDeck':
        for j in range(3):
            model = 1400 + 20*i + 3 + j
            element(models, 'raDataModel', id=model, modelType='Richtext')
            geometry(element(components, 'label', id=1040 + 40*i + j, model=model),
                     (28 + j*403, 180, 365, 55))
        geometry(element(components, 'label', id=8000+i,
                         text='App targets are advisory; follow vehicle guidance'), (28, 265, 1160, 55))
    if item['id'] == 'ShiftDeck':
        geometry(element(components, 'label', id=1894, model=1883), (28,180,1160,45))
        geometry(element(components, 'label', id=1895, model=1884), (28,225,1160,45))
        geometry(element(components, 'label', id=1896, model=1886,
                         text='App target; follow vehicle guidance'), (28,275,1160,55))
    element(models, 'raDataModel', id=600+i, modelType='Richtext')
    geometry(element(components, 'label', id=460+i, model=600+i), (30, 365, 1110, 70))
for index, (title, target) in enumerate((('Updates', 210), ('System', 211))):
    element(actions, 'hmiAction', id=target, target=target)
    page = element(states, 'hmiState', id=target, text=title)
    if target == 211:
        page.set('parentId', '210')
    components = element(page, 'components')
    other = 211 if target == 210 else 210
    surface_button(actions, components, id=534 + index, text='System' if other == 211 else 'Updates',
                   action=other, selection=other - 200, position=(12, 170 if other == 211 else 70, 172, 82))
    if target == 210:
        for offset, (label, action, x, y, width, height) in enumerate(UPDATES):
            confirmation = 212 + offset
            element(actions, 'hmiAction', id=confirmation, target=confirmation)
            surface_button(actions, components, id=600 + action, text=label, action=confirmation,
                           selection=action, position=(x * 2, (y - 20) * 2, width * 2, height * 2))
            detail = element(states, 'hmiState', id=confirmation, parentId=210, text=label)
            controls = element(detail, 'components')
            element(actions, 'raAction', id=action)
            geometry(element(controls, 'button', id=875 + offset, action=action, text='Confirm'),
                     (242, 208, 450, 68))
            element(models, 'raDataModel', id=612 + offset, modelType='Richtext')
            geometry(element(controls, 'label', id=560 + offset, model=612 + offset),
                     (242, 312, 960, 78))
        element(models, 'raDataModel', id=610, modelType='Richtext')
        geometry(element(components, 'label', id=552, model=610), (236, 300, 940, 82))
    else:
        geometry(element(components, 'label', id=553, text='Vehicle data starts when its app is open.'),
                 (236, 105, 940, 82))
element(actions,'hmiAction',id=214,target=214)
element(actions,'hmiAction',id=215,target=215)
prefs=element(element(states,'hmiState',id=214,parentId=200,text='BoostDeck settings'),'components')
layout=element(element(states,'hmiState',id=215,parentId=214,text='Configure gauges'),'components')
element(prefs,'label',id=923,model=1899)
element(prefs,'button',id=920,text='Configure gauges',action=215)
element(models,'raDataModel',id=1899,modelType='Richtext')
element(actions, 'raAction', id=5750)
element(models, 'raDataModel', id=1880, modelType='Richtext')
element(prefs, 'button', id=1880, action=5750, model=1880, text='DCT temperature')

def return_action(identifier, remote, target):
    group=element(element(actions,'combinedAction',id=identifier),'actions')
    element(group,'raAction',id=remote)
    element(group,'hmiAction',id=identifier+10000,target=target)

for row,(label,choices) in enumerate(BOOST_SETTINGS):
    state=2160+row
    element(actions,'hmiAction',id=state,target=state)
    model=1800+row
    element(models,'raDataModel',id=model,modelType='Richtext')
    element(prefs,'button',id=930+row,action=state,text='',model=model)
    picker=element(element(states,'hmiState',id=state,parentId=214,text=label),'components')
    for choice,text in enumerate(choices):
        offset=row*16+choice
        return_action(4000+offset,5000+offset,214)
        element(picker,'button',id=3500+offset,action=4000+offset,text=text)

signals = []
for line in (root/'native/telemetry/common/boost_signals.def').read_text().splitlines():
    match=re.match(r'SIGNAL\(([A-Z_]+),\s*"([^"]+)"',line)
    if match:
        signals.append(match.groups())

# A choice returns to the assignment screen, which displays the saved name.
for index,(_,title) in enumerate(signals):
    return_action(4500+index,5500+index,215)
picker=element(element(states,'hmiState',id=2180,parentId=215,text='Choose a gauge'),'components')
element(models,'raDataModel',id=1898,modelType='Richtext')
element(picker,'label',id=2999,model=1898)
for index,(_,title) in enumerate(signals):
    element(picker,'button',id=3000+index,action=4500+index,text=title)
for slot in range(9):
    model=1820+slot
    return_action(4580+slot,5580+slot,2180)
    element(models,'raDataModel',id=model,modelType='Richtext')
    element(layout,'button',id=950+slot,action=4580+slot,text='',model=model)
settings_file = output / 'boost_settings_assets.h'
with settings_file.open('w') as data:
    data.write('/* Generated by generate.py. */\n')
    data.write('static const char *const boost_option_names[] = {')
    data.write(', '.join(json.dumps(label) for label, _ in BOOST_SETTINGS))
    data.write('};\n')
    data.write('static const char *const boost_option_values[][10] = {\n')
    for _, choices in BOOST_SETTINGS:
        data.write('    {' + ', '.join(json.dumps(choice, ensure_ascii=False) for choice in choices) + '},\n')
    data.write('};\n')
def write_individual(name, title, connector, weight, entry, included_states):
    individual, app_actions, app_models, app_states, _ = app(name, title, connector, weight)
    app_actions[0].set('target', str(entry))
    app_states.clear()
    for state in states:
        if int(state.get('id')) in included_states:
            app_states.append(deepcopy(state))

    action_ids = {node.get(attribute) for node in app_states.iter()
                  for attribute in ('action', 'selectAction') if node.get(attribute)}
    model_ids = {node.get('model') for node in app_states.iter() if node.get('model')}
    for action in actions:
        if action.get('id') in action_ids:
            app_actions.append(deepcopy(action))
    for model in models:
        if model.get('id') in model_ids:
            app_models.append(deepcopy(model))
    write(name, individual)


page_names = ('boost', 'shift', 'mhd', 'codes', 'road', 'beamer', 'launch', 'warm')
with (output / 'modules.txt').open('w') as modules:
    modules.write('ID6LibraryLab surface_updates\n')
    write_individual('ID6LibraryLab', 'ID6 App Updates', 229, 244, 210, {210, 211, 212, 213})
    for i, item in enumerate(catalog):
        name = 'ID6Full' + item['id']
        connector = 231 + i
        entry = 200 + i
        selected = {entry}
        if item['id'] == 'BoostDeck':
            selected.update((214, 215, 2180, *range(2160, 2170)))
        if item['id'] == 'ShiftDeck':
            selected.update((216,219))
        if item['id'] == 'EngineCodes':
            selected.add(217)
        modules.write(f'{name} surface_{page_names[i]}\n')
        write_individual(name, item['name'], connector, 245 + i, entry, selected)

with (output / 'catalog.h').open('w') as header:
    header.write('static const char* const kCatalog[] = {\n')
    for item in catalog:
        plugin = 'ID6AirPlayLab' if item['id'] == 'AirPlayLab' else item['id']
        header.write(f'    "{plugin}",\n')
    header.write('};\n')
