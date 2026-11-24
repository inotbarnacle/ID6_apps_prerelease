# Developing an ID6 dashboard

This guide explains how to add and test a native iDrive dashboard in this
repository, then make it available through the desktop installer. It assumes
you can use a terminal and edit C/C++, XML, Python, and Go. The intent is to
show where the pieces live so you can work through the code yourself.

Start with a display-only screen or a dashboard that reads ordinary vehicle
data. The HMI, worker, package, and installer are separate pieces; get each one
working before adding the next. Treat vehicle diagnostics as a separate,
vehicle-specific engineering task. Begin read-only, use documented and
verified signals, and do not test a new app while driving.

## 1. Get oriented

From the repository root, create a branch and run the existing host tests:

```sh
git switch -c my-dashboard
make -C source/native test
```

Host tests use fake diagnostic and MOST interfaces. They do not need a car or
the private BMW framework files. See [BUILD.md](BUILD.md) for toolchain details.

The main folders are:

| Path | Purpose |
| --- | --- |
| `source/native/src/plugins/` | C++ HMI applications loaded by the iDrive AppFramework |
| `source/native/src/workers/` | C diagnostic workers, including the host-testable workers |
| `source/native/manifests/` | The plugin manifest and RHMI screen/action/model XML |
| `source/native/src/common/` | Shared diagnostic transport, leases, decoders, and helpers |
| `source/native/tests/` | Host-side C/C++ and Python tests, including fake DME/DSI support |
| `source/tools/` | RHMI/XML/artwork generators and diagnostic probes |
| `packages/` | ZIP archives consumed by the installer payload stager |
| `source/tools/desktop-installer/` | Installer UI, app registry, payload staging, and release build |

Useful examples:

- [ShiftDeck](../native/src/plugins/shift_deck.cpp) for a live dashboard, model
  updates, and polling.
- [EngineCodes](../native/src/plugins/engine_codes.cpp) for a user-triggered
  worker request, asynchronous pipe handling, and paged results.
- [MhdMaps](../native/src/plugins/mhd_maps.cpp) for a small app with a few
  button actions.
- [ShiftDeck XML](../native/manifests/ShiftDeck.xml) for an RHMI screen with
  models and component geometry.
- [make_drive_apps.py](../tools/make_drive_apps.py) for generating XML and
  image resources in Python.

Read the complete example files before copying a pattern. In particular,
notice how each example cleans up children or timers in `onFinalize()`.

## 2. Understand the three layers

An installed native app is made from three cooperating parts:

1. **Plugin manifest** (`<App>.manifest`) tells the HMI where the shared library
   and RHMI description are and assigns the app an Etch connector ID.
2. **RHMI description** (`<App>.xml`) declares HMI actions, data models,
   screens, and components. The numerical IDs are the connections between the
   screen and the native plugin.
3. **C++ plugin** (`src/plugins/<app>.cpp`) registers handlers for the
   manifest's RA actions, updates models, and implements the app lifecycle.

A worker is a fourth, optional part. Put potentially blocking diagnostic or
collection work in a separate executable, not in an HMI callback. Existing
plugins launch a worker and read its result asynchronously.

Keep IDs consistent across all three layers:

| XML element | C++ counterpart |
| --- | --- |
| `<raAction id="21" />` | `addHandler(21u, action)` |
| `<hmiAction id="10" target="100" />` | HMI navigation target/state |
| `<raDataModel id="80" ... />` | `updateModel(80u, ...)` |
| button `action="21"` | The action the button invokes |
| manifest `<id>122</id>` | Unique Etch connector registered with the installer |

An `raAction` is handled by the native plugin. An `hmiAction` navigates to an
HMI state. Do not reuse IDs from another app without checking the complete
description and its handler registrations.

## 3. Make the smallest working screen

For a new app, copy an existing simple app's structure, then rename it
consistently. Do not start by editing the installer or adding a worker.

1. Add `source/native/src/plugins/MyDashboard.cpp`.
2. Add `source/native/manifests/MyDashboard.manifest` and
   `source/native/manifests/MyDashboard.xml`.
3. Give the app a unique name, connector ID, state IDs, action IDs, and model
   IDs. The manifest's plugin name, library name, XML filename, and installer
   app name must agree.
4. Put one screen and one button on the screen. Have the button update a text
   model. Build and verify this UI before adding data collection.
5. Add explicit geometry properties to components. The description's
   coordinates are designed for the main 1440 × 540 layout; each visible
   component must remain within the display. The installer stager checks
   bounds and generates 800 × 480 and 960 × 480 variants for compact layouts.
6. If the screen needs artwork, generate the app's
   `<App>_ALL_Images.zip` and reference its image IDs consistently in XML.
   Existing generators are better examples than hand-editing binary image
   databases. `make -C source/native assets` regenerates several existing
   apps' assets; it does not generate assets for a new app automatically.

The manifest usually follows this shape:

```xml
<plugin>
  <name>MyDashboard</name>
  <type>App</type>
  <file>{pluginDir}/MyDashboard</file>
  <config>
    <autoStart>yes</autoStart>
    <dependencies>
      <pluginAppConnector>
        <type>Etch</type>
        <id>YOUR_UNIQUE_CONNECTOR_ID</id>
        <asyncCallbacks>yes</asyncCallbacks>
      </pluginAppConnector>
    </dependencies>
    <parameters>
      <DescFile>{pluginDir}/MyDashboard.xml</DescFile>
      <ImageDBFile>{pluginDir}/MyDashboard_ALL_Images.zip</ImageDBFile>
    </parameters>
  </config>
</plugin>
```

If the app has no image database, omit `ImageDBFile` and account for that in
the installer as described below.

The C++ plugin exports `pluginAppInfo` with a `create_app` factory. Its
`onInit()` registers actions and initializes visible models; `onFinalize()`
must stop work and release resources. Preserve the repository's C++98/QNX
compatibility and use the SDK and ABI patterns of existing plugins. Do not
assume a modern desktop C++ API is available on the head unit.

## 4. Add data collection only after the UI works

For a dashboard that reads data continuously or on button press:

- Keep HMI callbacks short. Do not wait on a network or ECU response in the
  HMI dispatcher.
- Follow `EngineCodes` for a child worker, nonblocking pipe, scheduled polling,
  and lifecycle cleanup. Use a generation/token in delayed callbacks so
  results from an old request cannot update a new screen.
- For diagnostic transport, reuse `diag_link` and the diagnostic lease
  patterns in `source/native/src/common/` and `source/native/src/workers/`.
  The shared link handles HSFZ and the MOST fallback. Do not create a second
  independent transport implementation.
- Use `DiagnosticGateway` for the configured gateway address. The head-unit
  SSH address and diagnostic gateway address are different endpoints.
- Check every response prefix, length, range, status, timeout, and invalid
  sentinel. Display an explicit unavailable/stale state rather than a
  plausible-looking default.
- For continuous polling, stop the worker when the screen closes and on
  `onFinalize()`. Respect the shared diagnostic lease so separate apps do not
  collide on the vehicle bus.

Add a Makefile target for a host-buildable worker and test it against the fake
DME/DSI infrastructure. For a read-only DME feature, the tests in
`test_drive_apps.py` and `test_oneshot_most.py` show how the repository tests
both HSFZ and MOST behavior. Test successful replies, refusal, malformed or
short replies, timeout, and busy-lease behavior. Keep the vehicle-specific
request/decoder separate from the HMI so it can be tested without loading the
BMW UI framework.

The QNX worker compiler helper in `source/native/build.sh` builds diagnostic
workers with the shared link code. Add a worker build line there if your app
needs one. Add a separate C++ plugin build line there for the shared library.
Also add the worker to `source/native/Makefile` and the `test` prerequisites so
the host worker is compiled by the normal test command.

## 5. Build and validate locally

From the repository root:

```sh
make -C source/native test
python3 source/tools/test_probe_signals.py
```

The probe test is useful when your change touches diagnostic capture or
decoding. Run the focused test while iterating, then run the full suite before
sharing your branch.

To compile the actual native payload, follow [BUILD.md](BUILD.md). In brief,
you need the licensed QNX 6.5 ARM toolchain, the matching head-unit
`libAppFramework.A.so` and `libCoreFramework.A.so`, and any required runtime
libraries. Then run:

```sh
cd source
sh native/build.sh
```

The build produces worker executables and plugin `.so` files under
`source/native/out/`. A successful host build is not a substitute for this
QNX cross-build: it does not validate the actual HMI ABI or load the plugin on
the car.

For an initial on-car check, use a supported head unit and a parked vehicle.
Install only the app under development, verify it opens and closes cleanly,
check all button and empty/error states, then verify data against an
independent source. Restart iDrive and confirm the app loads again. Do not
infer support for other firmware, chassis, engines, or ECUs from one
successful test.

## 6. How the desktop installer gets its apps

The installer does **not** copy files from `source/native/out/` directly.
`stage-payload.py` reads checked app files from `packages/<group>.zip`,
verifies each file against the ZIP's root `SHA256SUMS`, creates compact
layouts, and places the result under the installer template's `payload/`.
`install.go` embeds that staged `payload/` at compile time. Therefore changing
C++ or XML alone does not change an already-built installer.

To add an app to the optional installer:

1. **Create its package folder** with the files the app needs:
   - `<App>/<App>.manifest`
   - `<App>/<App>.xml`
   - `<App>/lib<App>.so`
   - `<App>/<worker-name>` (the executable worker, if the app has one)
   - `<App>/<App>_ALL_Images.zip` (if the app uses images)
2. **Put it in a package group** in `packages/`, such as `dashboard.zip`.
   Package members use `<App>/<filename>` paths, and the archive contains a
   root `SHA256SUMS`. This repo consumes these package ZIPs; the stager does
   not compile native code or create a release ZIP for you.
3. Add the app to `GROUPS` in
   [stage-payload.py](../tools/desktop-installer/stage-payload.py). If its
   layout should be scaled for 800 × 480 and 960 × 480 displays, add it to
   `COMPACT` too. Check the generated variants and component bounds.
4. Add an entry to the `apps` slice in
   [install.go](../tools/desktop-installer/install.go): app folder name,
   worker executable name, and the **same unique connector ID** used by the
   manifest. The installer expects `<App>.manifest`, `<App>.xml`,
   `lib<App>.so`, and the registered worker file. If the app has more than one
   executable, update `executables()`. If it has no image archive, update
   `usesImages()`.
5. Add a checkbox for installation and, if it can be removed, a checkbox for
   uninstallation in
   [index.html](../tools/desktop-installer/index.html).
6. Add or update tests in `install_test.go`: selection, publication, and
   connector-conflict checks. Add a manifest/layout test if geometry or
   action wiring is important.
7. If the shared library imports new BMW framework symbols, run the ABI audit
   in [BUILD.md](BUILD.md) against the correct private reference libraries.
   Never commit the private framework libraries. The fingerprint test checks
   the packaged plugin imports.

Connector IDs must not conflict with any other plugin installed on the car.
The installer checks existing connector use before publishing. The manifest,
installer registry, and app package must all use the same name and connector.
If the app is not ready to distribute, keep it out of the installer and test
it as an individual package first.

To regenerate a package group's `SHA256SUMS`, work on a **complete copy** of
the group, including the existing apps; do not make a replacement
`dashboard.zip` containing only the new app. For the flat app payload files
used here, from the package staging directory you can generate entries with:

```sh
python3 - <<'PY'
from hashlib import sha256
from pathlib import Path

files = sorted(p for app in Path(".").iterdir() if app.is_dir()
               for p in app.iterdir() if p.is_file())
Path("SHA256SUMS").write_text("".join(
    f"{sha256(path.read_bytes()).hexdigest()}  {path.as_posix()}\n"
    for path in files
))
PY
```

Create a fresh ZIP from every app directory in the group plus `SHA256SUMS`,
then run `unzip -t` on it. For the dashboard group, for example:

```sh
zip -qr /tmp/dashboard-new.zip BoostDeck ShiftDeck EngineCodes MhdMaps MyDashboard SHA256SUMS
unzip -t /tmp/dashboard-new.zip
```

Keep archive member paths in the `App/file` form shown by existing package
ZIPs. The stager rejects checksum mismatches. Do not copy this example list
blindly if group membership has changed; include every app in that group.

The current installer registry expects every registered app to have at least
one executable worker. A workerless, display-only app can be developed and
tested locally, but making it installable also requires updating the
installer's executable validation and publication logic to support no worker,
then testing install and uninstall for that case.

## 7. Stage and build the installer

First build the QNX outputs and update the package ZIP. Then from the installer
directory, stage the embedded payload and build the desktop release:

```sh
cd source/tools/desktop-installer
python3 stage-payload.py ../../.. /absolute/path/to/gaugedeck-fileops
sh build-release.sh template /absolute/path/to/output
```

`gaugedeck-fileops` comes from the matching QNX release build. Python 3 and
Pillow are needed for layout/image staging. The release script runs the Go
tests and builds the macOS and Windows installers; use the Go version and
platform prerequisites in
[desktop-installer/README.md](../tools/desktop-installer/README.md).

Before sharing the installer:

1. Confirm the app appears in the install and uninstall menus.
2. Inspect `desktop-installer/payload/<App>/` and the generated compact XML
   under `desktop-installer/payload/layouts/`.
3. Run `go test ./...` from `source/tools/desktop-installer/`.
4. Run any focused manifest/geometry tests you added, along with
   `make -C source/native test`.
5. Test installation and uninstallation on a supported test unit and verify
   the installer's backup/restore behavior.

For the normal user install/uninstall procedure, see the
[installer quick start](../tools/desktop-installer/Installer-Quick-Start.md).
