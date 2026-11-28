#!/bin/bash
# Runs a command in the QEMU image AA-EntryNav uses (aa-entrynav-qemu:local: qemu-arm, the ARM binutils, python3), with
# the project on /work and the copy of the car's system on /sysroot, read-only. HU_ROOTFS as in tc.sh.
#   bash toolchain/qemu.sh python3 tests/run_arm.py
# Under Git Bash, docker needs the Windows form of the path (pwd -W), and no rewriting of the container paths.
project="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
export MSYS_NO_PATHCONV=1
exec docker run --rm -v "$project:/work" -v "${HU_ROOTFS:-entrynav2-rootfs}:/sysroot:ro" -w /work \
    aa-entrynav-qemu:local "$@"
