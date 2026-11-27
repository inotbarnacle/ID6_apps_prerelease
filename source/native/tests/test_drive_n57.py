"""N57 ShiftDeck/LaunchDeck on loopback HSFZ, plus the lower shift threshold bindings."""
import ast
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET
from zipfile import ZipFile

import test_drive_apps
import test_n57

NATIVE = Path(__file__).resolve().parents[1]
BUILD = NATIVE / "build"
sys.path.insert(0, str(NATIVE.parent / "tools"))
import extend_shift_n57


class DriveN57Test(unittest.TestCase):
    def sample(self, speed_only=False, index="010203", rpm="0640", speed="0001", worker="shift-deck"):
        transactions = [("22f150", ["62f150" + index])]
        if index == "010203":
            if speed_only:
                transactions += test_n57.dyn("4c14", speed)
            else:
                transactions += [("2c03f303", ["6c03f303"]),
                                 ("2c01f303595501024c140102", ["6c01f303"]),
                                 ("22f303", ["62f303" + rpm + speed])]
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0)); udp.settimeout(3)
            start = time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1000000
            args = ["--cycles", "1", "--n57-sgbd", "010203", "--udp-port", str(udp.getsockname()[1])]
            if speed_only:
                args += ["--speed-only"]
            test_drive_apps.DriveAppsTest.run_worker(self, worker, args, transactions)
            packet = udp.recv(128)
            self.assertEqual(len(packet), 24)
            magic, sampled, rpm_value, mkph = struct.unpack("=QQii", packet)
            self.assertLessEqual(start, sampled)
            self.assertLessEqual(sampled, time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1000000)
            self.assertEqual(magic, 0xFFFFFFFF31454444)
        return rpm_value, mkph

    def test_shift_uses_dde_with_fractional_speed_in_both_builds(self):
        for worker in ("shift-deck", "shift-deck-fast"):
            with self.subTest(worker=worker):
                self.assertEqual(self.sample(worker=worker), (800, 5))

    def test_launch_speed_only_never_queries_rpm_or_mode01(self):
        self.assertEqual(self.sample(speed_only=True, speed="2710"), (-1, 45780))

    def test_wrong_identity_and_invalid_rpm_cannot_produce_live_data(self):
        for changes in ({"index": "040506"}, {"rpm": "ffff"}, {"rpm": ""}):
            with self.subTest(changes=changes):
                self.assertEqual(self.sample(**changes), (-1, -1))

    def test_partial_pair_rejects_all_measurements(self):
        self.assertEqual(self.sample(speed=""), (-1, -1))

    def test_shift_layout_keeps_upstream_targets_and_adds_n57_window(self):
        path = NATIVE / "manifests/ShiftDeck.xml"
        original = path.read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            generated = Path(directory) / path.name
            generated.write_bytes(original)
            extend_shift_n57.update_xml(generated)
            self.assertEqual(generated.read_bytes(), original)
        app = ET.fromstring(original).find("pluginApp")
        actions = [x.get("id") for x in app.find("actions")]
        self.assertEqual(len(actions), len(set(actions)))
        choices = app.find("hmiStates/hmiState[@id='101']/components")
        with ZipFile(NATIVE.parents[1] / "packages/dashboard.zip") as archive:
            upstream = ET.fromstring(archive.read("ShiftDeck/ShiftDeck.xml"))
        expected = upstream.find("pluginApp/hmiStates/hmiState[@id='101']/components")
        self.assertEqual([(b.get("action"), b.get("text")) for b in choices.findall("button")],
                         [(b.get("action"), b.get("text")) for b in expected.findall("button")])
        window = app.find("hmiStates/hmiState[@id='102']/components")
        self.assertEqual([b.get("action") for b in window.findall("button")],
                         ["121", "122", "123", "124", "125"])
        self.assertEqual(window.find("button[@action='125']").get("text"), "Reset: 4600-4800 RPM")
        source = (NATIVE.parent / "tools/shift_art.py").read_text()
        targets = ast.literal_eval(re.search(r"^TARGETS = (.+)$", source, re.M)[1])
        header = (NATIVE / "src/plugins/shift_assets.h").read_text()
        self.assertIn("{" + ", ".join(map(str, targets)) + "}", header)
        self.assertIn("kDefaultN57Target = 6u", header)


if __name__ == "__main__":
    unittest.main()
