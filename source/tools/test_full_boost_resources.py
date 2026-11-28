"""Full-screen Boost channel bindings and matching emulator preference contracts."""
import ctypes
import importlib.util
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

SOURCE=Path(__file__).resolve().parents[1]
FULL=SOURCE/'fullscreen'
spec=importlib.util.spec_from_file_location('boost_overlay',FULL/'emulator/library_overlay.py')
overlay=importlib.util.module_from_spec(spec);spec.loader.exec_module(overlay)


class FullBoostResources(unittest.TestCase):
    def test_catalogue_matches_shared_core_and_all_picker_actions(self):
        shared=(SOURCE/'native/src/common/boost_signals.def').read_bytes()
        self.assertEqual(shared,(FULL/'native/telemetry/common/boost_signals.def').read_bytes())
        signals=re.findall(r'^SIGNAL\(([A-Z0-9_]+),\s*"([^"]+)"',shared.decode(),re.M)
        self.assertEqual(len(signals),58)
        self.assertEqual(signals[24][0],'STFT_B1');self.assertEqual(signals[28][0],'LAMBDA_TARGET')
        for folder in ('','layouts/800x480/','layouts/960x480/'):
            tree=ET.parse(FULL/('native/generated/'+folder+'ID6FullBoostDeck.xml'))
            picker=tree.find(".//hmiState[@id='2180']/components")
            buttons=picker.findall('button');self.assertEqual(len(buttons),58)
            for index,(_,title) in enumerate(signals):
                button=picker.find("button[@id='%d']"%(3000+index))
                self.assertEqual(button.get('text'),title)
                self.assertEqual(button.get('action'),str(4500+index))
                self.assertEqual(tree.find(".//combinedAction[@id='%d']/actions/raAction"%(4500+index)).get('id'),str(5500+index))

    def test_modes_dynamic_captions_and_fallback_bounds(self):
        for folder,right in (('',1260),('layouts/800x480/',720),('layouts/960x480/',900)):
            tree=ET.parse(FULL/('native/generated/'+folder+'ID6FullBoostDeck.xml'))
            for action in (5850,5851):
                self.assertIsNotNone(tree.find(".//hmiState[@id='214']/components/button[@action='%d']"%action))
                self.assertIsNotNone(tree.find(".//actions/raAction[@id='%d']"%action))
            for model in (1830,1831,*range(1840,1849)):
                self.assertIsNotNone(tree.find(".//models/raDataModel[@id='%d']"%model))
            state=tree.find(".//hmiState[@id='200']")
            for model in range(1840,1849):
                self.assertIsNotNone(state.find("components/label[@model='%d']"%model))
            for node in state.findall('components/*'):
                props={p.get('id'):int(p.get('value')) for p in node.findall('properties/property')}
                self.assertLessEqual(props.get('20',0)+props.get('9',0),right)
                self.assertLessEqual(props.get('21',0)+props.get('10',0),477)

    def test_c_guest_and_python_overlay_validate_the_same_records(self):
        compiler=shutil.which('cc')
        self.assertIsNotNone(compiler,'a host C compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'validate.c';library=root/'validate.so'
            source.write_text('#include "boost_config.h"\nint validate(unsigned k,const unsigned char* d,size_t n) { return boost_config_valid(k,d,n); }\n')
            subprocess.run([compiler,'-std=c99','-Wall','-Wextra','-Werror','-fPIC','-shared','-I'+str(FULL/'emulator'),str(source),'-o',str(library)],check=True,capture_output=True)
            native=ctypes.CDLL(str(library));native.validate.argtypes=(ctypes.c_uint,ctypes.c_void_p,ctypes.c_size_t)
            cases=[]
            for version in range(1,7):
                stride=11 if version==1 else 9
                record=bytes((ord('B'),version,1))+bytes(range(stride))*2
                cases.extend(((1,record,True),(1,record[:-1],False),(1,record+b'\0',False)))
                limit=(0,31,31,39,47,55,58)[version]
                cases.append((1,record[:3]+bytes((limit,))+record[4:],False))
            cases.extend((kind,record,valid) for kind,record,valid in (
                (0,b'G\x01\x08\x01',True),(0,b'G\x01\x08\x02',False),
                (0,b'G\x02\x08'+bytes(7)+b'\x0c',True),
                (0,b'G\x02\x08'+bytes(7)+b'\x0d',False),
                (2,b'D\x01\0',True),(2,b'D\x01\x01',True),(2,b'D\x01\x02',False)))
            for kind,data,expected in cases:
                self.assertEqual(bool(native.validate(kind,ctypes.create_string_buffer(data),len(data))),expected)
                self.assertEqual(overlay.LibraryOverlay._valid_config(kind,data),expected)
            randomizer=random.Random(728)
            for _ in range(500):
                kind=randomizer.randrange(4);data=bytes(randomizer.randrange(256) for _ in range(randomizer.randrange(28)))
                self.assertEqual(bool(native.validate(kind,ctypes.create_string_buffer(data),len(data))),
                                 overlay.LibraryOverlay._valid_config(kind,data))

    def test_overlay_preserves_versions_and_default_mode_in_private_files(self):
        with tempfile.TemporaryDirectory() as directory:
            instance=overlay.LibraryOverlay.__new__(overlay.LibraryOverlay)
            instance._config=tuple(Path(directory)/name for name in ('prefs','layout','mode'))
            for kind,data in ((1,b'B\x06\x01'+bytes((57,))*18),(2,b'D\x01\0')):
                instance._save_config(f'ID6CONF {kind} {data.hex()}\n'.encode())
                self.assertEqual(instance._config[kind].read_bytes(),data)
                instance._save_config(f'ID6CONF {kind} 00\n'.encode())
                self.assertEqual(instance._config[kind].read_bytes(),data)


if __name__=='__main__': unittest.main()
