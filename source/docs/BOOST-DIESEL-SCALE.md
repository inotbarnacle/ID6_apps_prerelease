# BoostDeck diesel rail display range

BoostDeck previously used its gasoline 0–250 bar rail-pressure display range in
both engine profiles. A synthetic standard PID `23` response `41 23 27 10`
decodes to 1,000 bar, but the bar and circular dial were already full at 250 bar.
The numeric reading was correct; the graphic could not distinguish higher values.

Select **BoostDeck settings → Diesel rail display scale** to choose a 0–1,000,
0–1,600, 0–2,000 or 0–2,500 bar scale. The default is 0–2,000 bar. These are
application display choices, not BMW specifications, ECU identification, pressure
targets, safe operating limits or a claim that the selected vehicle supports rail
telemetry. Select Diesel in the existing Engine profile menu to use this scale.

The rail tile caption shows the selected range in the same units as the reading
below it. Pressure-unit changes convert the caption and settings choices between
bar, psi and kPa without changing the fill fraction. Horizontal bars, circular
dials and the narrow ninth tile all use the same scale. At 1,000 bar on the default
scale the graphic is half full. Values beyond the chosen range fill the graphic
and retain their full numeric value. Unavailable or stale readings remain `--`.
The gasoline profile retains its existing 250-bar range and captions; other
signals retain their current ranges in both profiles.

## Saved settings and rollback

The scale uses a separate three-byte, versioned `R1` file:
`/var/opt/hmi/data/ID5/BoostDeck.rail-scale`. Missing, malformed, unsupported-version
or out-of-range files leave the default choice. Saves use the existing atomic
temporary-file/rename helper; a failed save reports an error and retains the
previous choice. The older `G1`/`G2` `BoostDeck.prefs` and `B1`/`B2`
`BoostDeck.signals` readers and writers are unchanged. Changing the scale writes
neither of those files and does not restart a collector or send diagnostics.

An older BoostDeck binary ignores the separate scale file and can still read the
ordinary preferences and gauge assignments. Restore the complete previous app
folder, including its matching XML and image archive, for rollback. Preserve the
preferences and scale file with the backup. This change does not authorize an
installation or restart on a vehicle.

## Reproduce offline checks

```sh
make -C source/native test
python3 -m venv /tmp/idrive-boost-scale-env
/tmp/idrive-boost-scale-env/bin/pip install Pillow==13.0.0
/tmp/idrive-boost-scale-env/bin/python -B source/tools/test_boost_scale_resources.py
```

The two scale tests exercise production PID decoding, every display choice,
invalid numeric input and persistence, unchanged gasoline/other-signal ranges,
actual plugin callbacks, horizontal/vertical/dial image selection, unit changes,
stale/unsupported blanks, profile switching, restart and save failures. The plugin
test uses a host BMW framework stand-in from Andreas Boehm's MIT-licensed
[PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143); it never displays
the live dashboard or launches a diagnostic worker. Resource tests check the
generated menu, model/action IDs, original dashboard geometry and initial image
IDs, plus all 906 regenerated image dimensions/pixels against the package. PNG
compression bytes may vary with Pillow. The image-generation algorithm and raster assets are unchanged; the new
control is native text. Run `make -C source/native assets` to stage matching assets
when preparing a source build; generated native outputs remain private/ignored.

CI runs GCC/Clang host regressions, C++98 plugin syntax, Clang static analysis and
AddressSanitizer/UndefinedBehaviorSanitizer for both new tests. These are synthetic
host checks. They do not establish QNX ABI/loader compatibility, actual HMI text
wrapping, DDE support or an N57/X5 rail-pressure measurement. No request, response
format, scaling, validity rule, worker packet, firmware profile or vehicle setting
is changed by this display feature.
