"""Assemble an offline N57 candidate ZIP; never connects to a vehicle."""
import argparse
from io import BytesIO
import hashlib
import json
from pathlib import Path
import stat
import struct
import subprocess
import xml.etree.ElementTree as ET
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / "source/native"

# Unrelated tuning, lighting and game menus are not part of the N57 bundle.
APPS = {
    "BoostDeck": ("core", "dashboard", "boostdeck-collector-qnx", 100),
    "ShiftDeck": ("core", "dashboard", "shift-deck-qnx", 104),
    "LaunchDeck": ("core", "launchdeck", "shift-deck-qnx", 120),
    "EngineCodes": ("core", "dashboard", "engine-codes-qnx", 102),
    "PowerDeck": ("core", None, "boostdeck-collector-qnx", 121),
    "XDriveControl": ("optional", "xdrive", "xdrive-control-qnx", 112),
}


def app_resources(app):
    """Use regenerated resources when present, retaining the upstream layout."""
    _, package, _, _ = APPS[app]
    files = {}
    if package:
        with ZipFile(ROOT / "packages" / (package + ".zip")) as upstream:
            files = {Path(n).name: upstream.read(n) for n in upstream.namelist()
                     if n.startswith(app + "/") and len(Path(n).parts) == 2}
    for suffix in ("manifest", "xml"):
        source = NATIVE / "manifests" / (app + "." + suffix)
        if source.exists():
            files[source.name] = source.read_bytes()
    archive = app + "_ALL_Images.zip"
    generated = NATIVE / "manifests" / archive
    if generated.is_file():
        files[archive] = generated.read_bytes()
    elif app == "PowerDeck":
        import make_n57_dashboards
        bitmaps = list(make_n57_dashboards.own_bitmaps(app))
        import power_graph_art
        bitmaps += list(power_graph_art.bitmaps())
        output = BytesIO()
        with ZipFile(output, "w", ZIP_DEFLATED) as images:
            for identifier, bitmap in sorted(bitmaps):
                png = BytesIO()
                bitmap.save(png, format="PNG", optimize=True)
                image = ZipInfo(str(identifier) + ".png", date_time=(2026, 1, 1, 0, 0, 0))
                images.writestr(image, png.getvalue(), compress_type=ZIP_DEFLATED)
        files[archive] = output.getvalue()
    return files


def check_elf(data, shared):
    expected_type = 3 if shared else 2
    if (len(data) < 52 or data[:6] != b"\x7fELF\x01\x01" or
            struct.unpack_from("<HH", data, 16) != (expected_type, 40)):
        raise ValueError("Expected QNX ARM ELF32 " + ("library" if shared else "worker"))


def validate_app(files, app, worker, connector):
    manifest = ET.fromstring(files[app + ".manifest"])
    if (manifest.findtext("name") != app or manifest.findtext("file") != "{pluginDir}/" + app or
            manifest.findtext("config/dependencies/pluginAppConnector/id") != str(connector)):
        raise ValueError("Manifest mismatch: " + app)
    params = manifest.find("config/parameters")
    for entry in params:
        if not entry.text.startswith("{pluginDir}/") or entry.text.removeprefix("{pluginDir}/") not in files:
            raise ValueError("Missing manifest resource: " + str(entry.text))
    xml = ET.fromstring(files[app + ".xml"])
    for section in ("actions", "models", "hmiStates"):
        entries = xml.find("pluginApp/" + section)
        identifiers = [x.get("id") for x in entries]
        if len(identifiers) != len(set(identifiers)):
            raise ValueError("Duplicate " + section + " ID: " + app)
    archive = params.findtext("ImageDBFile")
    if archive:
        with ZipFile(BytesIO(files[archive.removeprefix("{pluginDir}/")])) as images:
            names = {Path(n).stem for n in images.namelist()}
        for image in xml.findall("pluginApp/models/imageIdModel"):
            if image.get("imageId") not in names:
                raise ValueError("Missing image: " + app + "/" + str(image.get("imageId")))
    check_elf(files["lib" + app + ".so"], True)
    check_elf(files[worker], False)


def checksums(files):
    return "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n"
                   for name, data in sorted(files.items())).encode()


def verify_checksums(files):
    covered = set()
    for line in files["SHA256SUMS"].decode().splitlines():
        digest, name = line.split("  ", 1)
        if hashlib.sha256(files[name]).hexdigest() != digest:
            raise ValueError("Checksum mismatch: " + name)
        covered.add(name)
    if covered != set(files) - {"SHA256SUMS"}:
        raise ValueError("Incomplete checksum coverage")


def verify_zip(path):
    with ZipFile(path) as z:
        if z.testzip() is not None:
            raise ValueError("ZIP CRC check failed")
        names = z.namelist()
        if len(names) != len(set(names)) or any(Path(n).is_absolute() or ".." in Path(n).parts for n in names):
            raise ValueError("Unsafe or duplicate ZIP path")
        roots = {n.split("/", 1)[0] for n in names}
        if len(roots) != 1:
            raise ValueError("Expected one bundle root")
        prefix = roots.pop() + "/"
        app_dirs = {tuple(Path(n[len(prefix):]).parts[:2]) for n in names
                    if Path(n[len(prefix):]).parts[:1] in (("core",), ("optional",))}
        if app_dirs != {(group, app) for app, (group, _, _, _) in APPS.items()}:
            raise ValueError("Unexpected application inventory")
        verify_checksums({n[len(prefix):]: z.read(n) for n in names})
        for app, (group, _, worker, connector) in APPS.items():
            base = prefix + group + "/" + app + "/"
            files = {n[len(base):]: z.read(n) for n in names if n.startswith(base)}
            verify_checksums(files)
            validate_app(files, app, worker, connector)
            if not (z.getinfo(base + worker).external_attr >> 16) & stat.S_IXUSR:
                raise ValueError("Worker is not executable: " + app)
        forbidden = {"xdrive-owner-vin.txt", "gateway-ip.txt", "n57-sgbd.txt",
                     "libAppFramework.A.so", "libCoreFramework.A.so"}
        if any(Path(n).name in forbidden for n in names):
            raise ValueError("Private library or active vehicle setting in ZIP")


def assemble(output, binaries, build_info):
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip():
        raise ValueError("Commit source changes before labelling a package")
    info = json.loads(build_info.read_text())
    native_tree = subprocess.check_output(["git", "rev-parse", "HEAD:source/native"], cwd=ROOT, text=True).strip()
    if info["source_commit"] != commit or info["native_tree"] != native_tree or info["car_access"] is not False:
        raise ValueError("Build report must describe the current offline source commit")
    label = "iDriveDeck-N57-" + commit[:7] + "-local-candidate"
    output.mkdir(parents=True, exist_ok=True)
    dest = output / (label + ".zip")
    if dest.exists():
        raise FileExistsError(dest)
    payload = {}
    executable = set()
    for app, (group, _, worker, connector) in APPS.items():
        files = app_resources(app)
        for name in ("lib" + app + ".so", worker):
            files[name] = (binaries / name).read_bytes()
            if hashlib.sha256(files[name]).hexdigest() != info["binaries"][name]:
                raise ValueError("Binary differs from build report: " + name)
        files.pop("SHA256SUMS", None)
        validate_app(files, app, worker, connector)
        files["SHA256SUMS"] = checksums(files)
        base = group + "/" + app + "/"
        payload.update({base + n: data for n, data in files.items()})
        executable.add(base + worker)
    payload["INSTALL.md"] = (ROOT / "source/docs/N57.md").read_bytes()
    for doc in ("N57.md", "MOST.md", "BUILD.md", "PORTING.md", "SIGNALS.md"):
        payload["docs/" + doc] = (ROOT / "source/docs" / doc).read_bytes()
    for name in ("LICENSE", "NOTICE"):
        payload["licenses/nbtevo-diag-" + name] = (ROOT / "source/third_party/nbtevo-diag" / name).read_bytes()
    payload["licenses/iDriveDeck-LICENSE"] = (ROOT / "source/LICENSE").read_bytes()
    payload["config/n57-sgbd.txt.example"] = b"0F1F70\n"
    payload["BUILD-INFO.json"] = json.dumps(info, indent=2).encode() + b"\n"
    payload["SHA256SUMS"] = checksums(payload)
    with ZipFile(dest, "x", ZIP_DEFLATED) as z:
        for name, data in sorted(payload.items()):
            entry = ZipInfo(label + "/" + name)
            entry.create_system = 3
            entry.external_attr = (stat.S_IFREG | (0o755 if name in executable else 0o644)) << 16
            z.writestr(entry, data, compress_type=ZIP_DEFLATED)
    verify_zip(dest)
    digest = hashlib.sha256(dest.read_bytes()).hexdigest()
    dest.with_suffix(".zip.sha256").write_text(digest + "  " + dest.name + "\n")
    return dest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binaries", type=Path, default=NATIVE / "out")
    parser.add_argument("--build-info", type=Path, required=True)
    args = parser.parse_args()
    print(assemble(args.output, args.binaries, args.build_info))
