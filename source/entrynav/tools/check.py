#!/usr/bin/env python3
"""Every check of EntryNav-Apps, in the order a release needs them. There is no CI: tools/publish_release.py runs this.

    python3 tools/check.py

1. The ARM build, in the build image, against the copy of the car (toolchain/tc.sh make arm).
2. The ARM checks under QEMU with the car's libraries (tests/run_arm.py).
3. The host tests (make test), in WSL on Windows.
4. The release archives (tools/package_release.py). The screens' image archives must be there (make assets).
5. setup.sh and Flare Drive's scripts on a pretend head unit (tests/run_setup.py).
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The bash on PATH: on Windows a bare "bash" finds System32's WSL launcher before PATH, and WSL may have no Docker.
BASH = shutil.which("bash") or "bash"


def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)


def main():
    missing = [app for app in ("BoostDeck", "ShiftDeck", "Bm3Maps", "MhdMaps", "EngineCodes") if not (ROOT / f"plugin/{app}_ALL_Images.zip").is_file()]
    if missing:
        sys.exit(f"FAIL: plugin/{missing[0]}_ALL_Images.zip is missing. Run make assets (Python with Pillow) first.")
    run(BASH, "toolchain/tc.sh", "make", "arm")
    run(BASH, "toolchain/qemu.sh", "python3", "tests/run_arm.py")
    if os.name == "nt":
        run("wsl", "-e", "sh", "-c", f'cd "$(wslpath "{ROOT}")" && make test')
    else:
        run("make", "test")
    run(sys.executable, "tools/package_release.py")
    run(sys.executable, "tests/run_setup.py")
    print("PASS every check")


if __name__ == "__main__":
    main()
