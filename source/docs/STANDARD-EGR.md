# Standard EGR gauges and evidence limits

BoostDeck Custom layout can select unspecified EGR request/error fields and
separate EGR A/B request, actual and error fields. These are read-only standard
OBD queries. The word requested describes data returned by the ECU, not an
actuator command sent by the app. Detected diesel defaults remain unchanged.

## Protocol and meaning

| Source | Data after `41 <pid>` | Interpretation |
| --- | --- | --- |
| `01 2C` | Exactly one byte | Requested EGR, raw × 100 / 255 %. No A/B assignment inferred. |
| `01 2D` | Exactly one byte | EGR error, (raw − 128) × 100 / 128 %. Not actual position. |
| `01 69` | Exactly seven bytes | Six support flags followed by A request/actual/error and B request/actual/error. |

For PID 69 the low six bits of its first data byte correspond to those six
fields in order. High bits are rejected. Request/actual fields use raw × 100 / 255
%; error fields use (raw − 128) × 100 / 128 %. Each field needs its own support
bit; an absent actual field cannot be reconstructed from request/error or the
unspecified legacy PIDs. All raw byte values have defined numeric conversions:
zero is a valid request/actual value, 128 means zero error and 255 is not treated
as an unavailable marker.

Reported actual is normalized duty cycle or position. It does not establish
mass flow, an independent position measurement, high/low-pressure hardware
identity, bank assignment, DPF state or an emissions fault. A/B labels retain
the standard field names. No comparison alarm or control action is added.

The scalar conversions were checked against the vendor's public
[CSS Electronics OBD reference](https://www.csselectronics.com/pages/obd2-dbc-file)
and its [v4.5 archive](https://canlogger.csselectronics.com/files/dbc-files/obd-dbc-files-v4.5.zip),
SHA-256 `ab38439676c08e3f48026e37645118af2512c79650dbf0cab9cdd865fd0ae84e`.
Its PID 69 entry is a single-byte placeholder and was not used as a complete
decoder definition. The full field layout was inspected in the vendor's
[pinned OBDb reference](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/signalsets/v3/default.json)
and [Pelican parameter documentation](https://pelican.clutch.engineering/cars/bmw/3-series/parameters).
These are one vendor's related sources, not independent confirmations.
[Bosch's scan-tool manual](https://www.boschdiagnostics.com/sites/default/files/2025-01/OBD%201300%20User%20Manual.pdf)
separately distinguishes actual duty/position, requested duty/position and
error labels; it does not establish this X5's support or scaling.

OBDb publishes its data under [CC BY-SA 4.0](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/LICENSE).
This contribution implements the
protocol arithmetic independently and does not vendor its database entries,
descriptions or polling recommendations. No proprietary BMW dictionary or
vehicle capture is included. The official standard is available through
[SAE J1979-DA](https://saemobilus.sae.org/standards/j1979da_202607-j1979-da-digital-annex-e-e-diagnostic-test-modes).
No third-party copy of that standard is downloaded or redistributed.

## Capability, timing and unavailable states

Every EGR query requires its PID to be advertised through the existing exact
supported-bitmap continuation chain. This also applies when selecting the new
gauges in Gasoline. Old gasoline selections keep their existing query policy.
PID 69 serves all selected A/B fields with one request; their current support
flags are checked on every successful reply. A later absent flag clears that
field. EGR is optional in Custom layout and does not expand the automatic
candidate pool or its capability report.

The EGR polling interval is at least two seconds from the actual request,
including after a slow capability reply. Malformed or refused EGR replies back
off five seconds. Values expire after 3.5 seconds. Missing, malformed,
unsupported and stale values remain blank; a supported zero displays as zero.
The shared transport timeout, lease and cleanup paths are retained. No session
change, dynamic identifier, write or fallback between field meanings is added.

## Packet and settings compatibility

Existing IDs 0–38 remain stable. EGR IDs 39–46 are appended. A frame without
valid EGR keeps the exact prior NBG7 or NBG8 encoding; a synthetic old-core
fixture verifies EGT byte compatibility. NBG8 retains 39 fields, its 1024-byte
bound and mandatory final newline. NBG7 and earlier formats retain their bounds.
Valid EGR uses NBG9 with 47 fields, a mandatory final newline and a 1280-byte
bound. The bounded UI receive buffer is 1696 bytes to also accommodate optional
[pressure](STANDARD-PRESSURE.md) frames; it still drains at most
eight packets per tick. New EGR ranges are validated before display.

B1/B2 retain IDs 0–30 and B3 retains IDs 0–38. Layouts containing EGR use B4;
layouts with only old IDs keep B2/B3. G1/G2 preferences and the D1 mode sidecar
are unchanged. Matching collector, plugin and manifests must be deployed
together after separate authorization. Back up preferences before an update;
an older binary cannot read B4, so rollback needs the saved assignments too.
No packaged QNX binaries or firmware approvals are supplied by this change.

## Offline validation

```sh
make -C source/native test-egr
python3 -B source/native/tests/test_egr_transport.py
python3 -B source/tools/test_boost_defaults_resources.py
```

Synthetic tests cover every support byte and every scalar value, exact sizes,
valid zero/maximum values, absent fields, byte-preserved EGT, strict NBG9
truncation/range guards, actual native callbacks/UDP and B4 migration bounds.
The UI test intercepts launch and never executes a diagnostic worker. Worker
tests cover both transports, request grouping/rates, unsupported-read avoidance,
refusal/backoff/recovery, disappearing flags, unrelated coolant preservation
and unchanged gasoline requests. All 906 original image pixels remain intact.

These are host/simulation results. Exact DDE identity, PID support, A/B meaning,
independent values, QNX loading and physical X5 behavior remain unverified.
The original 16/19-read discovery plans in #201 remain unchanged. Its separate
22-read `standard-diesel` profile can collect EGR and EGT evidence with a v3
sanitized report. Review that exact plan and obtain owner approval before any
physical run; selecting it or decoding a replay grants no authorization.
