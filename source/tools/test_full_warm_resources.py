"""Full-screen Warm RHMI generation and actual emulator packet acceptance."""
import ctypes
import importlib.util
from io import BytesIO
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]/'fullscreen'
spec = importlib.util.spec_from_file_location('full_warm_overlay',ROOT/'emulator/library_overlay.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


class Stream:
    def __init__(self,data): self.data=data
    def makefile(self,mode):
        assert mode=='rb'
        return BytesIO(self.data)


def receive(data):
    instance=overlay.LibraryOverlay.__new__(overlay.LibraryOverlay)
    instance._client=Stream(data)
    instance._size=496
    instance._magic=0x49443656
    instance._hidden_page=14
    instance._animation=ctypes.create_string_buffer(32)
    instance._lock=threading.Lock()
    instance._packet=instance._frame_packet=None
    instance._last=0
    instance._receive()
    return instance


def packet(magic):
    return struct.pack('<II',magic,7)+bytes(488)


class FullWarmResources(unittest.TestCase):
    def test_generator_reproduces_all_generated_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            candidate=Path(temp)
            for name in ('catalog.json','native/generate.py','native/features.py',
                         'native/telemetry/common/boost_signals.def'):
                target=candidate/name;target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(ROOT/name,target)
            (candidate/'preview').mkdir()
            subprocess.run([sys.executable,'-B',str(candidate/'native/generate.py')],check=True,capture_output=True)
            outputs=list((candidate/'native/generated').rglob('*'))+[candidate/'preview/catalog.js']
            self.assertGreater(len(outputs),25)
            for path in outputs:
                if path.is_file():
                    self.assertEqual(path.read_bytes(),(ROOT/path.relative_to(candidate)).read_bytes(),str(path))

    def test_readiness_fallback_units_and_bounds_on_all_layouts(self):
        for folder,right in (('',1260),('layouts/800x480/',720),('layouts/960x480/',900)):
            tree=ET.parse(ROOT/('native/generated/'+folder+'ID6FullWarmDeck.xml'))
            state=tree.find(".//hmiState[@id='207']")
            for index in range(3):
                model=str(1543+index)
                self.assertIsNotNone(tree.find(".//models/raDataModel[@id='"+model+"']"))
                self.assertIsNotNone(state.find("components/label[@model='"+model+"']"))
            for action,model in ((5750,1880),(5752,1882)):
                self.assertEqual(state.find("optionComponents/button[@action='"+str(action)+"']").get('model'),str(model))
            for node in state.findall('components/*'):
                values={p.get('id'):int(p.get('value')) for p in node.findall('properties/property')}
                self.assertLessEqual(values.get('20',0)+values.get('9',0),right)
                self.assertLessEqual(values.get('21',0)+values.get('10',0),477)
            advisory=state.find("components/label[@id='8007']")
            self.assertIn('advisory',advisory.get('text'))
            for selector in ('.//models/*','.//actions/*'):
                ids=[node.get('id') for node in tree.findall(selector)]
                self.assertEqual(len(ids),len(set(ids)))

    def test_emulator_rejects_previous_surface_version(self):
        for magic in (0x49443653,0x49443654,0x49443655):
            old=packet(magic)
            state=receive(b'ID6HUD '+old.hex().encode()+b'\n')
            self.assertIsNone(state._packet)
            self.assertEqual(state._last,0)

    def test_emulator_accepts_new_version_ignores_bad_frames(self):
        current=packet(0x49443656)
        data=b'ID6HUD '+current.hex().encode()+b'\nID6HUD not-hex\nID6HUD 00\n'
        data+=b'ID6HUD '+packet(0x49443653).hex().encode()+b'\n'
        state=receive(data)
        self.assertEqual(state._packet,current)
        self.assertGreater(state._last,0)


if __name__=='__main__': unittest.main()
