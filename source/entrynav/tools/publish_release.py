#!/usr/bin/env python3
"""Publish EntryNav-Apps releases for Flare Drive to download, one feed per plugin:
https://updates.darkzone.dev/boostdeck/, /shiftdeck/, /bm3-maps/, /mhd-maps/ and /engine-codes/

  uv run --no-project --with cryptography python tools/publish_release.py init [ID...]      an empty signed index, once
  uv run --no-project --with cryptography python tools/publish_release.py verify [ID...]    check the live indexes
  uv run --no-project --with cryptography python tools/publish_release.py publish [--stable] [--experimental ID...]
  uv run --no-project --with cryptography python tools/publish_release.py stability ID VERSION stable|experimental
  uv run --no-project --with cryptography python tools/publish_release.py retire ID VERSION...   refused, see below

Without IDs, init and verify cover boostdeck, shiftdeck, bm3-maps, mhd-maps and engine-codes.

The repo has one version, VERSION, for every app. `publish` releases every app at it, so the apps never drift apart:
a change to one app raises VERSION, and the others ship again unchanged. Apps already published at VERSION are left
out, so a publish that stopped part way can run again. Their archives are downloaded and checked against the catalog
to finish the GitHub Release, even when every app is already published. It then tags v<version> at HEAD and pushes the tag (an existing
tag is never moved, and once one exists nothing an archive is built from may change), and puts the new archives and
their .sha256 files on the tag's GitHub Release, creating it if needed, with VERSION's CHANGELOG.md section as text.

Releases reach Flare Drive through its plugin catalog, which only the publishing Worker writes. `publish` sends each
plugin's archive there with flare-publish (Flare Drive's tools/flare_publish.py, in its repo beside this one, or
FLARE_DRIVE_REPO), with plugins/<id>.toml as its manifest. The Worker checks the archive, stores it, adds it to the
catalog and signs it; flare-publish then rebuilds that plugin's per-plugin list (<id>/index.json) from the catalog and
signs it with the release key (~/.flare-drive/release-signing-key, or FLARE_DRIVE_SIGNING_KEY), for Flare Drive 2.2.1
and earlier. Flare Drive's docs/publishing.md describes the whole process. Never upload to the bucket any other way:
Flare Drive 2.2.2 and later read only the catalog.

`publish` refuses a dirty tree, a HEAD that is not pushed and a version whose CHANGELOG.md
section lacks a plugin's Flare Drive note (<!-- Flare Drive <id>: ... -->). It runs tools/check.py first, then sends
each plugin's archive. Each release is experimental unless --stable, and --experimental keeps the apps it names
experimental; `stability` changes that later in the catalog.
`retire` is refused: the catalog would keep listing the release, and the Worker can't take one off yet.
"""

import argparse
import base64
import hashlib
import json
import os
import re
import runpy
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[1]
BUCKET = "darkzone-updates"
REPO = "CallMeLewis/EntryNav-Apps"  # gh would otherwise pick the upstream fork parent
# What the archives are built from. Once v<version> is tagged, none of it may change before the rest of the apps publish.
APP_PATHS = ("VERSION", "Makefile", "src", "plugin", "plugins", "install", "sdk", "third-party", "images", "toolchain",
             "tools/package_release.py")
BASE_URL = "https://updates.darkzone.dev"
PLUGINS = ("boostdeck", "shiftdeck", "bm3-maps", "mhd-maps", "engine-codes")
# flare-publish, which sends releases to Flare Drive's plugin catalog (docs/publishing.md in Flare Drive).
FLARE_DRIVE = Path(os.environ.get("FLARE_DRIVE_REPO", ROOT.parent / "flare-drive"))
FLARE_PUBLISH = FLARE_DRIVE / "tools" / "flare_publish.py"
WRANGLER = "wrangler@4.146.0"  # the version the Flare apps pin
# The first Flare Drive version code that knows these plugins (2.1.1 is 31). An older app never reads these feeds.
MIN_APP = 32
CHANGELOG = ROOT / "CHANGELOG.md"
VERSION = re.compile(r"^\d+\.\d+\.\d+(-(beta|test)\.\d+)?$")


def key_path() -> Path:
    return Path(os.environ.get("FLARE_DRIVE_SIGNING_KEY", Path.home() / ".flare-drive" / "release-signing-key"))


def load_key() -> Ed25519PrivateKey:
    path = key_path()
    if not path.is_file():
        sys.exit(f"FAIL: no signing key at {path}. It is AA-EntryNav's key: see its tools/publish_release.py keygen.")
    return Ed25519PrivateKey.from_private_bytes(base64.b64decode(path.read_text().strip()))


def run(*args):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)


def output(*args) -> str:
    return subprocess.run([str(a) for a in args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def flare_publish(*args):
    """Runs flare-publish with this Python, which has cryptography for the per-plugin list's signature. Stops on any
    failure, which flare-publish has already explained."""
    if not FLARE_PUBLISH.is_file():
        sys.exit(f"FAIL: no flare-publish at {FLARE_PUBLISH}. Clone Flare Drive beside this repo, or set FLARE_DRIVE_REPO.")
    if subprocess.run([sys.executable, str(FLARE_PUBLISH), *args], cwd=FLARE_DRIVE).returncode:
        sys.exit(f"FAIL: flare-publish {args[0]} failed, above.")


def live_catalog():
    """Read the authoritative catalog through the publishing Worker's authenticated API."""
    result = runpy.run_path(str(FLARE_PUBLISH))["call"]("GET", "/api/catalog")
    if not result.get("catalog") or not result.get("signatureValid"):
        sys.exit("FAIL: the live catalog is missing or its signature doesn't check.")
    return {p["id"]: p for p in result["catalog"]["plugins"]}


def wrangler_object(plugin, action, key, *options):
    npx = shutil.which("npx")
    if not npx:
        sys.exit(f"FAIL: npx not found. Install Node.js, then sign in once with: npx {WRANGLER} login")
    run(npx, "--yes", WRANGLER, "r2", "object", action, f"{BUCKET}/{plugin}/{key}", "--remote", *options)


def wrangler_put(plugin, key, file, content_type, cache_control):
    wrangler_object(plugin, "put", key, "--file", file, "--content-type", content_type, "--cache-control", cache_control)


def fetch(plugin, name):
    """The live file, or None when it does not exist yet. Bypasses Cloudflare's cache."""
    url = f"{BASE_URL}/{plugin}/{name}?t={datetime.now().timestamp()}"
    request = urllib.request.Request(url, headers={"User-Agent": "FlareDrivePublish"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.read()
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise


def live_index(key, plugin):
    """The published index after checking its signature, or None if none is published."""
    index, signature = fetch(plugin, "index.json"), fetch(plugin, "index.json.sig")
    if index is None:
        return None
    if signature is None:
        sys.exit(f"FAIL: {plugin}/index.json is published without index.json.sig. Fix the bucket by hand before publishing.")
    try:
        key.public_key().verify(base64.b64decode(signature), index)
    except InvalidSignature:
        sys.exit(f"FAIL: {plugin}'s published index is not signed by this key. Nothing was changed.")
    return json.loads(index)


def publish_index(key, plugin, index):
    data = (json.dumps(index, indent=2) + "\n").encode()
    signature = base64.b64encode(key.sign(data)).decode() + "\n"
    key.public_key().verify(base64.b64decode(signature), data)
    with tempfile.TemporaryDirectory() as folder:
        index_file, sig_file = Path(folder) / "index.json", Path(folder) / "index.json.sig"
        index_file.write_bytes(data)
        sig_file.write_text(signature)
        # The signature first: an app fetching between the two uploads rejects the pair and keeps its last index.
        wrangler_put(plugin, "index.json.sig", sig_file, "text/plain", "public, max-age=60")
        wrangler_put(plugin, "index.json", index_file, "application/json", "public, max-age=60")


def now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def flare_drive_note(version: str, plugin: str) -> str:
    if not CHANGELOG.is_file():
        sys.exit("FAIL: no CHANGELOG.md.")
    text = CHANGELOG.read_text(encoding="utf-8")
    match = re.search(rf"^## \[{re.escape(version)}\].*?(?=^## \[|\Z)", text, re.M | re.S)
    if not match:
        sys.exit(f"FAIL: CHANGELOG.md has no section for {version}.")
    note = re.search(rf"<!--\s*Flare Drive {re.escape(plugin)}:(.*?)-->", match.group(0), re.S)
    if not note or not note.group(1).strip():
        sys.exit(f"FAIL: CHANGELOG.md's {version} section has no <!-- Flare Drive {plugin}: ... --> note.")
    return " ".join(note.group(1).split())


def chosen(ids):
    unknown = sorted(set(ids) - set(PLUGINS))
    if unknown:
        sys.exit(f"FAIL: no plugin called {', '.join(unknown)}. They are {', '.join(PLUGINS)}.")
    return [p for p in PLUGINS if not ids or p in ids]


def init(args):
    key = load_key()
    for plugin in chosen(args.ids):
        if live_index(key, plugin) is not None:
            print(f"{plugin}: an index is already published; left as it is.")
            continue
        publish_index(key, plugin, {"schema": 1, "published": now(), "releases": []})
    verify(args)


def verify(args):
    catalog = live_catalog()
    for plugin in chosen(args.ids):
        index = catalog.get(plugin)
        if index is None:
            print(f"{plugin}: not in the catalog yet.")
            continue
        print(f"{plugin}: catalog signature OK")
        for release in index["releases"]:
            print(f"  {release['version']}  {release['sha256'][:12]}…  {release['size']} bytes  "
                  f"{release.get('stability') or 'no label'}  {release['published']}")
        if not index["releases"]:
            print("  (no releases yet)")


def release_text(version: str) -> str:
    """The GitHub Release's text: VERSION's CHANGELOG.md section without its heading and Flare Drive notes, each
    paragraph or bullet on one line, since GitHub shows hard line breaks, then how to install."""
    text = CHANGELOG.read_text(encoding="utf-8")
    match = re.search(rf"^## \[{re.escape(version)}\][^\n]*\n(.*?)(?=^## \[|\Z)", text, re.M | re.S)
    blocks = []  # paragraphs and bullets, unwrapped; "" for a blank line
    for line in re.sub(r"<!--.*?-->", "", match.group(1), flags=re.S).splitlines():
        line = line.strip()
        if not line or line.startswith(("- ", "#")) or not blocks or not blocks[-1]:
            blocks.append(line)
        else:
            blocks[-1] += " " + line
    body = re.sub(r"\n{3,}", "\n\n", "\n".join(blocks)).strip()
    return (f"{body}\n\nEach app is its own archive. Install it with [Flare Drive](https://github.com/CallMeLewis/flare-drive-app), or by hand with the setup.sh inside it: "
            "see Installing in the README. Each .sha256 file holds its archive's SHA-256.\n")


def tag_and_release(version, head, archives):
    """Tags v<version> at HEAD and pushes the tag, unless it is there already, then puts archives and their .sha256 files
    on its GitHub Release, which it creates if needed, and sets the Release's text from CHANGELOG.md. A failure is
    reported, never fatal: the apps are already published."""
    tag = f"v{version}"
    if not output("git", "tag", "--list", tag):
        run("git", "tag", "-a", tag, head, "-m", f"EntryNav-Apps {version}")
    if subprocess.run(["git", "push", "origin", f"refs/tags/{tag}"], cwd=ROOT, check=False).returncode:
        print(f"WARN: {tag} is tagged here but not on GitHub. Push it with: git push origin {tag}")
        return
    files = []
    for archive in archives:
        sums = archive.with_name(archive.name + ".sha256")
        sums.write_text(f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n", newline="\n")
        files += [archive, sums]
    with tempfile.TemporaryDirectory() as folder:
        notes = Path(folder) / "notes.md"
        notes.write_text(release_text(version), encoding="utf-8", newline="\n")
        if subprocess.run(["gh", "release", "view", tag, "-R", REPO], capture_output=True, check=False).returncode == 0:
            steps = [["gh", "release", "upload", tag, *files, "-R", REPO, "--clobber"],
                     ["gh", "release", "edit", tag, "-R", REPO, "--notes-file", notes]]
        else:
            steps = [["gh", "release", "create", tag, *files, "-R", REPO, "--verify-tag",
                      "--title", f"EntryNav-Apps {version}", "--notes-file", notes,
                      *(["--prerelease"] if "-" in version else [])]]
        for step in steps:
            if subprocess.run([str(a) for a in step], cwd=ROOT, check=False).returncode:
                print(f"WARN: {' '.join(str(a) for a in step[:3])} failed, above. Finish {tag}'s GitHub Release by hand.")
                return
    print(f"{tag}'s GitHub Release has {', '.join(a.name for a in archives)}.")


def publish(args):
    """Publishes every app at VERSION. Apps already published at VERSION are left out, so a publish that stopped part
    way can be run again."""
    unknown = sorted(set(args.experimental) - set(PLUGINS))
    if unknown:
        sys.exit(f"FAIL: no plugin called {', '.join(unknown)}. They are {', '.join(PLUGINS)}.")
    if output("git", "status", "--porcelain"):
        sys.exit("FAIL: the working tree has changes. Publish only what is committed.")
    head = output("git", "rev-parse", "HEAD")
    if not output("git", "branch", "-r", "--contains", head):
        sys.exit("FAIL: HEAD is not pushed. Push it first.")
    version = (ROOT / "VERSION").read_text().strip()
    if not VERSION.match(version):
        sys.exit(f"FAIL: VERSION says {version}, which is not X.Y.Z, X.Y.Z-beta.N or X.Y.Z-test.N.")
    if args.stable and "-" in version:
        sys.exit(f"FAIL: {version} is a beta or test build: it is always experimental.")
    tag = f"v{version}"
    if output("git", "tag", "--list", tag):
        # A version is one build of every app: once tagged, nothing that goes into an archive may change.
        changed = subprocess.run(["git", "diff", "--quiet", tag, head, "--", *APP_PATHS], cwd=ROOT, check=False)
        if changed.returncode:
            sys.exit(f"FAIL: {tag} is on {output('git', 'rev-parse', '--short', tag)}, and the apps have changed since. "
                     "Raise VERSION: every app ships at the new version.")
    catalog = live_catalog()
    plugins, existing = [], {}
    for plugin in PLUGINS:
        entry = next((r for r in catalog.get(plugin, {}).get("releases", []) if r["version"] == version), None)
        if entry:
            print(f"{plugin} {version} is already published; left out.", flush=True)
            existing[plugin] = entry
        else:
            plugins.append(plugin)
    notes = {plugin: flare_drive_note(version, plugin) for plugin in PLUGINS}
    for plugin, note in notes.items():
        print(f"{plugin} note: {note}", flush=True)
    out = ROOT / "dist"
    out.mkdir(exist_ok=True)
    archives = []
    if plugins:
        run(sys.executable, "tools/check.py")
        run(sys.executable, "tools/package_release.py", out, *plugins)
    # Reuse the published bytes, including on retries after upload succeeded but legacy sync failed.
    # Do this after check.py, which packages every app into dist as part of its checks.
    for plugin, entry in existing.items():
        data = fetch(plugin, entry["file"])
        if data is None or len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            sys.exit(f"FAIL: {plugin} {version}'s archive doesn't match the catalog.")
        archive = out / f"{plugin}-{version}-armhf.tar"
        archive.write_bytes(data)
        archives.append(archive)
    for plugin in plugins:
        archive = out / f"{plugin}-{version}-armhf.tar"
        data = archive.read_bytes()
        with tarfile.open(archive) as tar:
            recorded = tar.extractfile(f"{plugin}-{version}/VERSION").read().decode().strip()
            described = json.loads(tar.extractfile(f"{plugin}-{version}/plugin.json").read())
        if recorded != version or described["version"] != version or described["id"] != plugin:
            sys.exit(f"FAIL: the {plugin} archive says {described['id']} {recorded}, not {plugin} {version}.")
        label = "stable" if args.stable and plugin not in args.experimental else "experimental"
        # The Worker checks, stores and adds it to the catalog; flare-publish then rebuilds this plugin's list.
        with tempfile.TemporaryDirectory() as folder:
            notes_file = Path(folder) / "notes.txt"
            notes_file.write_text(notes[plugin], encoding="utf-8", newline="\n")
            flare_publish("release", str(archive), "--manifest", str(ROOT / "plugins" / f"{plugin}.toml"),
                          "--stability", label, "--notes-file", str(notes_file),
                          "--source-commit", head, "--min-app", str(MIN_APP))
        if fetch(plugin, f"releases/{version}/{archive.name}") != data:
            sys.exit(f"FAIL: the published {plugin} archive doesn't read back as {archive.name}. Check with flare-publish verify.")
        print(f"Published {plugin} {version} ({label}, {hashlib.sha256(data).hexdigest()[:12]}…) from {head[:7]}.", flush=True)
        archives.append(archive)
    tag_and_release(version, head, archives)
    args.ids = []
    verify(args)


def stability(args):
    index = live_catalog().get(chosen([args.id])[0])
    entries = [r for r in (index or {}).get("releases", []) if r["version"] == args.version]
    if not entries:
        sys.exit(f"FAIL: {args.id} {args.version} is not published.")
    if args.label == "stable" and "-" in args.version:
        sys.exit(f"FAIL: {args.version} is a beta or test build: it stays experimental.")
    # Through the catalog, then the per-plugin list rebuilt from it.
    flare_publish("edit-release", args.id, args.version, "--stability", args.label)
    args.ids = [args.id]
    verify(args)


def retire(args):
    """Refuses: releases come from Flare Drive's catalog, which would keep listing a release taken off this list, and
    its next rebuild of the list would put it back. Flare Drive's docs/publishing.md says what to do instead."""
    sys.exit(
        "FAIL: retire isn't available now releases come from Flare Drive's catalog, which would keep listing "
        f"{args.id} {', '.join(args.versions)}. See docs/publishing.md in Flare Drive. Nothing was changed."
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, func in (("init", init), ("verify", verify)):
        p = commands.add_parser(name)
        p.add_argument("ids", nargs="*")
        p.set_defaults(func=func)
    p = commands.add_parser("publish")
    p.add_argument("--stable", action="store_true", help="label the apps stable in Flare Drive (default: experimental)")
    p.add_argument("--experimental", nargs="+", default=[], metavar="ID", help="apps that stay experimental with --stable")
    p.set_defaults(func=publish)
    p = commands.add_parser("stability")
    p.add_argument("id")
    p.add_argument("version")
    p.add_argument("label", choices=["stable", "experimental"])
    p.set_defaults(func=stability)
    p = commands.add_parser("retire")
    p.add_argument("id")
    p.add_argument("versions", nargs="+")
    p.add_argument("--delete-archives", action="store_true")
    p.set_defaults(func=retire)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
