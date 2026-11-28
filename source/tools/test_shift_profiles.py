from io import BytesIO
from pathlib import Path
import unittest
from xml.etree import ElementTree as ET
from zipfile import ZipFile

from PIL import Image
import make_shift_profiles as profiles
import shift_art

REPO = Path(__file__).resolve().parents[2]


class ShiftAssetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with ZipFile(REPO/"packages/dashboard.zip") as package:
            cls.base = package.read("ShiftDeck/ShiftDeck_ALL_Images.zip")
        # Frozen current upstream layout; the packaged ZIP still contains the older five-preset UI.
        cls.old_xml = (REPO/"source/tools/fixtures/shift-standard-v0719.xml").read_bytes()
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
        for action in range(600, 607):
            self.assertIsNotNone(new.find(f".//actions/raAction[@id='{action}']"))
            self.assertIsNotNone(new.find(f".//button[@action='{action}']"))
        self.assertIn("not an engine redline", self.xml.decode())
        for state in (101, 102):
            self.assertEqual(ET.tostring(old.find(f".//hmiState[@id='{state}']/components")).strip(),
                             ET.tostring(new.find(f".//hmiState[@id='{state}']/components")).strip())
        self.assertEqual(new.find(".//hmiState[@id='101']").get("text"),"Gasoline shift presets")
        self.assertEqual(old.find(".//hmiState[@id='102']").attrib,new.find(".//hmiState[@id='102']").attrib)
        for selector in (".//models/*", ".//actions/*", ".//hmiState"):
            identifiers = [node.get("id") for node in new.findall(selector)]
            self.assertEqual(len(identifiers), len(set(identifiers)))

    def test_existing_gasoline_image_payloads_are_identical(self):
        with ZipFile(BytesIO(self.base)) as old, ZipFile(self.output) as new:
            for name in old.namelist():
                self.assertEqual(old.read(name), new.read(name), name)
            self.assertEqual(len(new.namelist()), len(set(new.namelist())))

    def test_every_initial_native_image_model_has_a_payload(self):
        with ZipFile(self.output) as archive:
            for node in ET.fromstring(self.xml).findall(".//models/imageIdModel"):
                self.assertIn(node.get("imageId")+".png", archive.namelist())

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
                        self.assertAlmostEqual(center, shift_art.BAR_START+shift_art.BAR_SLANT+
                                               16*shift_art.BAR_PITCH*target/maximum, delta=0.5)
                        self.assertTrue(0 <= bounds[0] < bounds[2] <= marker.width)
            for index in range(3):
                with Image.open(BytesIO(archive.read(f"{profiles.SCALE_BASE+index}.png"))) as scale:
                    self.assertEqual(scale.size, (1104, 51))
                    self.assertNotIn((255, 69, 72, 255), {color for _, color in scale.getcolors(scale.width*scale.height)})

    def test_equal_gasoline_and_diesel_sweep_fractions_have_identical_markers(self):
        # A 3,000/4,000 target must align exactly with upstream's 6,000/8,000 marker.
        with ZipFile(self.output) as archive:
            compared=0
            for scale_index, maximum in enumerate(profiles.SCALES):
                for index, target in enumerate(profiles.TARGETS):
                    gasoline=target*8000//maximum
                    if target>=maximum or target*8000%maximum or gasoline not in shift_art.TARGETS:
                        continue
                    a=Image.open(BytesIO(archive.read(f"{profiles.MARK_BASE+scale_index*64+index}.png"))).convert("RGBA")
                    b=Image.open(BytesIO(archive.read(f"{shift_art.MARK_BASE+shift_art.TARGETS.index(gasoline)}.png"))).convert("RGBA")
                    self.assertEqual(a.tobytes(),b.tobytes())
                    compared+=1
            self.assertGreater(compared,10)

    def test_constants_and_new_asset_ranges_match(self):
        self.assertEqual((profiles.ROOT/"src/plugins/shift_profile_assets.h").read_text(), profiles.CONSTANTS)
        ids = [identifier for identifier, _ in profiles.bitmaps()]
        self.assertEqual(len(ids), len(set(ids)))
        with ZipFile(BytesIO(self.base)) as original:
            self.assertTrue({f"{identifier}.png" for identifier in ids}.isdisjoint(original.namelist()))
        self.assertTrue(set(ids).isdisjoint(profiles.GASOLINE_IMAGES))

    def test_all_current_gasoline_targets_and_markers_are_added_from_upstream(self):
        with ZipFile(self.output) as archive:
            for identifier, expected in shift_art.bitmaps(face=profiles.font):
                if identifier in profiles.GASOLINE_IMAGES:
                    actual = Image.open(BytesIO(archive.read(f"{identifier}.png"))).convert("RGBA")
                    self.assertEqual(actual.size, expected.size)
                    self.assertEqual(actual.tobytes(), expected.tobytes())

    def test_current_upstream_generated_archive_is_accepted_unchanged(self):
        source, output = BytesIO(), BytesIO()
        with ZipFile(source, "w") as archive:
            for identifier, image in shift_art.bitmaps(face=profiles.font):
                data = BytesIO()
                image.save(data, format="PNG")
                archive.writestr(f"{identifier}.png", data.getvalue())
        profiles.add_images(source, output)
        with ZipFile(source) as original, ZipFile(output) as extended:
            for name in original.namelist():
                self.assertEqual(original.read(name), extended.read(name), name)

    def test_retired_overlapping_diesel_archive_is_rejected_before_writing(self):
        source, output = BytesIO(), BytesIO()
        with ZipFile(BytesIO(self.base)) as original, ZipFile(source,"w") as old:
            for item in original.infolist():
                old.writestr(item, original.read(item))
            old.writestr("57021.png", original.read("55920.png"))
        with self.assertRaisesRegex(ValueError,"retired overlapping"):
            profiles.add_images(source, output)
        self.assertEqual(output.getvalue(), b"")

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
