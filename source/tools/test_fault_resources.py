from pathlib import Path
import unittest
from xml.etree import ElementTree as ET
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]


class FaultResourceTest(unittest.TestCase):
    def setUp(self):
        self.current = ET.parse(ROOT/"native/manifests/EngineCodes.xml")
        with ZipFile(ROOT.parent/"packages/dashboard.zip") as package:
            self.original = ET.fromstring(package.read("EngineCodes/EngineCodes.xml"))

    def test_preserves_current_dashboard_geometry_and_background(self):
        for component in self.original.findall(".//hmiState[@id='101']/components/*"):
            new = self.current.find(f".//hmiState[@id='101']/components/*[@id='{component.get('id')}']")
            self.assertIsNotNone(new)
            self.assertEqual(ET.tostring(component).strip(), ET.tostring(new).strip())
        for model in self.original.findall(".//models/*"):
            self.assertEqual(model.attrib, self.current.find(f".//models/*[@id='{model.get('id')}']").attrib)
        self.assertEqual(self.current.find(".//imageIdModel[@id='95']").get("imageId"), "56700")

    def test_clear_requires_confirmation_page_and_profiles_have_no_clear_action(self):
        self.assertEqual(self.current.find(".//hmiAction[@id='25']").get("target"), "102")
        confirmations = self.current.findall(".//button[@action='27']")
        self.assertEqual(len(confirmations), 1)
        self.assertIs(confirmations[0], self.current.find(".//hmiState[@id='102']/components/button[@action='27']"))
        self.assertIn("Confirm clear", confirmations[0].get("text"))
        self.assertEqual(self.current.find(".//hmiAction[@id='32']").get("target"), "103")
        profile_actions = {node.get("action") for node in self.current.findall(".//hmiState[@id='103']/components/button")}
        self.assertEqual(profile_actions, {"30", "31"})
        self.assertIsNotNone(self.current.find(".//hmiState[@id='101']/components/button[@action='23']"))

    def test_connector_ids_and_action_model_state_namespaces_remain_consistent(self):
        manifest = ET.parse(ROOT/"native/manifests/EngineCodes.manifest")
        self.assertEqual(manifest.findtext(".//pluginAppConnector/id"), "102")
        for selector in (".//models/*", ".//actions/*", ".//hmiState"):
            identifiers = [node.get("id") for node in self.current.findall(selector)]
            self.assertEqual(len(identifiers), len(set(identifiers)))
        self.assertEqual(self.current.find(".//raDataModel[@id='96']").get("modelType"), "Richtext")
        self.assertIsNotNone(self.current.find(".//hmiState[@id='103']/components/label[@model='96']"))


if __name__ == "__main__":
    unittest.main()
