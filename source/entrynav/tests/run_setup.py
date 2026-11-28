#!/usr/bin/env python3
"""Runs the tests on a pretend EntryNav2 made from the copy of the car's system: tests/test_setup.py (each plugin's
setup.sh), then tests/test_flare_drive.py (the scripts Flare Drive sends).

    python3 tools/package_release.py && python3 tests/run_setup.py [test_setup|test_flare_drive] [TestCase.test_name ...]

The tests run in the disposable aa-entrynav-qemu:local container, started with --privileged so that they can mount
and chroot, with the copy of the car's system from the Docker volume entrynav2-rootfs (HU_ROOTFS to use another).
Flare Drive's scripts come from its core/build/head-unit-scripts/entrynav-apps, written by its `gradlew :core:test`, when
the flare-drive repository sits beside this one; test_flare_drive is skipped without them.
"""

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUITES = ("test_setup", "test_flare_drive")
FLARE_DRIVE_SCRIPTS = ROOT.parent / "flare-drive" / "core" / "build" / "head-unit-scripts" / "entrynav-apps"


def main(args):
    suites = [args[0]] if args and args[0] in SUITES else list(SUITES)
    cases = args[1:] if args and args[0] in SUITES else args
    script = " && ".join(f"python3 /work/tests/{suite}.py {' '.join(cases)}".strip() for suite in suites)
    command = ["docker", "run", "--rm", "--privileged",
               "-v", f"{os.environ.get('HU_ROOTFS', 'entrynav2-rootfs')}:/sysroot:ro",
               "-v", f"{ROOT}:/work:ro",
               *(["-v", f"{FLARE_DRIVE_SCRIPTS}:/scripts:ro"] if FLARE_DRIVE_SCRIPTS.is_dir() else []),
               "-e", "PYTHONDONTWRITEBYTECODE=1", "-w", "/work/tests",
               "aa-entrynav-qemu:local", "sh", "-c", script]
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    return subprocess.run(command, env=env).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
