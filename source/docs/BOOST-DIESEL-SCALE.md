# BoostDeck diesel rail display range

Adapted from [voioo's PR #205](https://github.com/inotbarnacle/iDriveDeck/pull/205)
for the pinned N57 profile and existing production-plugin test fixture.

Select **OPTIONS → BoostDeck settings → Diesel rail display scale** to choose
0–1,000, 0–1,600, 0–2,000 or 0–2,500 bar. The default is 0–2,000 bar. These are
display choices, not ECU pressure targets or BMW operating limits. The selected
range is used only by Diesel/pinned N57; gasoline retains its 0–250 bar scale.

Bars, circular dials and the ninth vertical tile share the same range. The
caption and choices follow the selected pressure units; changing units does
not change the fill fraction. **SI (kPa / MPa)** uses kPa for boost/MAP and MPa
for diesel rail pressure: 1800 bar is displayed as 180.0 MPa, and the 2500-bar
range as 0–250 MPa. The unit stays stable across readings; one decimal in MPa
preserves the existing whole-bar display precision. The existing pressure
preference encoding is unchanged. Numeric readings are not clipped to the graphic
range. Unsupported and stale data remain unavailable. No diagnostic request,
DDE scaling, F303 transaction, pin, freshness or emissions-selection rule changes.

The narrow ninth tile displays its number above its unit (for example, `230.0`
above `kPa`) in both bar and dial styles. The other eight tiles keep their
combined number and unit. Missing or stale readings clear the separate unit.

The range is saved atomically in a separate three-byte file:
`/var/opt/hmi/data/ID5/BoostDeck.rail-scale`, containing `R`, version `1`, and
choice `0`–`3`. A new instance defaults to 2000 bar; a missing or invalid file
leaves its current choice intact. A failed save keeps
the previous choice and shows an error. Changing this setting does not rewrite
`BoostDeck.prefs`/`BoostDeck.signals` or restart the collector. Older binaries
ignore this separate file; preserve it with ordinary settings during backup.

The host tests exercise production callbacks and UDP rendering alongside the
existing N57 capability/profile checks: all ranges, bars/dials/vertical,
pressure units, numeric overflow, missing/stale readings, persistence and save
failure. Resource checks preserve existing geometry/image pixels and N57
selectors while reproducing the generated menu. Run:

```sh
make -C source/native test
python3 -B source/tools/test_boost_scale_resources.py
```

These synthetic checks do not validate a physical rail-pressure source or
replace target-firmware rendering checks.
