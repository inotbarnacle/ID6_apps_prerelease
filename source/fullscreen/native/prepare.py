"""Stage separate ID6 Lab plugin folders for a measured display."""
import argparse
from pathlib import Path
from shutil import copy2, copytree
import sys

ROOT = Path(__file__).resolve().parent / 'out'
MODULES = tuple(line.split()[0] for line in (ROOT.parent / 'generated/modules.txt').read_text().splitlines())
PANELS = ('800x480', '960x480', '1280x480', '1440x540', '1920x720')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('panel', choices=PANELS)
    parser.add_argument('output', type=Path, help='a new staging directory')
    parser.add_argument('--gateway', help='optional IPv4 diagnostic gateway override')
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        parser.error('the staging directory must be new')
    if args.gateway:
        from ipaddress import IPv4Address
        try:
            gateway = IPv4Address(args.gateway)
        except ValueError:
            parser.error('use an IPv4 address for the gateway')
        if gateway.is_loopback or gateway.is_multicast or gateway.is_unspecified:
            parser.error('use a valid remote IPv4 address for the gateway')
    for required in ('id6-library-fullscreen-qnx', 'library-usb-update-qnx', 'update-key.der'):
        if not (ROOT / MODULES[0] / required).is_file():
            parser.error(f'Missing {required}. Build the Library package first.')
    compact = args.panel in PANELS[:2]
    source = ROOT / 'layouts' / args.panel
    for name in MODULES:
        if not (ROOT / name / f'lib{name}.so').is_file():
            parser.error(f'Missing built plugin {name}')
        if compact and not (source / f'{name}.xml').is_file():
            parser.error(f'Missing compact layout for {name}')
    args.output.mkdir()
    for name in MODULES:
        copytree(ROOT / name, args.output / name)
        if compact:
            copy2(source / f'{name}.xml', args.output / name / f'{name}.xml')
    (args.output / MODULES[0] / "screen-size.txt").write_text(args.panel.replace("x", " ") + "\n")
    (args.output / MODULES[0] / "panel.txt").write_text(args.panel + "\n")
    if args.gateway:
        (args.output / MODULES[0] / 'gateway-ip.txt').write_text(f'{gateway}\n')
    print(f'Staged the full-screen apps for {args.panel}: {args.output}')


if __name__ == '__main__':
    sys.exit(main())
