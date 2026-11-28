import importlib.util
from io import BytesIO
from pathlib import Path
import sys
import tempfile
import unittest
from xml.etree import ElementTree as ET
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
XML = ROOT / "native/manifests/BoostDeck.xml"
sys.path.insert(0, str(ROOT / "tools"))


class BoostScaleResources(unittest.TestCase):
    def setUp(self):
        self.current = ET.parse(XML)

    def test_generator_reproduces_committed_layout(self):
        spec = importlib.util.spec_from_file_location("boost_generator", ROOT / "tools/make_boostdeck_extended.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        with tempfile.TemporaryDirectory() as directory:
            generated = Path(directory) / "BoostDeck.xml"
            generated.write_bytes(XML.read_bytes())
            generator.XML = generated
            generator.update_xml()
            self.assertEqual(generated.read_bytes(), XML.read_bytes())
        self.assertEqual(generator.SETTINGS[8], ("Diesel rail display scale", 4))

    def test_preserves_dashboard_geometry_and_existing_images(self):
        with ZipFile(ROOT.parent / "packages/dashboard.zip") as package:
            original = ET.fromstring(package.read("BoostDeck/BoostDeck.xml"))
        for component in original.findall(".//hmiState[@id='100']/components/*"):
            updated = self.current.find(f".//hmiState[@id='100']/components/*[@id='{component.get('id')}']")
            self.assertIsNotNone(updated)
            self.assertEqual(ET.tostring(component).strip(), ET.tostring(updated).strip())
        for model in original.findall(".//models/imageIdModel"):
            self.assertEqual(model.attrib, self.current.find(f".//models/imageIdModel[@id='{model.get('id')}']").attrib)

    def test_radio_callbacks_models_and_return_destination(self):
        self.assertEqual(self.current.find(".//hmiAction[@id='529']").get("target"), "529")
        self.assertEqual(self.current.find(".//hmiState[@id='520']/components/button[@model='128']").get("action"), "529")
        for choice in range(4):
            radio = self.current.find(f".//hmiState[@id='529']/components/radiobutton[@id='{860+choice}']")
            self.assertEqual(radio.get("model"), str(5160+choice))
            self.assertEqual(radio.get("textModel"), str(860+choice))
            self.assertEqual(radio.get("action"), str(2160+choice))
            action = self.current.find(f".//combinedAction[@id='{2160+choice}']/actions")
            self.assertEqual(action.find("raAction").get("id"), str(2560+choice))
            self.assertEqual(action.find("hmiAction").get("target"), "520")

    def test_connector_and_namespaces(self):
        manifest = ET.parse(ROOT / "native/manifests/BoostDeck.manifest")
        self.assertEqual(manifest.findtext(".//pluginAppConnector/id"), "100")
        for selector in (".//models/*", ".//pluginApp/actions/*", ".//hmiState"):
            ids = [node.get("id") for node in self.current.findall(selector)]
            self.assertEqual(len(ids), len(set(ids)))
        models = {node.get("id") for node in self.current.findall(".//models/*")}
        for node in self.current.findall(".//hmiState/*/*"):
            for attribute in ("model", "textModel"):
                if attribute in node.attrib:
                    self.assertIn(node.get(attribute), models)

    def test_regenerated_images_preserve_all_packaged_pixels(self):
        from make_boostdeck_extended import update_images
        from PIL import Image
        with ZipFile(ROOT.parent / "packages/dashboard.zip") as package, tempfile.TemporaryDirectory() as directory:
            original_bytes = package.read("BoostDeck/BoostDeck_ALL_Images.zip")
            source, generated = Path(directory)/"original.zip", Path(directory)/"generated.zip"
            source.write_bytes(original_bytes)
            update_images(source, generated)
            with ZipFile(BytesIO(original_bytes)) as original, ZipFile(generated) as updated:
                self.assertEqual(set(original.namelist()), set(updated.namelist()))
                for name in original.namelist():
                    a = Image.open(BytesIO(original.read(name))).convert("RGBA")
                    b = Image.open(BytesIO(updated.read(name))).convert("RGBA")
                    self.assertEqual(a.size, b.size, name)
                    self.assertEqual(a.tobytes(), b.tobytes(), name)


if __name__ == "__main__":
    unittest.main()
