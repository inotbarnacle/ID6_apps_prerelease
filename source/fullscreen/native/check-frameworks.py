"""Read-only check of required BMW framework symbols for this project."""
import argparse
from hashlib import sha256
import json
from pathlib import Path
import re

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
MODULES = tuple(line.split()[0] for line in (ROOT / 'native/generated/modules.txt').read_text().splitlines())
FRAMEWORKS = ('libAppFramework.A.so', 'libCoreFramework.A.so')
BMW_SYMBOL = re.compile(r"^_Z.*3bmw")


def inspect(path):
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM':
            raise ValueError(f'{path.name} needs to be a little-endian ARM ELF32')
        table = elf.get_section_by_name('.dynsym')
        if table is None:
            raise ValueError(f'{path.name} has no dynamic symbol table')
        return [(item.name, item['st_shndx'], item['st_info']['bind'], item['st_other']['visibility'])
                for item in table.iter_symbols() if item.name]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('framework_directory', type=Path)
    arguments = parser.parse_args()
    exports, hashes, missing_by_module = set(), {}, {}
    for filename in FRAMEWORKS:
        path = arguments.framework_directory / filename
        hashes[filename] = sha256(path.read_bytes()).hexdigest()
        exports |= {name for name, section, binding, visibility in inspect(path)
                    if section != 'SHN_UNDEF' and binding in ('STB_GLOBAL', 'STB_WEAK')
                    and visibility in ('STV_DEFAULT', 'STV_PROTECTED')}
    for name in MODULES:
        needed = {symbol for symbol, section, binding, _ in inspect(ROOT / 'native/out' / name / f'lib{name}.so')
                  if section == 'SHN_UNDEF' and binding != 'STB_WEAK' and BMW_SYMBOL.search(symbol)}
        if not needed:
            raise ValueError(f'{name} did not expose any required BMW framework symbols')
        missing_by_module[name] = sorted(needed - exports)
    print(json.dumps({
        'framework_sha256': hashes,
        'missing_required_bmw_symbols': missing_by_module,
        'status': 'missing symbols' if any(missing_by_module.values()) else 'static symbol check passed',
        'runtime': 'unverified by this check',
    }, indent=2))
    return 1 if any(missing_by_module.values()) else 0


if __name__ == '__main__':
    raise SystemExit(main())
