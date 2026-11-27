"""DPF resources/package checks and an explicit, non-deployable installer fixture."""
from hashlib import sha256
from io import BytesIO
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
from zipfile import BadZipFile, ZipFile, ZipInfo

from PIL import Image
import make_dpfdeck as layout
import package_dpfdeck as package

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('dpf_stage',ROOT/'source/tools/desktop-installer/stage-payload.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


def synthetic_arm(plugin):
    """A synthetic ELF-shaped fixture, never executable QNX code."""
    ident = b'\x7fELF\x01\x01\x01'+b'\0'*9
    header = struct.pack('<16sHHIIIIIHHHHHH',ident,3 if plugin else 2,40,1,
                         0 if plugin else 0x10054,52,0,0,52,32,1,0,0,0)
    segment = struct.pack('<8I',1,0,0x10000,0x10000,100,100,5,4)
    return header+segment+b'TEST ONLY'.ljust(16,b'\0')


def fixture_build(directory):
    binaries,resources = directory/'bin',directory/'resources'
    binaries.mkdir();layout.generate(resources)
    (binaries/'libDPFDeck.so').write_bytes(synthetic_arm(True))
    (binaries/'dpfdeck-collector-qnx').write_bytes(synthetic_arm(False))
    return binaries,resources


def stage_installer_fixture(repo):
    """Stage public baseline packages plus synthetic DPF data for Go tests only."""
    installer = repo/'source/tools/desktop-installer'
    with tempfile.TemporaryDirectory(prefix='dpf-installer-test-') as temp:
        root = Path(temp)
        binaries,resources = fixture_build(root)
        candidate = root/'candidate'
        (candidate/'packages').mkdir(parents=True)
        for archive in (repo/'packages').glob('*.zip'):
            if archive.name=='dpfdeck.zip':
                continue
            (candidate/'packages'/archive.name).symlink_to(archive.resolve())
        package.package(binaries,resources,candidate/'packages/dpfdeck.zip')
        for name in ('source/native/fileops.c','source/docs/installer-profiles.json'):
            path = candidate/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.symlink_to((repo/name).resolve())
        fileops = root/'test-fileops'
        fileops.write_bytes(b'TEST-ONLY placeholder; Go fixtures use their own host-built helper')
        stage.stage(candidate,fileops,installer,test_fixture=True)
    print('Staged synthetic DPF installer fixture. Do not distribute or install this payload.')


class DpfResourcesTest(unittest.TestCase):
    def test_generated_layout_manifest_images_and_callbacks_match(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            layout.generate(output)
            first = {p.name:p.read_bytes() for p in output.iterdir()}
            layout.generate(output)
            self.assertEqual(first,{p.name:p.read_bytes() for p in output.iterdir()})
            for name in ('DPFDeck.xml','DPFDeck.manifest'):
                self.assertEqual(first[name],(ROOT/'source/native/manifests'/name).read_bytes())
            app = ET.fromstring(first['DPFDeck.xml']).find('pluginApp')
            self.assertEqual(app.get('text'),'DPFDeck')
            self.assertEqual({x.get('id') for x in app.findall('hmiStates/hmiState')},{'100','101'})
            self.assertEqual({x.get('id') for x in app.findall('.//raAction')},{'420','421','430','431','440','441'})
            source = (ROOT/'source/native/src/plugins/dpf_deck.cpp').read_text()
            for identifier in (420,421,430,431,440,441):
                self.assertIn('addHandler('+str(identifier)+'u,action)',source)
            values = app.findall("hmiStates/hmiState[@id='100']/components/label")
            self.assertEqual({x.get('model') for x in values if x.get('model') and int(x.get('model'))<56},set(map(str,range(50,56))))
            self.assertEqual(next(x for x in values if x.get('id')=='202').get('model'),'60')
            self.assertEqual({x.get('id') for x in app.findall('.//raDataModel')},set(map(str,range(50,62))))
            manifest = ET.fromstring(first['DPFDeck.manifest'])
            self.assertEqual(manifest.findtext('config/dependencies/pluginAppConnector/id'),'123')
            for other in (ROOT/'source/native/manifests').glob('*.manifest'):
                if other.name!='DPFDeck.manifest':
                    self.assertNotEqual(ET.parse(other).findtext('config/dependencies/pluginAppConnector/id'),'123')
            with ZipFile(BytesIO(first['DPFDeck_ALL_Images.zip'])) as archive:
                self.assertEqual(archive.namelist(),['65100.png'])
                with Image.open(BytesIO(archive.read('65100.png'))) as image:
                    self.assertEqual(image.size,(1160,444))
                    self.assertEqual(image.mode,'RGBA')
                    self.assertEqual(image.getpixel((0,0))[3],0)
                    self.assertEqual(image.getpixel((100,100)),(28,39,51,255))

    def test_wide_bounds_and_compact_incompatibility_are_explicit(self):
        data = (ROOT/'source/native/manifests/DPFDeck.xml').read_bytes()
        for width,height in ((1280,480),(1440,540),(1920,720)):
            stage.check_bounds(data,stage.content_width(width,height),'DPFDeck')
        for width in (800,960):
            with self.assertRaises(ValueError): stage.check_bounds(data,stage.content_width(width,480),'DPFDeck')
        self.assertNotIn('DPFDeck',stage.COMPACT)

    def test_package_has_exact_files_sanitized_build_record_and_verified_sums(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp);binaries,resources = fixture_build(root)
            output = root/'dpfdeck.zip'
            package.package(binaries,resources,output)
            original = output.read_bytes()
            with ZipFile(output) as archive:
                self.assertEqual(set(archive.namelist()),{'DPFDeck/'+n for n in package.FILES}|{'SHA256SUMS','BUILD.json'})
                sums = dict((name,digest) for digest,name in (line.split('  ',1) for line in archive.read('SHA256SUMS').decode().splitlines()))
                for name,digest in sums.items(): self.assertEqual(sha256(archive.read(name)).hexdigest(),digest)
                build = json.loads(archive.read('BUILD.json'))
                self.assertIn('not QNX ABI',build['architecture_check'])
                self.assertNotIn(str(root),json.dumps(build))
                self.assertEqual(set(build['inputs']),{'DPFDeck/'+n for n in package.FILES})
            with self.assertRaises(ValueError): package.package(binaries,resources,output)
            self.assertEqual(output.read_bytes(),original)

    def test_host_wrong_arch_truncated_or_invalid_elf_is_rejected(self):
        good = synthetic_arm(False)
        bad = [b'',good[:51],good[:60]]
        for index,value in ((4,2),(5,2),(18,62),(20,0),(40,51),(42,31),(44,0),(24,255)):
            value_bytes = bytearray(good);value_bytes[index]=value
            bad.append(bytes(value_bytes))
        bad.append(good[:52]+struct.pack('<8I',1,0,0x10000,0x10000,200,200,5,4)+good[84:])
        for value in bad:
            with self.assertRaises(ValueError): package.checked_arm_elf(value,False)
        with self.assertRaises(ValueError): package.checked_arm_elf(synthetic_arm(False),True)

    def test_missing_empty_or_symlink_input_never_creates_package(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);binaries,resources=fixture_build(root)
            path=binaries/'dpfdeck-collector-qnx';data=path.read_bytes();path.unlink()
            with self.assertRaises(ValueError): package.package(binaries,resources,root/'missing.zip')
            path.write_bytes(b'')
            with self.assertRaises(ValueError): package.package(binaries,resources,root/'empty.zip')
            path.unlink();target=root/'worker';target.write_bytes(data);path.symlink_to(target)
            with self.assertRaises(ValueError): package.package(binaries,resources,root/'symlink.zip')
            self.assertFalse(list(root.glob('*.zip')))

    def test_wrong_connector_or_image_geometry_cannot_be_packaged(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);binaries,resources=fixture_build(root)
            path=resources/'DPFDeck.manifest'
            path.write_text(path.read_text().replace('<id>123</id>','<id>100</id>'))
            with self.assertRaises(ValueError): package.package(binaries,resources,root/'wrong-id.zip')
            layout.generate(resources)
            image=BytesIO();Image.new('RGBA',(10,10)).save(image,format='PNG')
            with ZipFile(resources/'DPFDeck_ALL_Images.zip','w') as archive: archive.writestr('65100.png',image.getvalue())
            with self.assertRaises(ValueError): package.package(binaries,resources,root/'wrong-image.zip')

    def test_optional_stage_preserves_baseline_and_removes_stale_dpf_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);binaries,resources=fixture_build(root)
            repo=root/'repo';(repo/'packages').mkdir(parents=True)
            for name in ('source/native/fileops.c','source/docs/installer-profiles.json'):
                path=repo/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('fixture')
            data=b'unchanged baseline'
            with ZipFile(repo/'packages/base.zip','w') as archive:
                archive.writestr('ExistingApp/file',data)
                archive.writestr('SHA256SUMS',sha256(data).hexdigest()+'  ExistingApp/file\n')
            fileops=root/'helper';fileops.write_bytes(b'fixture')
            installer=root/'installer';installer.mkdir()
            with patch.dict(stage.GROUPS,{'base':('ExistingApp',)},clear=True):
                stage.stage(repo,fileops,installer)
                self.assertFalse((installer/'payload/DPFDeck').exists())
                package.package(binaries,resources,repo/'packages/dpfdeck.zip')
                stage.stage(repo,fileops,installer)
                self.assertEqual({p.name for p in (installer/'payload/DPFDeck').iterdir()},set(package.FILES))
                self.assertEqual((installer/'payload/ExistingApp/file').read_bytes(),data)
                fixture=repo/'source/tools/desktop-installer';fixture.mkdir(parents=True)
                fixture_marker=fixture/'payload/TEST-ONLY-DPF-FIXTURE'
                write=Path.write_bytes
                observed=[]
                def observe_write(path,data):
                    if path.name in ('libDPFDeck.so','dpfdeck-collector-qnx') and path.parent.name=='DPFDeck':
                        observed.append((path.name,fixture_marker.is_file()))
                    return write(path,data)
                with patch.object(Path,'write_bytes',observe_write):
                    stage_installer_fixture(repo)
                self.assertEqual({name for name,_ in observed},{'libDPFDeck.so','dpfdeck-collector-qnx'})
                self.assertTrue(all(protected for _,protected in observed))
                self.assertTrue(fixture_marker.is_file())
                (repo/'packages/dpfdeck.zip').unlink()
                stage.stage(repo,fileops,installer)
                self.assertFalse((installer/'payload/DPFDeck').exists())
                self.assertEqual((installer/'payload/ExistingApp/file').read_bytes(),data)
                marker=installer/'payload/TEST-ONLY-DPF-FIXTURE'
                marker.write_text('do not release')
                (repo/'packages/base.zip').write_bytes(b'incomplete staging input')
                with self.assertRaises(BadZipFile):
                    stage.stage(repo,fileops,installer)
                self.assertTrue(marker.is_file())
                interrupted=root/'interrupted-installer';interrupted.mkdir()
                with self.assertRaises(BadZipFile):
                    stage.stage(repo,fileops,interrupted,test_fixture=True)
                self.assertTrue((interrupted/'payload/TEST-ONLY-DPF-FIXTURE').is_file())


if __name__=='__main__':
    if sys.argv[1:]==['--stage-installer-fixture']:
        stage_installer_fixture(ROOT)
    else:
        unittest.main()
