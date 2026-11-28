#!/bin/bash
# Runs a command in the ARM build image (entrynav2-tc:4.7, GCC 4.7 armhf, the same image AA-EntryNav builds with), with
# the project on /work and a copy of the car's system on /sysroot, read-only. HU_ROOTFS names the copy: a Docker volume
# or a folder. It defaults to the volume entrynav2-rootfs. The copy never goes into git.
#   bash toolchain/tc.sh make arm
# Under Git Bash, docker needs the Windows form of the path (pwd -W), and no rewriting of the container paths.
project="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
export MSYS_NO_PATHCONV=1
exec docker run --rm -u "$(id -u):$(id -g)" -v "$project:/work" -v "${HU_ROOTFS:-entrynav2-rootfs}:/sysroot:ro" \
    -w /work entrynav2-tc:4.7 "$@"
