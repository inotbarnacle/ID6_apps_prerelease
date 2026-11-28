"""Publishing retries must use the catalog and preserve the already published bytes."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("publish_release", Path(__file__).resolve().parents[1] / "tools/publish_release.py")
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class PublishRetryTests(unittest.TestCase):
    def retry(self, corrupt=False):
        payload = b"already published archive"
        entry = {"version": "0.1.2", "file": "releases/0.1.2/app.tar", "size": len(payload),
                 "sha256": hashlib.sha256(payload).hexdigest()}
        catalog = {plugin: {"releases": [entry]} for plugin in publisher.PLUGINS}
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "VERSION").write_text("0.1.2")
            def output(*args):
                return "" if args[1:3] == ("status", "--porcelain") else "committed-head"
            with patch.multiple(publisher, ROOT=root), patch.object(publisher, "output", side_effect=output), \
                 patch.object(publisher.subprocess, "run", return_value=SimpleNamespace(returncode=0)), \
                 patch.object(publisher, "live_catalog", return_value=catalog), \
                 patch.object(publisher, "flare_drive_note", return_value="note"), \
                 patch.object(publisher, "fetch", return_value=b"bad" if corrupt else payload), \
                 patch.object(publisher, "run") as build, patch.object(publisher, "flare_publish") as upload, \
                 patch.object(publisher, "verify"), patch.object(publisher, "tag_and_release") as github:
                args = SimpleNamespace(stable=True, experimental=["mhd-maps"])
                if corrupt:
                    with self.assertRaisesRegex(SystemExit, "doesn't match the catalog"):
                        publisher.publish(args)
                    github.assert_not_called()
                else:
                    publisher.publish(args)
                    archives = github.call_args.args[2]
                    self.assertEqual(len(archives), 5)
                    self.assertTrue(all(a.read_bytes() == payload for a in archives))
                build.assert_not_called()
                upload.assert_not_called()

    def test_all_published_finishes_github_without_rebuilding(self):
        self.retry()

    def test_corrupt_published_archive_stops_retry(self):
        self.retry(corrupt=True)

    def test_invalid_catalog_signature_is_refused(self):
        with patch.object(publisher.runpy, "run_path", return_value={"call": lambda *a: {
            "catalog": {"plugins": []}, "signatureValid": False}}):
            with self.assertRaisesRegex(SystemExit, "signature"):
                publisher.live_catalog()


if __name__ == "__main__":
    unittest.main()
