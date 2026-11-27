"""New N57 dashboard resources, strict values and actual local child lifecycle."""
from io import BytesIO
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from zipfile import ZipFile

from PIL import Image

NATIVE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(NATIVE.parent / "tools"))
import make_n57_dashboards as layout
import power_graph_art as graph


class N57DashboardTest(unittest.TestCase):
    def test_digit_ink_fits_fixed_hmi_spacing_with_fallback_fonts(self):
        for glyph in "0123456789- .":
            image = layout.digit_image(glyph)
            self.assertEqual(image.size, (52, 98))
            bounds = image.getbbox()
            if bounds:
                self.assertLessEqual(bounds[2] - bounds[0], 34)
                self.assertGreater(bounds[0], 0)
                self.assertLess(bounds[2], 52)

    def test_value_freshness_and_canonical_bounds(self):
        self.compile_case("test_n57_dashboard_values.cpp")

    def test_actual_child_lifecycle_and_loopback_packets(self):
        self.compile_case("test_n57_dashboard.cpp", fake_sdk=True)

    def compile_case(self, name, fake_sdk=False):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            cc = os.environ.get("CC", "cc")
            cxx = os.environ.get("CXX", "c++")
            subprocess.run([cc, "-std=c99", "-DPERF_LAB", "-Isrc/common", "-Isrc/ipc", "-Isrc/workers", "-Isrc/plugins", "-c", "src/common/gauge_core.c",
                            "-o", str(output / "core.o")], cwd=NATIVE, check=True, capture_output=True)
            includes = ["-Itests/dashboard_sdk"] if fake_sdk else []
            subprocess.run([cxx, "-std=gnu++98", "-Wall", "-Wextra", "-Werror", "-DPERF_LAB",
                            *includes, "-Isrc/common", "-Isrc/ipc", "-Isrc/workers", "-Isrc/plugins", str(NATIVE / "tests" / name), str(output / "core.o"),
                            "-lm", "-o", str(output / "check")], cwd=NATIVE, check=True, capture_output=True)
            args = [str(output / "check")]
            if fake_sdk:
                worker = output / "collector"
                subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", str(NATIVE / "tests/dashboard_worker.c"),
                                "-o", str(worker)], check=True, capture_output=True)
                args.append(str(worker))
            result = subprocess.run(args, check=True, capture_output=True, text=True, timeout=8)
            self.assertIn("OK", result.stdout)

    def test_generated_resource_parity_and_references(self):
        with tempfile.TemporaryDirectory() as directory:
            layout.generate(directory)
            for name in layout.APPS:
                for extension in ("xml", "manifest"):
                    self.assertEqual((Path(directory) / f"{name}.{extension}").read_bytes(),
                                     (NATIVE / "manifests" / f"{name}.{extension}").read_bytes())
                app = ET.parse(Path(directory) / f"{name}.xml").find("pluginApp")
                with ZipFile(Path(directory) / f"{name}_ALL_Images.zip") as images:
                    names = images.namelist()
                    self.assertEqual(len(names), len(set(names)))
                    for model in app.findall("models/imageIdModel"):
                        filename = model.get("imageId") + ".png"
                        self.assertIn(filename, names)
                        image = Image.open(BytesIO(images.read(filename)))
                        self.assertGreater(image.width, 0)
                for section in ("actions", "models", "hmiStates"):
                    ids = [node.get("id") for node in app.find(section)]
                    self.assertEqual(len(ids), len(set(ids)), (name, section))
                # RHMI resolves objects through one registry; digit models must
                # not shadow actions even when each section is unique.
                ids = [node.get("id") for node in app.iter()
                       if node.tag != "property" and node.get("id") is not None]
                self.assertEqual(len(ids), len(set(ids)), (name, "global object IDs"))
                for state in app.findall("hmiStates/hmiState"):
                    # Exact 006 HMI truncates overly large component lists.
                    self.assertLessEqual(len(state.findall("components/*")), 64)

    def test_main_content_and_digit_frames_fit(self):
        for name in layout.APPS:
            app = layout.document(name).find("pluginApp")
            components = app.find("hmiStates/hmiState[@id='100']/components")
            component_ids = [node.get("id") for node in components]
            self.assertEqual(len(component_ids), len(set(component_ids)))
            for component in components:
                props = {int(item.get("id")): item.get("value") for item in component.findall("properties/property")}
                if {20, 21, 9, 10}.issubset(props):
                    x, y, width, height = [int(props[key]) for key in (20, 21, 9, 10)]
                    self.assertGreaterEqual(x, 0)
                    self.assertGreaterEqual(y, 0)
                    self.assertLessEqual(x + width, layout.WIDTH, (name, component.attrib))
                    self.assertLessEqual(y + height, 420, (name, component.attrib))
            value_images = [node for node in components.findall("image") if 60 <= int(node.get("model")) <= 95]
            self.assertEqual(len(value_images), 24)

    def test_identity_masks_and_no_fabricated_ecu_channels(self):
        root = NATIVE / "src/plugins"
        power = (root / "power_deck.cpp").read_text()
        common = (root / "n57_dashboard.h").read_text()
        gauges = (NATIVE / "src/common/n57_gauges.def").read_text()
        self.assertIn('"--n57-sgbd"', common)
        self.assertIn("n57_settings(&index)!=1", common)
        self.assertIn("BD_BIT(BD_RPM)|BD_BIT(BD_TORQUE)|BD_BIT(BD_POWER)", power)
        for signal in ("BATTERY", "FUEL_VOLUME", "COOLANT", "OIL_TEMP"):
            self.assertIn("N57_GAUGE(" + signal + ",", gauges)
        for name, (connector, _) in layout.APPS.items():
            manifest = layout.manifest(name)
            self.assertEqual(manifest.findtext("config/dependencies/pluginAppConnector/id"), str(connector))

    def test_only_powerdeck_is_generated(self):
        self.assertEqual(set(layout.APPS), {"PowerDeck"})
        with tempfile.TemporaryDirectory() as directory:
            layout.generate(directory)
            self.assertFalse(any("BaseDiagnostik" in path.name for path in Path(directory).iterdir()))
        with self.assertRaises(KeyError):
            list(layout.own_bitmaps("BaseDiagnostik"))
        self.assertFalse((NATIVE / "src/plugins/base_diagnostik.cpp").exists())
        self.assertFalse((NATIVE / "manifests/BaseDiagnostik.manifest").exists())

    def test_saved_rpm_plot_has_two_series_and_independent_axes(self):
        app = layout.document("PowerDeck").find("pluginApp")
        page = app.find("hmiStates/hmiState[@id='111']")
        self.assertEqual(page.get("text"), "PowerDeck - RPM curves")
        components = page.find("components")
        self.assertEqual(len(components), 52)
        for series in range(2):
            for line in range(24):
                index = series * 24 + line
                image = components.find(f"image[@id='{4000 + index}']")
                self.assertEqual(image.get("model"), str(2400 + index))
                properties = {int(item.get("id")): item.get("value") for item in image.findall("properties/property")}
                self.assertEqual([int(properties[key]) for key in (20, 21, 9, 10)], [graph.SAVED_LEFT + line * graph.SAVED_STEP, 30, 64, 3])
        self.assertEqual(app.find("models/imageIdModel[@id='2499']").get("imageId"), "64300")
        # Rasterized axes plus two dynamic peak labels stay under the 64-component ceiling.
        self.assertEqual([node.get("model") for node in components.findall("label")],
                         ["2500", "2510", "2520"])
        self.assertIsNotNone(app.find("models/raDataModel[@id='2510']"))
        self.assertIsNotNone(app.find("models/raDataModel[@id='2520']"))
        for component, width, height in ((4190, 150, 34), (4191, 280, 26)):
            peak = components.find(f"label[@id='{component}']")
            properties = {int(item.get("id")): item.get("value")
                          for item in peak.findall("properties/property")}
            self.assertEqual([int(properties[key]) for key in (20, 21, 9, 10)],
                             [graph.SAVED_LEFT, graph.SAVED_TOP, width, height])
            self.assertEqual(properties[3], "false")
        footer = components.find("label[@id='4181']")
        properties = {int(item.get("id")): int(item.get("value")) for item in footer.findall("properties/property")
                      if item.get("id") in ("20", "21", "9", "10")}
        self.assertEqual([properties[key] for key in (20, 21, 9, 10)], [0, 370, layout.WIDTH, 50])
        self.assertEqual(len(app.find("hmiStates/hmiState[@id='110']/components")), 50)
        for state in app.findall("hmiStates/hmiState"):
            for component in state.findall("components/*"):
                properties = {int(item.get("id")): item.get("value") for item in component.findall("properties/property")}
                if {20, 21, 9, 10}.issubset(properties):
                    x, y, width, height = [int(properties[key]) for key in (20, 21, 9, 10)]
                    self.assertGreaterEqual(x, 0)
                    self.assertGreaterEqual(y, 0)
                    self.assertLessEqual(x + width, layout.WIDTH)
                    self.assertLessEqual(y + height, 420)

    def test_saved_slope_bitmaps_preserve_series_and_geometry(self):
        images = dict(graph.bitmaps())
        with ZipFile(NATIVE / "manifests/PowerDeck_ALL_Images.zip") as archive:
            for identifier, expected in images.items():
                actual = Image.open(BytesIO(archive.read(f"{identifier}.png"))).convert("RGBA")
                self.assertEqual(actual.size, expected.size, identifier)
                self.assertEqual(actual.tobytes(), expected.tobytes(), identifier)
        self.assertEqual(images[64202].size, (1, 1))
        self.assertEqual(images[64300].size, (layout.WIDTH, 370))
        self.assertNotIn(64301, images)
        for series in range(2):
            for delta in (-300, -149, 0, 149, 300):
                image = images[63000 + series * 601 + delta + 300]
                self.assertEqual(image.size, (64, abs(delta) + 3))
                first = 1 if delta >= 0 else 1 - delta
                second = 1 + delta if delta >= 0 else 1
                self.assertGreater(image.getpixel((0, first))[3], 0)
                self.assertGreater(image.getpixel((graph.SAVED_STEP, second))[3], 0)
                # The 2 px stroke can cover x=36; its padded tail stays transparent.
                self.assertIsNone(image.getchannel("A").crop((graph.SAVED_STEP+2, 0, 64, image.height)).getbbox())
        # Both power units use the 6000-RPM end as their common axis spine.
        self.assertEqual(graph.SAVED_LEFT + (graph.SAVED_POINTS - 1) * graph.SAVED_STEP, layout.WIDTH-158)

    def test_saved_power_labels_are_smaller_paired_units_on_the_same_row(self):
        self.assertEqual(graph.SAVED_LEVELS, (400,))
        self.assertEqual(graph.METRIC_PS_KW, 0.73549875)
        font = graph.shift_art.font(18)
        red = (255, 69, 72, 255)
        blue = (123, 210, 255, 255)
        gray = (169, 185, 201, 255)
        image = graph.saved_background(400)
        ticks = graph.saved_power_ticks(400)
        self.assertEqual([kw for kw, _ in ticks], list(range(0, 401, 50)))
        for kw, y in ticks:
            self.assertLessEqual(abs(y - (330 - 300 * kw / 400)), 0.5)
            self.assertEqual(image.getpixel((graph.SAVED_RIGHT, y)), red)
        pairs = graph.saved_power_labels(400)
        self.assertEqual(pairs, [(0, 0, 330), (100, 136, 255), (200, 272, 180),
                                 (300, 408, 105), (400, 544, 30)])
        for kw, ps, y in pairs:
            self.assertLessEqual(abs(ps * 0.73549875 - kw), 0.73549875 / 2)
            for left, right, color in ((graph.SAVED_RIGHT+22, graph.SAVED_RIGHT+51, red), (graph.SAVED_RIGHT+56, graph.SAVED_RIGHT+68, gray), (graph.SAVED_RIGHT+74, graph.SAVED_RIGHT+103, blue)):
                pixels = image.crop((left, y - 9, right, y + 10)).getcolors(maxcolors=2048) or []
                self.assertTrue(any(p[:3] == color[:3] and p[3] for _, p in pixels))
            self.assertLessEqual(font.getbbox(str(ps))[2], layout.WIDTH-(graph.SAVED_RIGHT+74))
        # Paired labels derive PS from each kW major tick; no independent PS scale.
        self.assertEqual(graph.saved_power_ticks(400)[1], (50, 293))
        with self.assertRaises(ValueError):
            graph.saved_power_ticks(650)
        unit_image = dict(layout.own_bitmaps("PowerDeck"))[60008]
        self.assertEqual(unit_image.tobytes(), layout.text_image((70, 32), "PS", 30, layout.MUTED).tobytes())


if __name__ == "__main__":
    unittest.main()
