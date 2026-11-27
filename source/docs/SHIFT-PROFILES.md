# ShiftDeck engine profiles

The existing shift-light presets and 8,000 RPM display are gasoline-oriented.
They are unsuitable as the only choices for a diesel dashboard. This change
adds a manually selected Diesel profile while preserving the existing Gasoline
profile and all five source presets (5800, 6000, 6250, 6500 and 6700 RPM).

Open **OPTIONS → Engine profile → Use diesel profile**, then **Configure diesel
target and display**. Set the advisory target in 100 RPM steps and choose a
0–4000, 0–5000 or 0–6000 RPM display. The target must be at least 1000 RPM and
below the selected display ceiling. Reducing that ceiling also reduces a target
that no longer fits, to 100 RPM below it; the current target is always shown.
Changing diesel settings while Gasoline is active leaves them inactive until
Diesel is selected. Selecting a gasoline preset explicitly restores Gasoline.
Each profile retains its own target; the speed-unit choice applies to both.

The initial diesel choices (4000 RPM target, 6000 RPM display) are configurable
app defaults, **not a verified N57 limit, BMW redline or operating instruction**.
They are not inferred from an ECU, model year or fuel type. The display scale is
an instrument setting, not an engine limiter. Choose the target for the exact
engine and conditions. The light is advisory and must not guide cold-engine
operation. No ECU request, limiter, gearbox command or vehicle setting changes.

Diesel scale artwork has no fixed redline band. The light and red presentation
depend on the chosen target. Its approach spans the last 1000 RPM below that
target; the existing Gasoline approach retains its 2000 RPM span. Numeric RPM,
sample freshness and speed decoding are unchanged. Missing, stale, future-dated
or hidden RPM data cannot activate the shift light. No interpolated value is
used to decide that the measured target was reached.

## Settings and rollback

Preferences remain at `/var/opt/hmi/data/ID5/ShiftDeck.prefs`, outside Plugins.
Existing exact four-byte `G1` settings load with the same gasoline target and
speed units; reading them does not rewrite the file. A changed setting saves an
atomic, validated `S1` record containing the profile, both targets, diesel scale
and units. A save failure leaves the live choice usable and shows **not saved**
on the Engine profile page. Corrupt, truncated or unknown versions are rejected.

Before any separately authorized installation, preserve the original preference
file alongside the original plugin folder and its checksums. Older binaries
cannot interpret `S1`; restore the original preference file when rolling back.
Installing, replacing files and restarting the head unit require owner approval.
No such action was performed for this source contribution.

## Build resources

The committed current XML and packaged image archive contain layout changes
that the older `make_drive_apps.py` generator does not reproduce. The additive
generator keeps current geometry, all original model/action IDs, the connector,
128-step sweep frames and every original gasoline image payload. It adds diesel
scales, target tiles and markers under new image IDs. The existing `assets`
target now invokes it after extracting the base archive:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements-shift.txt
make -C native assets PYTHON=../.venv/bin/python
```

For a focused rebuild, run from `source/`:

```sh
mkdir -p native/build
unzip -p ../packages/dashboard.zip ShiftDeck/ShiftDeck_ALL_Images.zip > native/build/ShiftDeck_ALL_Images.zip
.venv/bin/python tools/make_shift_profiles.py \
  --base-images native/build/ShiftDeck_ALL_Images.zip \
  --font /path/to/an/authorized/font.ttf
```

Use the same font file and pinned Pillow version for reproducible new artwork.
The default searches installed DIN, DejaVu and Liberation fonts. Existing image
payloads are copied without rerendering. Regenerated XML, the new constants and
the complete image archive must accompany the new plugin in a maintainer-built
package. Do not install the new plugin with the old archive. Generated archives
and native binaries remain ignored/private; published packages are unchanged.

## Evidence and limits

Host tests cover every legacy target/unit combination, persistence and invalid
files, all diesel target/scale bounds, a monotonic/clamped meter, production
profile/target/scale/unit callbacks, save errors and stale/hidden suppression.
Asset tests compare all original image payloads, preserve existing geometry and
models/actions, enumerate every valid target/scale marker, check canvas bounds
and verify repeatable generation. The framework fake is reused with attribution
from [AndreasBoehm's PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143).
It emulates host logic, not the QNX framework ABI or renderer.

```sh
make -C native test
.venv/bin/python tools/test_shift_profiles.py
```

CI repeats the native tests with GCC and Clang and runs profile/plugin callbacks
under AddressSanitizer and UndefinedBehaviorSanitizer. Its preview artifact
composites production host-test model updates with the generated images and XML
geometry. It is explicitly marked as a host simulation, not an emulator or
head-unit screenshot. Generate it locally with
`.venv/bin/python tools/preview_shift_profiles.py` after building the resources
and running `make -C native test-shift-profiles`.

Static host compilation
does not establish ARM/QNX loader behavior. No QNX SDK or factory frameworks are
distributed or obtained by this change. No QNX build, physical N57 validation,
installation, firmware approval or factory CarPlay regression check is claimed.
An authorized parked runtime check remains necessary before shipping.
