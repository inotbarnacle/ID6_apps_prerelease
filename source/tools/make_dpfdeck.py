"""Generate DPFDeck's native wide-screen layout and original background artwork."""
import argparse
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageDraw
from make_bars import add_props

OUTPUT = Path(__file__).resolve().parents[1] / 'native/manifests'
CONNECTOR = 123
BACKGROUND = 65100
TITLES = ('Delta pressure', 'Inlet pressure', 'Outlet pressure',
          'Inlet temperature', 'Outlet temperature', 'Coolant')


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    root = ET.Element('pluginApps')
    app = ET.SubElement(root, 'pluginApp', applicationType='VehicleInformation',
                        applicationWeight='221', text='DPFDeck')
    entry = ET.SubElement(app, 'entryButton', id='1', action='10')
    ET.SubElement(ET.SubElement(entry, 'properties'), 'property', id='23', value='2')
    actions = ET.SubElement(app, 'actions')
    ET.SubElement(actions, 'hmiAction', id='10', target='100')
    ET.SubElement(actions, 'hmiAction', id='20', target='101')
    for callback in (420,421,430,431):
        combined = ET.SubElement(ET.SubElement(actions, 'combinedAction', id=str(1000+callback)), 'actions')
        ET.SubElement(combined, 'raAction', id=str(callback))
        ET.SubElement(combined, 'hmiAction', id=str(2000+callback), target='100')
    models = ET.SubElement(app, 'models')
    for model in range(50,60):
        ET.SubElement(models, 'raDataModel', id=str(model), modelType='Richtext')
    ET.SubElement(models, 'imageIdModel', id='70', imageId=str(BACKGROUND))
    states = ET.SubElement(app, 'hmiStates')
    screen = ET.SubElement(states, 'hmiState', id='100', text='DPFDeck')
    options = ET.SubElement(screen, 'optionComponents')
    ET.SubElement(options, 'button', id='30', action='20', text='DPFDeck settings')
    components = ET.SubElement(screen, 'components')

    def placed(tag, identifier, model, bounds, text=None):
        component = ET.SubElement(components, tag, id=str(identifier))
        if model is not None:
            component.set('model', str(model))
        if text is not None:
            component.set('text', text)
        add_props(component, *bounds, image=tag=='image')

    placed('image',200,70,(32,12,1160,444))
    placed('label',201,57,(54,18,240,40))
    placed('label',202,None,(430,18,738,40),'Regeneration: Unknown')
    for index,title in enumerate(TITLES):
        x,y = 54+(index%3)*386, 76+(index//3)*132
        placed('label',210+index,None,(x,y,342,45),title)
        placed('label',220+index,50+index,(x,y+47,342,60))
    placed('label',230,56,(54,323,1116,42))
    placed('label',231,None,(54,367,1116,42),'Soot, ash and last regeneration: unavailable')
    placed('label',232,None,(54,410,1116,42),'Inlet/outlet pressure reference: unknown')
    settings = ET.SubElement(states, 'hmiState', id='101', parentId='100', text='DPFDeck settings')
    content = ET.SubElement(settings, 'components')
    ET.SubElement(content, 'label', id='300', model='58')
    ET.SubElement(content, 'label', id='301', model='59')
    for index,(callback,title) in enumerate(((420,'Show bank 1'),(421,'Show bank 2'),
                                            (430,'Metric: mbar / C'),(431,'Imperial: psi / F'))):
        ET.SubElement(content, 'button', id=str(310+index), action=str(1000+callback), text=title)
    ET.indent(root, space='  ')
    ET.ElementTree(root).write(output/'DPFDeck.xml', encoding='utf-8', xml_declaration=True)

    manifest = ET.Element('plugin')
    for name,value in (('name','DPFDeck'),('type','App'),('file','{pluginDir}/DPFDeck')):
        ET.SubElement(manifest,name).text=value
    config = ET.SubElement(manifest,'config')
    ET.SubElement(config,'autoStart').text='yes'
    connector = ET.SubElement(ET.SubElement(config,'dependencies'),'pluginAppConnector')
    for name,value in (('type','Etch'),('id',str(CONNECTOR)),('asyncCallbacks','yes')):
        ET.SubElement(connector,name).text=value
    parameters = ET.SubElement(config,'parameters')
    ET.SubElement(parameters,'DescFile').text='{pluginDir}/DPFDeck.xml'
    ET.SubElement(parameters,'ImageDBFile').text='{pluginDir}/DPFDeck_ALL_Images.zip'
    ET.indent(manifest, space='  ')
    ET.ElementTree(manifest).write(output/'DPFDeck.manifest', encoding='utf-8', xml_declaration=True)

    image = Image.new('RGBA',(1160,444))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0,0,1159,443), radius=14, fill='#131b24')
    for index in range(6):
        x,y=(index%3)*386+8,(index//3)*132+58
        draw.rounded_rectangle((x,y,x+369,y+121),radius=10,fill='#1c2733')
    buffer = BytesIO()
    image.save(buffer,format='PNG',optimize=True)
    with ZipFile(output/'DPFDeck_ALL_Images.zip','w',ZIP_DEFLATED) as archive:
        archive.writestr(ZipInfo(str(BACKGROUND)+'.png'),buffer.getvalue(),ZIP_DEFLATED)


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=OUTPUT)
    generate(parser.parse_args().output)
