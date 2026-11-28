"""A pretend EntryNav2 made from the copy of the car's system, entered with chroot (after bmw-fixes' tests/headunit.py).

Runs as root inside the disposable aa-entrynav-qemu container, started with --privileged and the copy at /sysroot
(tests/run_setup.py does both). The car's ARM programs run through the kernel's binfmt registration for QEMU, so every
command setup.sh runs is the car's own: its bash 3.2 as /bin/sh, its BusyBox 1.21, its util-linux mount, its loader.

What it has, as on the car:
  /                an ext4 image on a loop device, mounted read-only: the system partition, made writable with
                   "mount -o remount,rw /"
  /var/local       a separate tmpfs, noexec: the data partition
  /tmp, /run, /proc, /dev
and the car's bin, sbin, lib, etc, usr and /opt/hmi/lib, with an empty /opt/hmi/data/ro/Plugins. /usr/sbin/iptables is
a recorder (/run/iptables-rules): the car's xtables-multi doesn't work under QEMU's user mode.
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

SYSROOT = Path("/sysroot")
TEMPLATE = Path("/var/tmp/unit-template")
IMAGE = Path("/var/tmp/unit-template.img")
COPIED = ("bin", "sbin", "lib", "etc", "usr", "opt/hmi/lib")
FAKE_IPTABLES = """#!/bin/sh
# Test recorder: keeps the rules as lines of /run/iptables-rules and answers -C, -A and -D like iptables.
f=/run/iptables-rules
op=$1
shift
rule="$*"
touch $f
case $op in
    -C) grep -qxF -- "$rule" $f ;;
    -A) echo "$rule" >>$f ;;
    -D) grep -qxF -- "$rule" $f || exit 1; grep -vxF -- "$rule" $f >$f.new; mv $f.new $f ;;
    *) exit 2 ;;
esac
"""


def run(*args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kwargs)


def template() -> Path:
    """The car's files in an ext4 image, made once per test run; each unit starts from a copy of it."""
    if not IMAGE.exists():
        TEMPLATE.mkdir(parents=True, exist_ok=True)
        run("truncate", "-s", "700M", str(IMAGE))
        run("mkfs.ext4", "-q", "-F", str(IMAGE))
        run("mount", "-o", "loop", str(IMAGE), str(TEMPLATE))
        try:
            for name in COPIED:
                (TEMPLATE / name).parent.mkdir(parents=True, exist_ok=True)
                run("cp", "-a", str(SYSROOT / name), str(TEMPLATE / name))
            (TEMPLATE / "opt/hmi/data/ro/Plugins").mkdir(parents=True)
            for name in ("proc", "tmp", "dev", "run", "var/local", "root"):
                (TEMPLATE / name).mkdir(parents=True, exist_ok=True)
            iptables = TEMPLATE / "usr/sbin/iptables"
            iptables.unlink()
            iptables.write_text(FAKE_IPTABLES)
            iptables.chmod(0o755)
        finally:
            run("umount", str(TEMPLATE))
    return IMAGE


class Unit:
    def __init__(self):
        self.base = Path(tempfile.mkdtemp(prefix="unit-", dir="/var/tmp"))
        self.image = Path(f"{self.base}.img")
        run("cp", "--sparse=always", str(template()), str(self.image))
        run("mount", "-o", "loop", str(self.image), str(self.base))
        run("mount", "-t", "proc", "proc", str(self.base / "proc"))
        run("mount", "-t", "tmpfs", "tmpfs", str(self.base / "tmp"))
        run("mount", "-t", "tmpfs", "tmpfs", str(self.base / "run"))
        run("mount", "--bind", "/dev", str(self.base / "dev"))
        run("mount", "-t", "tmpfs", "-o", "noexec", "tmpfs", str(self.base / "var/local"))
        (self.base / "var/local/data").mkdir()
        self.read_only()
        self.holders = []

    # ---- the unit's state, seen from outside ---------------------------------------------------------------------
    def path(self, name: str) -> Path:
        return self.base / name.lstrip("/")

    def writable(self):
        run("mount", "-o", "remount,rw", str(self.base))

    def read_only(self):
        run("mount", "-o", "remount,ro", str(self.base))

    def root_read_only(self) -> bool:
        options = self.sh("grep ' / ' /proc/mounts | sed -n '$p'").stdout.split()
        return "ro" in options[3].split(",")

    def put(self, name: str, data, mode: int = 0o644):
        """Writes a file of the system partition, as the firmware would have it."""
        self.writable()
        try:
            target = self.path(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data.encode() if isinstance(data, str) else data)
            target.chmod(mode)
        finally:
            self.read_only()

    def rules(self) -> list:
        path = self.path("run/iptables-rules")
        return path.read_text().splitlines() if path.exists() else []

    # ---- running inside ------------------------------------------------------------------------------------------
    def sh(self, script: str, timeout: int = 300):
        env = {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "HOME": "/root", "LANG": "C"}
        return subprocess.run(["chroot", str(self.base), "/bin/sh", "-c", script], capture_output=True, text=True,
                              env=env, timeout=timeout)

    def hold(self, path: str):
        """A program inside the unit that keeps a file open, as HmiMain3D keeps an app's library mapped."""
        holder = subprocess.Popen(["chroot", str(self.base), "/bin/sh", "-c", f"exec 3<'{path}'; while :; do sleep 1; done"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        self.holders.append(holder)
        return holder

    def release(self):
        for holder in self.holders:
            os.killpg(holder.pid, 9)
            holder.wait()
        self.holders = []

    def close(self):
        self.release()
        for proc in Path("/proc").iterdir():
            if proc.name.isdigit():
                try:
                    if os.readlink(proc / "root") == str(self.base):
                        os.kill(int(proc.name), 9)
                except OSError:
                    pass
        subprocess.run(["umount", "-R", str(self.base)], capture_output=True)
        shutil.rmtree(self.base, ignore_errors=True)
        self.image.unlink(missing_ok=True)
