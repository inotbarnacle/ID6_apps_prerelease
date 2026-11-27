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

    def test_reagent_gauges_append_named_actions_and_models_without_changing_layouts(self):
        for signal, name in ((55, "Reagent tank level"), (56, "Average reagent consumption"),
                             (57, "Average demanded reagent consumption")):
            for slot in range(9):
                node = self.current.find(f".//hmiState[@id='{510+slot}']/components/radiobutton[@model='{5300+signal}']")
                self.assertIsNotNone(node)
                self.assertEqual(node.get("text"), name)
                self.assertEqual(node.get("action"), str(3500+signal))
            node = self.current.find(f".//combinedAction[@id='{3500+signal}']/actions/raAction")
            self.assertEqual(node.get("id"), str(600+signal))

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

    def test_named_egt_channels_keep_original_signal_ids_and_native_callbacks(self):
        names = []
        import re
        for line in (ROOT / "native/src/common/boost_signals.def").read_text().splitlines():
            match = re.match(r'SIGNAL\((\w+), "([^"]+)"', line)
            if match:
                names.append(match.groups())
        self.assertEqual(len(names), 58)
        self.assertEqual(names[30][0], "THROTTLE_TARGET")
        for index in range(31, 39):
            bank, sensor = divmod(index - 31, 4)
            expected = f"EGT bank {bank+1} sensor {sensor+1}"
            self.assertEqual(names[index][1], expected)
            action = self.current.find(f".//combinedAction[@id='{3500+index}']/actions/raAction")
            self.assertEqual(action.get("id"), str(600+index))
            for slot in range(9):
                radio = self.current.find(f".//hmiState[@id='{510+slot}']/components/radiobutton[@model='{5300+index}']")
                self.assertEqual(radio.get("text"), expected)
                self.assertEqual(radio.get("action"), str(3500+index))

    def test_egr_choices_keep_requested_actual_and_error_distinct(self):
        expected = ["EGR requested (PID 2C)", "EGR error (PID 2D)",
                    "EGR A requested duty/position", "EGR A actual duty/position", "EGR A error",
                    "EGR B requested duty/position", "EGR B actual duty/position", "EGR B error"]
        for index, name in enumerate(expected, 39):
            action = self.current.find(f".//combinedAction[@id='{3500+index}']/actions/raAction")
            self.assertEqual(action.get("id"), str(600+index))
            for slot in range(9):
                radio = self.current.find(f".//hmiState[@id='{510+slot}']/components/radiobutton[@model='{5300+index}']")
                self.assertEqual(radio.get("text"), name)
                self.assertEqual(radio.get("action"), str(3500+index))

    def test_pressure_choices_keep_requested_actual_and_reference_distinct(self):
        expected = [f"{source} {side} {kind} {reference} pressure"
                    for source, reference in (("Rail", "gauge"), ("Boost", "absolute"))
                    for side in ("A", "B") for kind in ("requested", "actual")]
        for index, name in enumerate(expected, 47):
            action = self.current.find(f".//combinedAction[@id='{3500+index}']/actions/raAction")
            self.assertEqual(action.get("id"), str(600+index))
            for slot in range(9):
                radio = self.current.find(f".//hmiState[@id='{510+slot}']/components/radiobutton[@model='{5300+index}']")
                self.assertEqual(radio.get("text"), name)
                self.assertEqual(radio.get("action"), str(3500+index))

    def test_regenerated_artwork_preserves_all_original_pixels(self):
        from io import BytesIO
        from PIL import Image
        spec = importlib.util.spec_from_file_location("boost_egt_artwork", ROOT / "tools/make_boostdeck_extended.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        with ZipFile(ROOT.parent / "packages/dashboard.zip") as package:
            original = package.read("BoostDeck/BoostDeck_ALL_Images.zip")
        with tempfile.TemporaryDirectory() as directory:
            source, target = Path(directory)/"original.zip", Path(directory)/"generated.zip"
            source.write_bytes(original)
            generator.update_images(source, target)
            with ZipFile(BytesIO(original)) as old, ZipFile(target) as new:
                self.assertEqual(set(old.namelist()), set(new.namelist()))
                self.assertEqual(len(new.namelist()), 906)
                for name in old.namelist():
                    a = Image.open(BytesIO(old.read(name))).convert("RGBA")
                    b = Image.open(BytesIO(new.read(name))).convert("RGBA")
                    self.assertEqual(a.size, b.size, name)
                    self.assertEqual(a.tobytes(), b.tobytes(), name)


if __name__ == "__main__":
    unittest.main()
