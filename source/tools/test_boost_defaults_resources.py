import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from xml.etree import ElementTree as ET
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
XML = ROOT / "native/manifests/BoostDeck.xml"
sys.path.insert(0, str(ROOT / "tools"))


class BoostDefaultsResources(unittest.TestCase):
    def setUp(self):
        self.current = ET.parse(XML)

    def test_generator_reproduces_committed_xml(self):
        spec = importlib.util.spec_from_file_location("boost_defaults_generator", ROOT / "tools/make_boostdeck_extended.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        with tempfile.TemporaryDirectory() as directory:
            generated = Path(directory) / "BoostDeck.xml"
            generated.write_bytes(XML.read_bytes())
            generator.XML = generated
            generator.update_xml()
            self.assertEqual(generated.read_bytes(), XML.read_bytes())

    def test_current_dashboard_geometry_and_image_ids_are_preserved(self):
        with ZipFile(ROOT.parent / "packages/dashboard.zip") as package:
            original = ET.fromstring(package.read("BoostDeck/BoostDeck.xml"))
        for component in original.findall(".//hmiState[@id='100']/components/*"):
            updated = self.current.find(f".//hmiState[@id='100']/components/*[@id='{component.get('id')}']")
            self.assertIsNotNone(updated)
            self.assertEqual(ET.tostring(component).strip(), ET.tostring(updated).strip())
        for model in original.findall(".//models/imageIdModel"):
            self.assertEqual(model.attrib, self.current.find(f".//models/imageIdModel[@id='{model.get('id')}']").attrib)

    def test_mode_radios_and_custom_layout_explanation_are_bound(self):
        button = self.current.find(".//hmiState[@id='520']/components/button[@model='361']")
        self.assertEqual(button.get("action"), "505")
        self.assertEqual(self.current.find(".//hmiAction[@id='505']").get("target"), "505")
        for choice in range(2):
            radio = self.current.find(f".//hmiState[@id='505']/components/radiobutton[@model='{5212+choice}']")
            action = self.current.find(f".//combinedAction[@id='{2212+choice}']/actions")
            self.assertEqual(radio.get("action"), str(2212+choice))
            self.assertEqual(action.find("raAction").get("id"), str(506+choice))
            self.assertEqual(action.find("hmiAction").get("target"), "520")
        self.assertIsNotNone(self.current.find(".//hmiState[@id='500']/components/label[@model='362']"))
        self.assertIn("custom", self.current.find(".//hmiState[@id='500']").get("text").lower())

    def test_connector_and_id_namespaces_are_consistent(self):
        manifest = ET.parse(ROOT / "native/manifests/BoostDeck.manifest")
        self.assertEqual(manifest.findtext(".//pluginAppConnector/id"), "100")
        for selector in (".//models/*", ".//pluginApp/actions/*", ".//hmiState"):
            ids = [node.get("id") for node in self.current.findall(selector)]
            self.assertEqual(len(ids), len(set(ids)))
        models = {node.get("id") for node in self.current.findall(".//models/*")}
        for node in self.current.findall(".//hmiState/*/*"):
            for key in ("model", "textModel"):
                if key in node.attrib:
                    self.assertIn(node.get(key), models)


if __name__ == "__main__":
    unittest.main()
