from io import BytesIO
from pathlib import Path
import unittest
from xml.etree import ElementTree as ET
from zipfile import ZipFile

from PIL import Image
import make_shift_profiles as profiles

REPO = Path(__file__).resolve().parents[2]


class ShiftAssetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with ZipFile(REPO/"packages/dashboard.zip") as package:
            cls.base = package.read("ShiftDeck/ShiftDeck_ALL_Images.zip")
            cls.old_xml = package.read("ShiftDeck/ShiftDeck.xml")
        cls.xml = (profiles.ROOT/"manifests/ShiftDeck.xml").read_bytes()
        cls.output = BytesIO()
        profiles.add_images(BytesIO(cls.base), cls.output)

    def test_xml_is_reproducible_and_preserves_dashboard_geometry_and_models(self):
        self.assertEqual(profiles.update_xml(self.xml), self.xml)
        old, new = ET.fromstring(self.old_xml), ET.fromstring(self.xml)
        for node in old.findall(".//hmiState[@id='100']/components/*"):
            current = new.find(f".//hmiState[@id='100']/components/*[@id='{node.get('id')}']")
            self.assertIsNotNone(current)
            self.assertEqual(ET.tostring(node).strip(), ET.tostring(current).strip())
        for model in old.findall(".//models/*"):
            self.assertEqual(model.attrib, new.find(f".//models/*[@id='{model.get('id')}']").attrib)
        for action in old.findall(".//actions/*"):
            self.assertEqual(action.attrib, new.find(f".//actions/*[@id='{action.get('id')}']").attrib)
        for action in range(48, 55):
            self.assertIsNotNone(new.find(f".//actions/raAction[@id='{action}']"))
            self.assertIsNotNone(new.find(f".//button[@action='{action}']"))
        self.assertIn("not an engine redline", self.xml.decode())

    def test_existing_gasoline_image_payloads_are_identical(self):
        with ZipFile(BytesIO(self.base)) as old, ZipFile(self.output) as new:
            for name in old.namelist():
                self.assertEqual(old.read(name), new.read(name), name)
            self.assertEqual(len(new.namelist()), len(set(new.namelist())))

    def test_every_target_scale_and_marker_is_present_and_inside_the_canvas(self):
        with ZipFile(self.output) as archive:
            for index, target in enumerate(profiles.TARGETS):
                with Image.open(BytesIO(archive.read(f"{profiles.TARGET_BASE+index}.png"))) as tile:
                    self.assertEqual(tile.size, (180, 135))
                    self.assertIsNotNone(tile.getbbox())
                for scale_index, maximum in enumerate(profiles.SCALES):
                    if target >= maximum:
                        continue
                    with Image.open(BytesIO(archive.read(f"{profiles.MARK_BASE+scale_index*64+index}.png"))) as marker:
                        bounds = marker.getbbox()
                        self.assertEqual(marker.size, (1104, 9))
                        center = (bounds[0]+bounds[2]-1)/2
                        self.assertAlmostEqual(center, 8+1088*target/maximum, delta=0.5)
                        self.assertTrue(0 <= bounds[0] < bounds[2] <= marker.width)
            for index in range(3):
                with Image.open(BytesIO(archive.read(f"{profiles.SCALE_BASE+index}.png"))) as scale:
                    self.assertEqual(scale.size, (1104, 51))
                    self.assertNotIn((255, 69, 72, 255), {color for _, color in scale.getcolors(scale.width*scale.height)})

    def test_constants_and_new_asset_ranges_match(self):
        self.assertEqual((profiles.ROOT/"src/plugins/shift_profile_assets.h").read_text(), profiles.CONSTANTS)
        ids = [identifier for identifier, _ in profiles.bitmaps()]
        self.assertEqual(len(ids), len(set(ids)))
        with ZipFile(BytesIO(self.base)) as original:
            self.assertTrue({f"{identifier}.png" for identifier in ids}.isdisjoint(original.namelist()))

    def test_patching_an_extended_archive_is_idempotent(self):
        second = BytesIO()
        profiles.add_images(self.output, second)
        with ZipFile(self.output) as first, ZipFile(second) as other:
            self.assertEqual(first.namelist(), other.namelist())
            for name in first.namelist():
                self.assertEqual(first.read(name), other.read(name))

    def test_rejects_an_old_layout_or_incomplete_base_archive(self):
        with self.assertRaisesRegex(ValueError, "128-step"):
            profiles.update_xml(self.xml.replace(b'imageId="56400"', b'imageId="55800"'))
        empty = BytesIO()
        with ZipFile(empty, "w"):
            pass
        output = BytesIO()
        with self.assertRaisesRegex(ValueError, "base archive"):
            profiles.add_images(empty, output)
        self.assertEqual(output.getvalue(), b"")


if __name__ == "__main__":
    unittest.main()
