import importlib.util
from io import BytesIO
from pathlib import Path
import sys
import tempfile
import unittest
from xml.etree import ElementTree as ET
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


class WarmReadinessResources(unittest.TestCase):
    def setUp(self):
        self.current = ET.parse(ROOT / "native/manifests/WarmDeck.xml")
        with ZipFile(ROOT.parent / "packages/warmdeck.zip") as package:
            self.original = ET.fromstring(package.read("WarmDeck/WarmDeck.xml"))
            self.images = package.read("WarmDeck/WarmDeck_ALL_Images.zip")

    def test_component_ids_and_geometry_are_preserved(self):
        for old in self.original.findall(".//hmiState[@id='100']/components/*"):
            new = self.current.find(f".//hmiState[@id='100']/components/*[@id='{old.get('id')}']")
            self.assertIsNotNone(new)
            self.assertEqual(ET.tostring(old.find("properties")), ET.tostring(new.find("properties")))
        for model in self.original.findall(".//models/imageIdModel"):
            self.assertEqual(model.attrib, self.current.find(f".//models/imageIdModel[@id='{model.get('id')}']").attrib)

    def test_independent_target_labels_bind_to_native_models(self):
        for fluid in range(3):
            node = self.current.find(f".//label[@id='{250+fluid}']")
            self.assertEqual(node.get("model"), str(55+fluid))
            self.assertNotIn("text", node.attrib)
            self.assertEqual(self.current.find(f".//raDataModel[@id='{55+fluid}']").get("modelType"), "Richtext")
        for selector in (".//models/*", ".//pluginApp/actions/*", ".//hmiState"):
            ids = [node.get("id") for node in self.current.findall(selector)]
            self.assertEqual(len(ids), len(set(ids)))

    def test_generator_reproduces_layout_and_all_original_pixels(self):
        from PIL import Image
        spec = importlib.util.spec_from_file_location("warm_generator", ROOT / "tools/make_warmdeck.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        with tempfile.TemporaryDirectory() as directory:
            generator.PLUGIN = Path(directory)
            generator.main()
            self.assertEqual((generator.PLUGIN/"WarmDeck.xml").read_bytes(), (ROOT/"native/manifests/WarmDeck.xml").read_bytes())
            with ZipFile(BytesIO(self.images)) as old, ZipFile(generator.PLUGIN/"WarmDeck_ALL_Images.zip") as new:
                self.assertEqual(set(old.namelist()), set(new.namelist()))
                self.assertEqual(len(new.namelist()), 22)
                for name in old.namelist():
                    a = Image.open(BytesIO(old.read(name))).convert("RGBA")
                    b = Image.open(BytesIO(new.read(name))).convert("RGBA")
                    self.assertEqual(a.size, b.size, name)
                    self.assertEqual(a.tobytes(), b.tobytes(), name)


if __name__ == "__main__":
    unittest.main()
