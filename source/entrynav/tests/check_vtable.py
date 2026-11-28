#!/usr/bin/env python3
"""Compare each plugin's primary vtable with the car's bmw::PluginApp, slot by slot, without running anything.

sdk/bmw_list.h declares bmw::PluginApp's 34 virtuals in the order of the NBT Evo's library. A subclass's vtable takes
that order from the header, and the car's HmiMain3D calls slots by number, so a header that differs from the car's
libAppFramework would call the wrong function. For every slot this reads which function the plugin's vtable points at
(from its relocations) and which one the car's _ZTVN3bmw9PluginAppE points at, and requires either the same function
(inherited) or the plugin's own function with the same name and parameters (overridden).

Run where the ARM binutils are (the QEMU image): check_vtable.py SYSROOT build-arm/libX.so:_ZTV9ClassName ...
"""

import re
import subprocess
import sys

TOOL = "arm-linux-gnueabihf-"
SLOTS = 34
BASE_VTABLE = "_ZTVN3bmw9PluginAppE"


def tool(name, *args):
    return subprocess.run([TOOL + name, *args], capture_output=True, text=True, check=True).stdout


class Elf:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        self.by_name, self.by_addr = {}, {}
        for line in tool("readelf", "-sW", "--dyn-syms", path).splitlines() + tool("readelf", "-sW", path).splitlines():
            f = line.split()
            if len(f) >= 8 and f[0].endswith(":") and f[3] in ("FUNC", "OBJECT") and f[6] != "UND":
                addr, size = int(f[1], 16), int(f[2])
                self.by_name.setdefault(f[7], (addr, size))
                if f[3] == "FUNC":
                    self.by_addr.setdefault(addr & ~1, f[7])
        self.relocs = {}
        for line in tool("readelf", "-rW", path).splitlines():
            m = re.match(r"^([0-9a-f]{8})\s+[0-9a-f]{8}\s+(R_ARM_\w+)\s*(?:[0-9a-f]{8}\s+(\S+))?", line)
            if m:
                self.relocs[int(m.group(1), 16)] = (m.group(2), m.group(3))
        self.loads = []
        for line in tool("readelf", "-lW", path).splitlines():
            f = line.split()
            if f and f[0] == "LOAD":
                self.loads.append((int(f[1], 16), int(f[2], 16), int(f[4], 16)))

    def word(self, vaddr):
        for offset, start, filesz in self.loads:
            if start <= vaddr < start + filesz:
                at = offset + vaddr - start
                return int.from_bytes(self.data[at:at + 4], "little")
        raise SystemExit(f"FAIL {self.path}: 0x{vaddr:x} is not in the file")

    def slots(self, vtable):
        if vtable not in self.by_name:
            raise SystemExit(f"FAIL {self.path}: no symbol {vtable}")
        addr, size = self.by_name[vtable]
        if size < (2 + SLOTS) * 4:
            raise SystemExit(f"FAIL {self.path}: {vtable} is {size} bytes, too small for {SLOTS} slots")
        out = []
        for i in range(SLOTS):
            at = addr + 8 + 4 * i  # past offset-to-top and typeinfo
            kind, name = self.relocs.get(at, (None, None))
            if kind in ("R_ARM_ABS32", "R_ARM_GLOB_DAT") and name:
                out.append(name.split("@")[0])
            elif kind == "R_ARM_RELATIVE":
                target = self.word(at) & ~1
                out.append(self.by_addr.get(target, f"0x{target:x}"))
            else:
                raise SystemExit(f"FAIL {self.path}: {vtable} slot {i} has no relocation ({kind})")
        return out


def method(symbol):
    """'bmw::PluginApp::onInit()' -> 'onInit()'; destructors -> '~'."""
    text = tool("c++filt", symbol).strip()
    head, paren, params = text.partition("(")
    last = head.split("::")[-1]
    return "~" if last.startswith("~") else last + paren + params


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    car = Elf(argv[1].rstrip("/") + "/opt/hmi/lib/libAppFramework.A.so").slots(BASE_VTABLE)
    for spec in argv[2:]:
        path, vtable = spec.split(":")
        ours = Elf(path).slots(vtable)
        overridden = []
        for i, (theirs, mine) in enumerate(zip(car, ours)):
            if mine == theirs:
                continue
            if method(mine) != method(theirs):
                raise SystemExit(f"FAIL {path} slot {i}: the car calls {tool('c++filt', theirs).strip()}, "
                                 f"the plugin has {tool('c++filt', mine).strip()}")
            overridden.append(f"{i}:{method(mine).split('(')[0]}")
        print(f"PASS {path}: {SLOTS} slots match the car's bmw::PluginApp (overrides {', '.join(overridden)})")


if __name__ == "__main__":
    main(sys.argv)
