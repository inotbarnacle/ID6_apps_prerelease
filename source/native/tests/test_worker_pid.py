"""Compile and exercise every production worker-owning plugin against the host BMW fake."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

NATIVE = Path(__file__).resolve().parents[1]
PLUGINS = {
    # source: (initial action, requires visible page before the action, HUD toggle semantics)
    "beamer": (20, 1, 0),
    "boost_deck": (0, 0, 0),
    "bootmod3_suite": (0, 0, 0),
    "engine_codes": (23, 0, 0),
    "hud_menu": (21, 0, 1),
    "hybrid_deck": (0, 0, 0),
    "launch_deck": (0, 0, 0),
    "mg_flasher": (20, 0, 0),
    "mhd_maps": (21, 0, 0),
    "power_deck": (0, 0, 0),
    "road_jet": (0, 0, 0),
    "shift_deck": (0, 0, 0),
    "warm_deck": (0, 0, 0),
    "xdrive_control": (21, 0, 0),
}


class WorkerPidTest(unittest.TestCase):
    def test_production_plugin_pid_ownership(self):
        with tempfile.TemporaryDirectory(prefix="id6-worker-pid-") as directory:
            output = Path(directory)
            cc = shlex.split(os.environ.get("CC", "cc"))
            cxx = shlex.split(os.environ.get("CXX", "c++"))
            flags = ["-O2", "-Wall", "-Wextra", "-Werror"]
            includes = ["-Itests/dashboard_sdk", "-Isrc/common", "-Isrc/ipc", "-Isrc/workers", "-Isrc/plugins"]
            objects = []
            for source in ("gauge_core", "launch_timer"):
                obj = output / (source + ".o")
                subprocess.run(cc + flags + includes + ["-std=c99", "-c", "src/common/" + source + ".c", "-o", str(obj)],
                               cwd=NATIVE, check=True, timeout=30)
                objects.append(str(obj))
            for name, (action, visible, hud) in PLUGINS.items():
                with self.subTest(plugin=name):
                    target = output / name
                    definitions = ['-DID6_PID_SOURCE="../src/plugins/' + name + '.cpp"',
                                   "-DID6_PID_ACTION=" + str(action), "-DID6_PID_VISIBLE=" + str(visible),
                                   "-DID6_PID_HUD=" + str(hud)]
                    subprocess.run(cxx + flags + includes + ["-std=gnu++98"] + definitions +
                                   ["tests/test_worker_pid.cpp"] + objects + ["-lm", "-o", str(target)],
                                   cwd=NATIVE, check=True, timeout=60)
                    subprocess.run([str(target)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
