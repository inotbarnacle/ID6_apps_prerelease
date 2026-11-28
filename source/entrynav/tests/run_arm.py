#!/usr/bin/env python3
"""The ARM checks, with the car's own libraries under QEMU. Run in the QEMU image after the ARM build:

    bash toolchain/tc.sh make arm
    bash toolchain/qemu.sh python3 tests/run_arm.py
"""

import hashlib
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SYSROOT = "/sysroot"
PROFILE = Path(os.environ.get("ENTRYNAV_PROFILE_DIR", ROOT / "install"))
QEMU = ["qemu-arm", "-L", SYSROOT, "-E", "LD_LIBRARY_PATH=/opt/hmi/lib:/usr/local/lib:/usr/lib:/lib", "-E",
        "LD_BIND_NOW=1"]
ARM = ROOT / "build-arm"
PLUGINS = [("libBoostDeck.so", "BoostDeck", "_ZTV9BoostDeck"), ("libShiftDeck.so", "ShiftDeck", "_ZTV9ShiftDeck"),
           ("libBm3Maps.so", "Bm3Maps", "_ZTV7Bm3Maps"), ("libMhdMaps.so", "MhdMaps", "_ZTV7MhdMaps"),
           ("libEngineCodes.so", "EngineCodes", "_ZTV11EngineCodes")]

PLUGINS += [("libWarmDeck.so", "WarmDeck", "_ZTV8WarmDeck"),
            ("libLaunchDeck.so", "LaunchDeck", "_ZTV10LaunchDeck"),
            ("libHybridDeck.so", "HybridDeck", "_ZTV10HybridDeck"),
            ("libRoadJet.so", "RoadJet", "_ZTV7RoadJet"),
            ("libBEAMer.so", "BEAMer", "_ZTV9LightShow"),
            ("libXDriveControl.so", "XDriveControl", "_ZTV16XDriveControlApp"),
            ("libMgFlasher.so", "MgFlasher", "_ZTV9MgFlasher")]


def run(name, command, expect="PASS", code=0):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
    out = result.stdout + result.stderr
    if result.returncode != code or expect not in out:
        raise SystemExit(f"FAIL {name} (exit {result.returncode})\n{out}")
    print(out.strip() if expect == "PASS" else f"PASS {name}", flush=True)


def frameworks_match():
    """install/frameworks.md5, which setup.sh check compares with the car, must name the libraries checked here."""
    for line in (PROFILE / "frameworks.md5").read_text().splitlines():
        digest, path = line.split("  ")
        actual = hashlib.md5((Path(SYSROOT) / path.lstrip("/")).read_bytes()).hexdigest()
        if actual != digest:
            raise SystemExit(f"FAIL install/frameworks.md5: {path} is {actual} in the copy of the car, not {digest}")
    print("PASS install/frameworks.md5 matches the copy of the car these checks use", flush=True)


def main():
    frameworks_match()
    for lib, name, _ in PLUGINS:
        run(f"load {name}", [*QEMU, str(ARM / "test-plugin-load"), str(ARM / lib), name])
    run("vtables", [sys.executable, str(ROOT / "tests/check_vtable.py"), SYSROOT,
                    *[f"{ARM / lib}:{vtable}" for lib, _, vtable in PLUGINS]])
    run("worker spawn", [*QEMU, str(ARM / "test-spawn"), "--hu-glibc"])
    run("bm3 markers and requests", [*QEMU, str(ARM / "test-bm3")], "passed")
    # No gateway under QEMU: the map worker must report it and exit 1 without hanging.
    run("bm3-maps without a gateway", [*QEMU, str(ARM / "bm3-maps"), "--host", "127.0.0.1", "--port", "1", "--status"],
        "Diagnostic gateway unavailable", 1)
    run("mhd-maps without a gateway", [*QEMU, str(ARM / "mhd-maps"), "--host", "127.0.0.1", "--port", "1", "--map", "1"],
        "Diagnostic gateway unavailable", 1)
    run("engine-codes without a gateway", [*QEMU, str(ARM / "engine-codes"), "--host", "127.0.0.1", "--port", "1", "--codes"],
        "Diagnostic gateway unavailable", 0)
    # The workers start and parse their arguments with the car's C library (demo mode sends to a local UDP port).
    run("boostdeck-collector demo", [*QEMU, str(ARM / "boostdeck-collector"), "--demo", "--cycles", "2"], "")
    # Nothing answers under QEMU: discovery must time out cleanly with exit 1.
    run("ena-gateway discover", [*QEMU, str(ARM / "ena-gateway"), "discover", "--to", "127.0.0.1", "--wait", "200"],
        "", 1)
    print("PASS all ARM checks")


if __name__ == "__main__":
    main()
