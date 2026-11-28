"""Flare Drive's own scripts for the three plugins, run on a pretend EntryNav2 (tests/headunit.py) as the app runs them.

Flare Drive's `gradlew :core:test` writes the scripts it generates to core/build/head-unit-scripts/entrynav-apps
(EntryNavAppsTest.writesTheScriptsForTheShellTests). tests/run_setup.py mounts that folder at /scripts when the
flare-drive repository sits beside this one; this suite is skipped otherwise. Each script runs as the app sends it over
SSH, `sh -c <script>`, with the car's own programs: the upload of the archive on the standard input, then the snapshot,
status, the gateway and the removal. The install script's @MD5@ and @TOP@ are this release's.
"""

import hashlib
import subprocess
import sys
import unittest
from pathlib import Path

from headunit import Unit

ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text().strip()
SCRIPTS = Path("/scripts")
PLUGINS = {"boostdeck": ("BoostDeck", "BOOSTDECK"), "shiftdeck": ("ShiftDeck", "SHIFTDECK"), "bm3-maps": ("Bm3Maps", "BM3MAPS")}
LOCK = "tmp/aa-entrynav-launch.lock"


def archive(plugin):
    return ROOT / "dist" / f"{plugin}-{VERSION}-armhf.tar"


def script(name):
    return (SCRIPTS / name).read_text()


def install_script(plugin):
    data = archive(plugin).read_bytes()
    return script(f"{plugin}/install.sh").replace("@MD5@", hashlib.md5(data).hexdigest()).replace("@TOP@", f"{plugin}-{VERSION}")


@unittest.skipUnless(SCRIPTS.is_dir(), "Flare Drive's scripts aren't mounted (flare-drive beside this repository, gradlew :core:test)")
class FlareDriveTest(unittest.TestCase):
    def setUp(self):
        self.unit = Unit()

    def tearDown(self):
        self.unit.close()

    def send(self, text, data=b"", expect=0):
        """Runs a script as Flare Drive's RemoteShell does: sh -c, standard error merged, the archive on standard input."""
        result = subprocess.run(["chroot", str(self.unit.base), "/bin/sh", "-c", text], input=data, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin"}, timeout=300)
        out = result.stdout.decode("utf-8", "replace")
        self.assertEqual(result.returncode, expect, out)
        self.assertTrue(self.unit.root_read_only(), "system partition left writable\n" + out)
        return out

    def install(self, plugin, expect=0, data=None):
        return self.send(install_script(plugin), archive(plugin).read_bytes() if data is None else data, expect)

    def uploads(self):
        return sorted(p.name for p in self.unit.path("tmp").iterdir() if p.name.startswith("entrynav-apps-app."))

    def installed(self, plugin):
        return self.unit.path(f"opt/hmi/data/ro/Plugins/{PLUGINS[plugin][0]}/.installed-by-entrynav-apps").exists()

    def test_install_facts(self):
        out = self.send(script("boostdeck/install-facts.sh"))
        self.assertIn("FIRMWARE=202306092158", out)
        self.assertIn("GENERATION=0", out)

    def test_each_installs_through_the_app_and_shows_in_the_snapshot(self):
        out = self.send(script("snapshot.sh"))
        for _, prefix in PLUGINS.values():
            self.assertIn(f"{prefix} installed: no", out)
        self.assertTrue(out.rstrip().endswith("SNAPSHOT_END"), out)
        for plugin in PLUGINS:
            out = self.install(plugin)
            self.assertIn("is installed", out)
            self.assertTrue(self.installed(plugin), plugin)
        self.assertEqual(self.uploads(), [])
        self.assertFalse(self.unit.path(LOCK).exists())
        out = self.send(script("snapshot.sh"))
        for _, prefix in PLUGINS.values():
            self.assertIn(f"{prefix} installed: {VERSION}", out)
            self.assertIn(f"{prefix} files: intact", out)
        self.assertIn("INSTALLER_IDLE", self.send(script("installer-running.sh")))

    def test_a_changed_upload_installs_nothing(self):
        data = bytearray(archive("shiftdeck").read_bytes())
        data[600] ^= 1
        self.install("shiftdeck", expect=1, data=bytes(data))
        self.assertFalse(self.installed("shiftdeck"))
        self.assertEqual(self.uploads(), [])

    def test_a_set_aside_install_installs_nothing(self):
        # Check installed files raised the generation after this install's connection was lost.
        self.unit.path("tmp/aa-entrynav-generation").write_text("1\n")
        self.assertIn("this install was set aside", self.install("bm3-maps", expect=1))
        self.assertFalse(self.installed("bm3-maps"))

    def test_the_launch_lock_holds_an_install_back(self):
        self.unit.path(LOCK).mkdir()
        self.assertIn("another install, removal or settling check holds the launch lock", self.install("boostdeck", expect=1))
        self.assertFalse(self.installed("boostdeck"))
        self.assertTrue(self.unit.path(LOCK).is_dir(), "someone else's lock was taken away")

    def test_status_gateway_and_removal(self):
        self.assertIn("installed: no", self.send(script("shiftdeck/status.sh")))
        self.assertIn("no files removed", self.send(script("shiftdeck/uninstall.sh")))
        self.assertIn("STOP: ShiftDeck is not installed", self.send(script("shiftdeck/gateway.sh"), expect=1))
        self.install("shiftdeck")
        self.install("boostdeck")
        self.assertIn(f"installed: {VERSION}", self.send(script("shiftdeck/status.sh")))
        self.assertIn("No gateway answered", self.send(script("shiftdeck/gateway.sh"), expect=1))  # nothing answers here
        self.assertIn("ShiftDeck is uninstalled", self.send(script("shiftdeck/uninstall.sh")))
        self.assertFalse(self.installed("shiftdeck"))
        self.assertTrue(self.installed("boostdeck"))
        self.assertIn("installed: no", self.send(script("shiftdeck/status.sh")))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *sys.argv[1:]], verbosity=2)
