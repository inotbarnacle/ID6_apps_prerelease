"""Keep the upstream dashboards intact while adding N57 controls and assets."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET
from zipfile import ZipFile

NATIVE = Path(__file__).resolve().parents[1]
PACKAGES = NATIVE.parents[1] / "packages"


def geometry(tree):
    return {node.get("id"): {p.get("id"): p.get("value") for p in node.findall("properties/property")
                            if p.get("id") in ("20", "21", "9", "10")}
            for node in tree.findall("pluginApp/hmiStates/hmiState[@id='100']/components/*")}


class UpstreamN57UI(unittest.TestCase):
    def test_original_main_components_keep_upstream_geometry(self):
        for app, package in (("BoostDeck", "dashboard.zip"), ("ShiftDeck", "dashboard.zip"),
                             ("LaunchDeck", "launchdeck.zip")):
            with self.subTest(app=app), ZipFile(PACKAGES / package) as archive:
                expected = geometry(ET.fromstring(archive.read(f"{app}/{app}.xml")))
                actual = geometry(ET.parse(NATIVE / "manifests" / f"{app}.xml"))
                if app == "BoostDeck":
                    expected["318"]["10"] = "36"  # The former combined value gives room to its unit below.
                    self.assertEqual(actual["319"], {"20": "1080", "21": "373", "9": "112", "10": "40"})
                self.assertEqual({key: actual[key] for key in expected}, expected)

    def test_boost_remains_one_panel_with_upstream_dials_radios_and_alerts(self):
        tree = ET.parse(NATIVE / "manifests/BoostDeck.xml")
        states = tree.findall("pluginApp/hmiStates/hmiState")
        self.assertEqual([s.get("id") for s in states if s.get("text") == "BoostDeck"], ["100"])
        labels = " ".join(node.get("text", "") for node in tree.iter())
        for absent in ("Startup panel", "Panel name", "Next panel", "Previous panel", "Main panel"):
            self.assertNotIn(absent, labels)
        for option in ("Gauge style", "Coolant high", "Oil temp high", "Oil pressure low",
                       "Hide emissions gauges", "Show emissions gauges"):
            self.assertIn(option, labels)
        self.assertEqual(len(tree.findall("pluginApp/hmiStates/hmiState[@id='100']/components/image[@model='330']")), 1)
        signals = len(re.findall(r"^SIGNAL\(", (NATIVE / "src/common/boost_signals.def").read_text(), re.M))
        for slot in range(9):
            choices = tree.findall(f"pluginApp/hmiStates/hmiState[@id='{510+slot}']/components/radiobutton")
            self.assertEqual(len(choices), signals)
            self.assertEqual(len({c.get("id") for c in choices}), signals)
        self.assertTrue(tree.findall("pluginApp/hmiStates/hmiState[@id='501']/components/radiobutton"))

    def test_resource_references_and_component_ids(self):
        for app, package in (("BoostDeck", "dashboard.zip"), ("ShiftDeck", "dashboard.zip"),
                             ("LaunchDeck", "launchdeck.zip")):
            with self.subTest(app=app):
                tree = ET.parse(NATIVE / "manifests" / f"{app}.xml")
                path = NATIVE / "manifests" / f"{app}_ALL_Images.zip"
                if path.is_file():
                    archive = ZipFile(path)
                else:
                    from io import BytesIO
                    with ZipFile(PACKAGES / package) as original:
                        archive = ZipFile(BytesIO(original.read(f"{app}/{app}_ALL_Images.zip")))
                with archive:
                    assets = {Path(name).stem for name in archive.namelist()}
                for model in tree.findall("pluginApp/models/imageIdModel"):
                    self.assertIn(model.get("imageId"), assets)
                for section in ("actions", "models", "hmiStates"):
                    ids = [int(n.get("id")) for n in tree.findall(f"pluginApp/{section}/*")]
                    self.assertEqual(len(ids), len(set(ids)))
                    self.assertTrue(all(0 < identifier < 10000 for identifier in ids))
                for state in tree.findall("pluginApp/hmiStates/hmiState"):
                    ids = [n.get("id") for n in state.findall("components/*")]
                    self.assertEqual(len(ids), len(set(ids)))
        with ZipFile(NATIVE / "manifests/ShiftDeck_ALL_Images.zip") as archive:
            ids = {int(Path(name).stem) for name in archive.namelist()}
            for first, count in ((59400, 129), (59000, 129), (59162, 12), (59300, 3)):
                self.assertTrue(set(range(first, first+count)) <= ids)


if __name__ == "__main__":
    unittest.main()
