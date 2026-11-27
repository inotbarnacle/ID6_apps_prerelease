#!/bin/sh
set -eu
cd "$(dirname "$0")"
installer="$PWD"
if test -f payload/TEST-ONLY-DPF-FIXTURE; then
    echo "Installer test data cannot be included in a release. Stage real build inputs first." >&2
    exit 1
fi
template="$(cd "$1" && pwd)"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
dist=$(mktemp -d)
trap 'rm -rf "$dist"' EXIT
cp -R "$template/." "$dist"
export CGO_ENABLED=0
go test -count=1 ./...
GOOS=darwin GOARCH=arm64 go build -trimpath -o "$dist/mac-arm64" .
GOOS=darwin GOARCH=amd64 go build -trimpath -o "$dist/mac-amd64" .
GOOS=windows GOARCH=amd64 go build -trimpath -o "$dist/windows/x64/ID6AppsInstaller.exe" .
GOOS=windows GOARCH=arm64 go build -trimpath -o "$dist/windows/arm64/ID6AppsInstaller.exe" .
bundle="$dist/mac/ID6Apps Installer.app"
lipo -create "$dist/mac-amd64" "$dist/mac-arm64" -output "$bundle/Contents/MacOS/ID6AppsInstaller"
plutil -replace CFBundleShortVersionString -string 0.7.6 "$bundle/Contents/Info.plist"
plutil -replace CFBundleVersion -string 30 "$bundle/Contents/Info.plist"
codesign --force --sign - "$bundle"
codesign --verify --strict "$bundle"
for arch in arm64 x86_64; do
    /usr/bin/lipo -archs "$bundle/Contents/MacOS/ID6AppsInstaller" | tr ' ' '\n' | grep -qx "$arch"
done
file "$dist/windows/"*/ID6AppsInstaller.exe
for target in mac windows; do
    cp "$installer/Installer-Quick-Start.md" "$dist/$target/README.md"
    if ! find "$dist/$target" -iname '*NBT-AAD*' | grep -q .; then
        cp "$installer/Licenses/"NBT-AAD-* "$dist/$target/Licenses/"
    fi
done
(cd "$dist/mac" && ditto -c -k --sequesterRsrc . "$out/ID6Apps-Installer-v0.7.6-macOS.zip")
(cd "$dist/windows" && zip -q -9 -X -r "$out/ID6Apps-Installer-v0.7.6-Windows.zip" README.md Licenses x64 arm64)
unzip -tq "$out/ID6Apps-Installer-v0.7.6-macOS.zip"
unzip -tq "$out/ID6Apps-Installer-v0.7.6-Windows.zip"
