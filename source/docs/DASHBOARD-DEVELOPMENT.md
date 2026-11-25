# Developing an ID6 dashboard

This is a hands-on guide to exploring this repository, creating a small native
iDrive app, testing it, and (if you decide to distribute it) connecting it to
the desktop installer. It assumes you are new to this project, but have basic
experience with a terminal and source code.

Work in layers. First make an app that opens and updates text. Next add
navigation and artwork. Only then add vehicle data. Add the installer last.
This keeps a UI problem, a diagnostic problem, and a packaging problem from
becoming one hard-to-debug change.

> **Vehicle safety:** Start with a display-only screen. If you add vehicle
> communication, start read-only and use verified vehicle-specific requests.
> Do not test a new app while driving. Do not treat one car's successful test
> as support for another ECU, chassis, or firmware.

## What you are building

An installed dashboard is composed of a few separate files:

| Part | Typical file | What it does |
| --- | --- | --- |
| Native HMI plugin | `src/plugins/MyDashboard.cpp` | Registers actions, updates screen models, and manages app lifecycle |
| RHMI description | `manifests/MyDashboard.xml` | Declares screens, components, data models, and actions |
| Plugin manifest | `manifests/MyDashboard.manifest` | Connects the plugin library and RHMI description to the HMI |
| Worker (optional) | `src/workers/my_worker.c` | Performs slow work such as diagnostics outside the HMI dispatcher |
| Artwork (optional) | `manifests/MyDashboard_ALL_Images.zip` | Supplies image resources referenced by the RHMI XML |
| Package (for installer) | `packages/<group>.zip` | Checked source payload consumed by the installer stager |
| Installer integration | `tools/desktop-installer/` | Offers, stages, embeds, uploads, verifies, and removes the app |

The usual data flow is:

```text
button in XML -> RA action -> C++ handler -> worker (optional)
               -> C++ updates RHMI model -> label/image changes on screen
```

The XML IDs are how the pieces connect. The plugin's Etch connector ID is a
separate identifier used by the manifest and installer to register the app.

## Step 0: Open the repository and establish a baseline

Open a terminal at the repository root. Start a branch and run the existing
host test suite before editing:

```sh
git status --short
git switch -c my-dashboard
make -C source/native test
```

If `git status --short` shows changes you did not make, leave them alone and
avoid overwriting those files. If the branch name already exists, choose
another name.

The host test suite uses a normal C/C++ compiler and fake DME/DSI components.
It does not require the private BMW frameworks or a connected car. It builds
worker code and tests its behavior; it does **not** compile or validate the
QNX HMI plugin.

For the first pass, learn the layout:

```text
source/native/src/plugins/       C++ HMI applications
source/native/src/workers/       C diagnostic/collection workers
source/native/src/common/        Shared diagnostic and display code
source/native/manifests/         Plugin manifests and RHMI XML
source/native/tests/             Native host tests and fake vehicle interfaces
source/tools/                    Generators, probes, audits
packages/                        Input ZIP packages for the desktop installer
source/tools/desktop-installer/  Installer UI, Go installer, payload stager
source/docs/                     Project and development documentation
```

## Step 1: Read one example for each layer

Do not begin by copying a whole app. Open the following files and trace one
user action from the screen to the code:

1. [MHD Maps C++ plugin](../native/src/plugins/mhd_maps.cpp): a compact
   event-driven app with RA handlers, model updates, and optional workers.
2. [MHD Maps RHMI description](../native/manifests/MhdMaps.xml): an app screen
   with buttons, images, and labels.
3. [MHD Maps plugin manifest](../native/manifests/MhdMaps.manifest): the
   relationship between app, library, XML description, and connector.
4. [Engine Codes C++ plugin](../native/src/plugins/engine_codes.cpp): an
   example of running a worker asynchronously and reading its output.
5. [Engine Codes worker](../native/src/workers/engine_codes_worker.c): an
   example using the shared diagnostic link and returning text to the plugin.
6. [ShiftDeck](../native/src/plugins/shift_deck.cpp): a continuously updated
   dashboard with a worker, polling, visibility handling, and display models.

When reading, search for the IDs in order:

```text
button action="21" -> raAction id="21" -> addHandler(21u, action)
label model="50"   -> raDataModel id="50" -> updateModel(50u, ...)
```

This is the most useful habit when exploring an RHMI app: follow one ID across
the XML and C++ instead of trying to understand every file at once.

## Step 2: Make a UI-only app

Use your own app name consistently. This example uses `MyDashboard`; replace
it with a unique name. At minimum create:

```text
source/native/src/plugins/my_dashboard.cpp
source/native/manifests/MyDashboard.manifest
source/native/manifests/MyDashboard.xml
```

Do not register it with the installer yet. First make the plugin and screen
agree with each other.

### 2.1 Write a minimal plugin

The following is a shape to adapt, not a complete standalone build. The
constructor and factory match the patterns used by the plugins in this
repository:

```cpp
#include "plugin_factory.h"

class MyDashboard : public bmw::PluginApp
{
public:
    MyDashboard(bmw::IDispatcher &dispatcher, bmw::BasicString<char> name,
                unsigned major, unsigned minor,
                bmw::RHMIInterfaceVersion version,
                const bmw::Bundle &config, const bmw::Bundle &resources)
        : PluginApp(dispatcher, name, major, minor, version, config, resources)
    {
    }

    virtual bool onInit()
    {
        typedef bmw::Delegate<bool, const bmw::Parcel &> Action;
        Action action;
        action.m_obj = this;
        action.m_exec =
            &Action::execImpl<MyDashboard, &MyDashboard::showMessage>;
        addHandler(21u, action);
        updateModel(50u, bmw::BasicString<char>("Ready"));
        return true;
    }

    bool showMessage(const bmw::Parcel &)
    {
        updateModel(50u, bmw::BasicString<char>("Button pressed"));
        return true;
    }
};

extern "C"
{
    PluginAppInfo pluginAppInfo = {1, "MyDashboard", "owner-development", 1,
                                   create_app<MyDashboard>, destroy_app};
}
```

Important details:

- The callback is a member of the app class and returns `bool`.
- `addHandler(21u, action)` registers the RA action from the XML.
- `updateModel(50u, ...)` updates the data model with ID 50.
- The exported `pluginAppInfo` name must match the plugin being created.
- Keep code compatible with the project's QNX C++98 build. Avoid modern
  standard-library features and exceptions; check the build flags in
  `source/native/build.sh`.

### 2.2 Write a simple RHMI description

This small description gives the plugin an entry screen, a text model, and a
button. Add component geometry: the desktop installer checks it and scales
compact layouts from it.

```xml
<?xml version='1.0' encoding='utf-8'?>
<pluginApps>
  <pluginApp applicationType="VehicleInformation"
             applicationWeight="220" text="My Dashboard">
    <entryButton id="1" action="10">
      <properties>
        <property id="23" value="2" />
      </properties>
    </entryButton>
    <actions>
      <hmiAction id="10" target="100" />
      <raAction id="21" />
    </actions>
    <models>
      <raDataModel id="50" modelType="Richtext" />
    </models>
    <hmiStates>
      <hmiState id="100" text="My Dashboard">
        <components>
          <label id="200" model="50">
            <properties>
              <property id="20" value="54" />
              <property id="21" value="80" />
              <property id="9" value="700" />
              <property id="10" value="55" />
            </properties>
          </label>
          <button id="201" action="21" text="Refresh">
            <properties>
              <property id="20" value="54" />
              <property id="21" value="170" />
              <property id="9" value="300" />
              <property id="10" value="55" />
            </properties>
          </button>
        </components>
      </hmiState>
    </hmiStates>
  </pluginApp>
</pluginApps>
```

In this example:

- `hmiState id="100"` is the screen.
- `hmiAction id="10" target="100"` opens that screen.
- `raAction id="21"` is delivered to the native plugin.
- The button's `action="21"` invokes that RA action.
- `raDataModel id="50"` is updated in C++ and displayed by the label whose
  `model="50"`.
- Geometry properties `20`, `21`, `9`, and `10` are x, y, width, and height.

The base description targets a 1440 × 540 display. The installer stager uses
the repository's measured usable content bounds: at that base size, a
component's x+width must not exceed 1260 and y+height must not exceed 477.
Compact layouts use 800 × 480 and 960 × 480 content widths of 540 and 900
respectively. The stager checks these bounds and creates compact variants for
apps marked compact. Keep all components inside the measured bounds even if
your personal test car has a different display.

### 2.3 Write the plugin manifest

Use the format of `ShiftDeck.manifest` or `MhdMaps.manifest`, changing the app
name, paths, and connector:

```xml
<?xml version='1.0' encoding='utf-8'?>
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
    </parameters>
  </config>
</plugin>
```

The example omits `ImageDBFile` because this UI has no images. If you add
artwork, add `<ImageDBFile>{pluginDir}/MyDashboard_ALL_Images.zip</ImageDBFile>`
and include the ZIP in the package later.

The connector ID is not a casual choice. It must not collide with connectors
already installed on the head unit. Check current manifests and use an ID
confirmed as unused for the target installation. The app ID/name, connector,
library and description filenames must remain consistent in the manifest,
factory export, build command, and installer registry.

### 2.4 Build checklist before moving on

Review the ID wiring before you add data:

- `button action` exists as an `raAction`.
- The C++ plugin registers that same action ID.
- The handler updates a model ID declared in XML.
- The label refers to that same model ID.
- The screen target points to an existing HMI state.
- The manifest names the same app and XML file as the source/build.
- Every component has reasonable bounds.

At this point the app is still not in the repository's normal build or
installer. That is intentional: first get one UI working.

## Step 3: Add the app to the native build

The native build has separate commands for QNX workers and C++ shared-library
plugins. For a UI-only app, only add the plugin command to
`source/native/build.sh`, alongside the existing plugin commands:

```sh
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared \
    src/plugins/my_dashboard.cpp \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" \
    -Lout -lsocket -Wl,--allow-shlib-undefined \
    -Wl,-soname,libMyDashboard.so -o out/libMyDashboard.so
```

Use the `cxx` helper and flags from the actual script; this snippet shows the
placement and naming pattern. If your plugin calls shared core code, link the
appropriate object files and libraries just as its nearest existing example
does.

The host test suite cannot compile this plugin: `plugin_factory.h` and the
BMW frameworks are private build inputs. To cross-compile it, follow
[BUILD.md](BUILD.md) and run the QNX native build in an environment with the
licensed toolchain and matching framework libraries. Never add those private
libraries to Git or a package.

For local development without the QNX framework, keep the first testable
logic in a plain C/C++ helper that has no BMW dependencies, then add a small
unit test for that helper. Use the existing host-buildable worker and test
patterns as examples.

## Step 4: Add a worker only when needed

Do not add a worker just because other apps have one. A static screen or
quick local UI action can stay in the plugin. Anything that can block—network,
diagnostics, disk work, or long computation—must not block the HMI dispatcher.
Put that work in a separate process and return bounded results to the plugin.

### 4.1 Worker responsibilities

A robust worker should:

1. Validate command-line arguments and reject unknown options.
2. Use existing common helpers rather than implementing a second transport.
3. Bound response sizes and timeouts.
4. Print a small, documented result format to standard output.
5. Print diagnostics to standard error where useful.
6. Return or report failures distinctly; do not make a timeout look successful.
7. Exit cleanly when stopped.

For vehicle diagnostics, reuse `diag_link` and the diagnostic lease in
`source/native/src/common/`. The common link handles HSFZ and the MOST fallback.
Use `DiagnosticGateway` in the HMI plugin to read the configured gateway
address. The head-unit SSH address is not the diagnostic gateway address.

### 4.2 Connect worker to the plugin asynchronously

Follow `EngineCodes` or `MhdMaps`:

1. Register a button handler in `onInit()`.
2. In the handler, create a pipe and launch the worker with a fixed executable
   path and validated arguments.
3. Set the read end nonblocking and close unused pipe descriptors.
4. Show a “working” status, then schedule a short delayed callback.
5. In `invokeDataUpdate()`, read available bytes without blocking. Reschedule
   while the pipe or child is active.
6. On EOF, bound and parse the output, update screen models, close descriptors,
   and reap the child.
7. Stop the worker and invalidate pending poll events in `onFinalize()`.

Use a generation counter in delayed events. Increment it whenever an operation
starts or the worker stops; ignore callbacks carrying an older generation.
Otherwise a delayed event from a previous screen or request can overwrite
newer UI state.

### 4.3 Test worker behavior on the host

Add a Makefile target in `source/native/Makefile` using the existing
`WORKER_COMMON` and `HOST_LINK` variables if the worker uses the shared
diagnostic link. Add the target to the `test` prerequisites so it is built by
the standard suite. Add the worker to `source/native/build.sh` using the QNX
`worker` helper.

Tests belong under `source/native/tests/`. For examples:

- `test_drive_apps.py` runs workers against a simulated HSFZ gateway.
- `test_oneshot_most.py` checks one-shot workers with a fake MOST DME.
- `test_transport.py` contains reusable fake HSFZ framing utilities.
- `fake_dme.c` / `fake_dme.h` and `fake_most_env.c` provide the fake DME/DSI
  behavior.

Cover success, ECU refusal, malformed/short response, timeout, busy diagnostic
lease, and unexpected arguments as appropriate. For a read-only feature, test
that a write request is never sent. Keep request decoding in worker/core code
that can be tested independently from the HMI plugin.

Once your target is connected to `make test`, run the whole suite:

```sh
make -C source/native test
```

## Step 5: Add artwork or generated resources (optional)

The RHMI XML references image IDs; the app's image ZIP provides PNG files for
those IDs. Keep image IDs distinct from model IDs and match the XML's
`imageIdModel` declarations to filenames in the archive.

Use existing generators rather than editing a binary image database by hand:

- `source/tools/make_drive_apps.py` creates the MHD Maps and ShiftDeck resource
  XML/artwork patterns.
- `source/tools/make_warmdeck.py` and
  `source/tools/make_hybrid_and_mg.py` generate other app assets.
- `source/tools/make_boostdeck_extended.py` extends BoostDeck artwork.

`make -C source/native assets` only rebuilds the assets covered by that
Makefile target. It will not automatically discover a new app or generator.
If you add a generator, document how it runs and add an explicit target or
step so another developer can repeat it.

Check assets and geometry together at the display sizes your app supports.
Installer compact layouts scale XML geometry and image archives for 800 × 480
and 960 × 480 displays when the app is marked compact in the stager.

## Step 6: Validate a real QNX build and car test

Read [BUILD.md](BUILD.md) for all toolchain requirements. The short version:

1. Prepare the licensed QNX 6.5 ARM toolchain with the expected compiler.
2. Obtain the matching `libAppFramework.A.so` and `libCoreFramework.A.so`
   from a compatible unit into the configured private frameworks directory.
3. Set `QNX_HOST`, `QNX_TARGET`, `QNX_CONFIGURATION`,
   `NBTEVO_FRAMEWORKS_DIR`, or `NBTEVO_RUNTIME_DIR` if your setup differs from
   the defaults.
4. From `source/`, run:

   ```sh
   sh native/build.sh
   ```

5. Confirm the expected `out/libMyDashboard.so` and any worker executable
   exist. Inspect build output and do not continue if a tool or framework is
   missing.

Before installing on a car, validate the framework ABI and display support
using the [compatibility guide](COMPATIBILITY.md) and [porting guide](PORTING.md)
as applicable. A QNX link succeeding does not prove that the plugin loads or
that the target ECU supports your request.

Test only while parked:

1. Start with the simplest screen and confirm it opens and closes.
2. Press every action and verify the intended model updates.
3. Check empty, unavailable, error, and stale-data states.
4. For data, compare readings with an independent, trusted source under the
   same conditions.
5. Close the app during a request and confirm cleanup.
6. Restart iDrive and confirm the app loads again.
7. Record the exact head-unit firmware, display, chassis, engine, and ECU
   tested. Do not claim compatibility beyond that evidence.

For signal discovery, see [Port BoostDeck to another car](PORTING.md). It
describes the repository's bounded read-only probe workflow and why a
plausible-looking value is not by itself a validated decoder.

## Step 7: Understand the installer pipeline

The installer does not compile C++ or workers. It embeds a staged `payload/`
directory at Go build time. The pipeline is:

```text
native source -> QNX build outputs -> package group ZIP + SHA256SUMS
              -> stage-payload.py -> installer payload/ and compact layouts
              -> Go build embeds payload -> desktop installer release
```

Important files:

| File | Role |
| --- | --- |
| `source/tools/desktop-installer/index.html` | Checkboxes shown to users |
| `source/tools/desktop-installer/easy.go` | Validates menu selection and restrictions |
| `source/tools/desktop-installer/install.go` | App registry, expected files, connector checks, upload/publication, uninstall |
| `source/tools/desktop-installer/stage-payload.py` | Reads package ZIPs, verifies SHA256SUMS, builds payload and compact layouts |
| `source/tools/desktop-installer/build-release.sh` | Runs Go tests and creates desktop releases |

An app added only to the Go app list will fail: its files must exist under
`payload/<App>/` before compiling because `install.go` embeds `payload`.
An app added only to the package ZIP will not appear as a selectable option
until the installer UI and app registry are updated.

## Step 8: Package the app and add it to the installer

Do this after the app works and its QNX outputs exist.

### 8.1 Build the app folder

For a worker-backed app with artwork, a package folder usually contains:

```text
MyDashboard/
  MyDashboard.manifest
  MyDashboard.xml
  MyDashboard_ALL_Images.zip
  libMyDashboard.so
  my-dashboard-qnx
```

Omit the image ZIP if the app has no images. The current installer expects each
registered app to include an executable worker; a plugin-only display app is
not yet supported by its publication checks. To distribute a workerless app,
you would first need to update the installer validation/publication logic and
its tests to allow that case.

File names matter: the installer validates the manifest, XML, shared-library,
optional image archive, and registered executable files before publication.
Its Go code includes `gateway-ip.txt` when it stages an app.

### 8.2 Update the package ZIP

The package archives live in `packages/`. Check the existing archive layout
before making one:

```sh
unzip -l packages/dashboard.zip | head -40
```

The package ZIP contains app directories, a root `SHA256SUMS`, and paths in
`App/filename` form. There is no automatic command in this repository that
builds the package archive from arbitrary new native output. Make a separate
staging directory, copy in the **complete** contents for every app in that
package group, and add your new app directory.

For flat files directly inside each app directory, generate the checksum list
from the staging directory:

```sh
cd /path/to/dashboard-package-staging
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

Create a new archive containing every existing app directory in the group,
your new app directory, and `SHA256SUMS`. Example only—adjust the app list if
the package group changes:

```sh
zip -qr /tmp/dashboard-new.zip BoostDeck ShiftDeck EngineCodes MhdMaps MyDashboard SHA256SUMS
unzip -t /tmp/dashboard-new.zip
unzip -l /tmp/dashboard-new.zip | head -50
```

Check that the ZIP has the expected top-level app folders and a root
`SHA256SUMS`. Replace/update the intended `packages/<group>.zip` only after
checking the new archive. Preserve existing app files: a ZIP containing only
your new app would cause the stager to produce empty payloads for the other
apps in that group.

### 8.3 Register the app

1. Add the app name to the appropriate group in `GROUPS` in
   [stage-payload.py](../tools/desktop-installer/stage-payload.py). The name
   must match the package directory. If its layout should be scaled for
   compact displays, add it to `COMPACT`.
2. Add the app to the `apps` slice in
   [install.go](../tools/desktop-installer/install.go). The entry carries the
   app folder name, its worker filename, and its connector ID. The connector
   must match the manifest and must be available on the target unit.
3. If the app has additional executable files, update `executables()`. The
   selected worker and every companion worker must be in the package.
4. If the app has no image archive, update `usesImages()`.
5. Add an installation checkbox in
   [index.html](../tools/desktop-installer/index.html). Add an uninstall
   checkbox too if the app should be removable by the user.
6. Read [easy.go](../tools/desktop-installer/easy.go) before adding special
   selection rules, especially if your app is another tuning suite.
7. Add or update Go tests in `install_test.go` for menu selection, connector
   conflict, expected files, and publication/uninstall behavior.
8. Add a native test for manifest action/model consistency and geometry if the
   app has custom RHMI resources.

The installer checks currently installed manifests for connector collisions.
Do not pick an ID by looking only at the app list in this repository; a car
may already have another plugin using it.

## Step 9: Stage and build the desktop installer

First build the QNX outputs and update the package ZIP. Obtain the matching
`gaugedeck-fileops` helper from the native build or corresponding release. From
the installer directory, run the staging script:

```sh
cd source/tools/desktop-installer
python3 stage-payload.py ../../.. /absolute/path/to/gaugedeck-fileops
```

The stager reads every package listed in `GROUPS`, checks member hashes against
`SHA256SUMS`, copies app files to `payload/<App>/`, checks component geometry,
and creates compact layout XML (and image variants where applicable). It also
copies the installer helper and profile file. Python 3 and Pillow are needed
for image/layout staging. If staging fails, fix the reported input or geometry;
do not bypass checksum or bounds checks.

Inspect the staged output before building:

```sh
find payload/MyDashboard -maxdepth 1 -type f -print
find payload/layouts -type f -name 'MyDashboard.xml' -print
```

Confirm the app's manifest, XML, library, worker(s), and optional image ZIP are
present. Confirm compact XML variants exist if the app is marked `COMPACT`.
The Go installer uses `//go:embed payload`, so rebuilding after staging is
required to include your latest files.

Run installer tests from the same directory:

```sh
go test ./...
```

To produce release archives, follow
[desktop-installer/README.md](../tools/desktop-installer/README.md) for Go and
platform prerequisites. The release script accepts a template directory and
an output directory:

```sh
sh build-release.sh /path/to/installer-template /absolute/path/to/output
```

The script runs the Go tests, builds macOS and Windows binaries, assembles the
installer bundles, and checks the resulting ZIP archives. It is platform
specific; the QNX native build is a separate earlier step.

## Step 10: Final checklist

Before asking someone else to try the app, verify each part:

- [ ] `git diff --check` reports no whitespace errors.
- [ ] `make -C source/native test` passes.
- [ ] New worker tests cover success and the relevant failure/invalid cases.
- [ ] The actual QNX plugin and worker build succeeds with the intended
      framework/runtime inputs.
- [ ] Manifest name, XML name, plugin factory name, build output, package
      folder, and installer registry agree.
- [ ] Connector ID is unique for the target HMI installation.
- [ ] RHMI action IDs, data model IDs, and C++ registrations agree.
- [ ] Screen components fit the supported display layouts.
- [ ] Package `SHA256SUMS` verifies and `unzip -t` passes.
- [ ] `stage-payload.py` completes and the installer payload contains every
      expected app file.
- [ ] `go test ./...` passes in the desktop installer directory.
- [ ] Installation and removal have been tested on a supported test unit.
- [ ] Any diagnostic behavior has been validated while parked against an
      independent source and documented for the exact tested vehicle/ECU.

For the normal user install/uninstall procedure, see the
[installer quick start](../tools/desktop-installer/Installer-Quick-Start.md).
