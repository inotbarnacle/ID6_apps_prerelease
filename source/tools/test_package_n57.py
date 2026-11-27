"""Offline bundle checks with synthetic ELF headers and real public resources."""
from io import BytesIO
import json
import importlib.util
from pathlib import Path
import stat
import struct
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile, ZipInfo

import package_n57 as package


def elf(shared=False, machine=40):
    data = bytearray(52)
    data[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<HH", data, 16, 3 if shared else 2, machine)
    return bytes(data)


def app_files(app):
    _, archive, worker, _ = package.APPS[app]
    files = package.app_resources(app)
    files["lib" + app + ".so"] = elf(True)
    files[worker] = elf()
    return files


class PackageTest(unittest.TestCase):
    def test_six_app_scope_and_binary_inventory(self):
        self.assertEqual(set(package.APPS), {"BoostDeck", "ShiftDeck", "LaunchDeck", "EngineCodes",
                                             "PowerDeck", "XDriveControl"})
        binaries = {"lib" + app + ".so" for app in package.APPS}
        binaries.update(worker for _, _, worker, _ in package.APPS.values())
        self.assertEqual(len(binaries), 10)
        self.assertEqual(2 * len(package.APPS), 12)
        count = 0
        for app in package.APPS:
            files = app_files(app)
            files.pop("SHA256SUMS", None)
            count += len(files) + 1  # Each app has its own generated SHA256SUMS.
        self.assertEqual(count, 35)

    def test_rejects_dirty_source_or_mismatched_build_identity(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            report = root / "build.json"
            with patch.object(package.subprocess, "check_output", side_effect=["commit\n", " M source"]):
                with self.assertRaises(ValueError):
                    package.assemble(root / "output", root, report)
            for field, value in (("source_commit", "wrong"), ("native_tree", "wrong"), ("car_access", True)):
                info = {"source_commit": "commit", "native_tree": "tree", "car_access": False}
                info[field] = value; report.write_text(json.dumps(info))
                with patch.object(package.subprocess, "check_output", side_effect=["commit\n", "", "tree\n"]):
                    with self.assertRaises(ValueError):
                        package.assemble(root / "output", root, report)
            self.assertFalse((root / "output").exists())

    def test_elf_architecture_and_role(self):
        package.check_elf(elf(), False)
        package.check_elf(elf(True), True)
        for data, shared in ((b"not ELF", False), (elf(machine=3), False),
                             (elf(True), False), (elf(), True)):
            with self.subTest(shared=shared):
                with self.assertRaises(ValueError):
                    package.check_elf(data, shared)

    def test_complete_resources_and_matching_connectors(self):
        for app, (_, _, worker, connector) in package.APPS.items():
            package.validate_app(app_files(app), app, worker, connector)
        files = app_files("BoostDeck")
        for missing in ("BoostDeck.xml", "BoostDeck_ALL_Images.zip"):
            broken = files.copy(); del broken[missing]
            with self.assertRaises(ValueError):
                package.validate_app(broken, "BoostDeck", "boostdeck-collector-qnx", 100)
        with self.assertRaises(ValueError):
            package.validate_app(files, "BoostDeck", "boostdeck-collector-qnx", 104)
        broken = files.copy()
        images = BytesIO()
        with ZipFile(images, "w") as z:
            z.writestr("unused.png", b"")
        broken["BoostDeck_ALL_Images.zip"] = images.getvalue()
        with self.assertRaises(ValueError):
            package.validate_app(broken, "BoostDeck", "boostdeck-collector-qnx", 100)

    def bundle(self, path, change=None, mode=0o755):
        files, workers = {}, set()
        for app, (group, _, worker, _) in package.APPS.items():
            base = group + "/" + app + "/"
            app_data = app_files(app)
            app_data.pop("SHA256SUMS", None)
            app_data["SHA256SUMS"] = package.checksums(app_data)
            files.update({base + n: data for n, data in app_data.items()})
            workers.add(base + worker)
        if change == "private":
            files["config/xdrive-owner-vin.txt"] = b"private placeholder"
        if change == "retired-app":
            files["core/BaseDiagnostik/extra.txt"] = b"retired standalone app"
        if change == "hud-enabled":
            files["optional/HudTest/hud-enabled"] = b"1\n"
        digest = package.checksums(files)
        if change == "tamper":
            files["core/BoostDeck/BoostDeck.xml"] += b" "
        if change == "uncovered":
            files["extra.txt"] = b"not covered"
        if change == "traversal":
            files["../escape"] = b"invalid path"
        files["SHA256SUMS"] = digest
        with ZipFile(path, "w") as z:
            for name, data in files.items():
                entry = ZipInfo("bundle/" + name)
                entry.external_attr = (stat.S_IFREG | (mode if name in workers else 0o644)) << 16
                z.writestr(entry, data)

    def test_complete_archive(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "bundle.zip"
            self.bundle(path)
            package.verify_zip(path)

    def test_installer_overlay_uses_verified_payload_without_private_settings(self):
        module = importlib.util.spec_from_file_location(
            "stage_payload", Path(__file__).parent / "desktop-installer/stage-payload.py")
        stage = importlib.util.module_from_spec(module)
        module.loader.exec_module(stage)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.zip"
            self.bundle(path)
            apps = stage.candidate_apps(path, package.ROOT)
            self.assertEqual(set(apps), set(package.APPS))
            self.assertIn("libPowerDeck.so", apps["PowerDeck"])
            self.assertNotIn("SHA256SUMS", apps["PowerDeck"])
            self.assertNotIn("gateway-ip.txt", apps["PowerDeck"])
            self.bundle(path, "tamper")
            with self.assertRaises(ValueError):
                stage.candidate_apps(path, package.ROOT)

    def test_ordinary_installer_staging_omits_unbundled_powerdeck(self):
        module = importlib.util.spec_from_file_location(
            "stage_payload", Path(__file__).parent / "desktop-installer/stage-payload.py")
        stage = importlib.util.module_from_spec(module)
        module.loader.exec_module(stage)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            helper = root / "fileops"
            helper.write_bytes(elf())
            installer = root / "installer"
            stale = installer / "payload/PowerDeck"
            stale.mkdir(parents=True)
            (stale / "PowerDeck.manifest").write_bytes(b"old candidate")
            # Layout scaling is checked here; bitmap resampling has independent coverage.
            with patch.object(stage, "compact_images", side_effect=lambda data, _: data):
                stage.stage(package.ROOT, helper, installer)
            self.assertFalse(stale.exists())
            self.assertTrue((installer / "payload/BoostDeck/BoostDeck.manifest").is_file())
            candidate = root / "candidate.zip"
            self.bundle(candidate)
            with patch.object(stage, "compact_images", side_effect=lambda data, _: data):
                stage.stage(package.ROOT, helper, installer, candidate)
            self.assertEqual((stale / "PowerDeck.manifest").read_bytes(),
                             app_files("PowerDeck")["PowerDeck.manifest"])

    def test_rejects_tampering_uncovered_paths_private_settings_and_permissions(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "bundle.zip"
            for change in ("tamper", "uncovered", "traversal", "private", "hud-enabled", "retired-app"):
                with self.subTest(change=change):
                    self.bundle(path, change)
                    with self.assertRaises(ValueError):
                        package.verify_zip(path)
            self.bundle(path, mode=0o644)
            with self.assertRaises(ValueError):
                package.verify_zip(path)


if __name__ == "__main__":
    unittest.main()
