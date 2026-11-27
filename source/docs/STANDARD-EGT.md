# Standard exhaust-temperature gauges

BoostDeck can select EGT bank 1/2, sensor 1–4. Each label retains the bank and
sensor number. These identify standard OBD fields, not verified sensor locations
on an N57. No sensor is labelled before/after a turbo, catalyst or DPF without
vehicle-specific evidence. Temperature alone is not used to infer regeneration.

## Protocol evidence

Mode 01 PID `78` reports bank 1 and `79` bank 2. Each response has nine data
bytes after `41 <pid>`: a sensor-support byte followed by four big-endian 16-bit
temperatures. Bits 0–3 select sensors 1–4; unsupported sensors have no value.
Temperature is `raw / 10 - 40` °C.

These fields and scales were cross-checked with the vendor's publicly offered
[CSS Electronics OBD DBC](https://www.csselectronics.com/pages/obd2-dbc-file),
[v4.5 ISO-TP archive](https://canlogger.csselectronics.com/files/dbc-files/obd-dbc-files-v4.5.zip),
and the independent [MIT reference decoder](https://github.com/goabonga/obd-tui/blob/106bdbeb96b69dc5a1b3b8508ff43758cf75eab2/src/obd_tui/models/exhaust.py).
The inspected vendor archive SHA-256 is
`ab38439676c08e3f48026e37645118af2512c79650dbf0cab9cdd865fd0ae84e`.
It is a protocol reference, not a capture from the owner's vehicle. No vendor
database, fault descriptions or sample logs are copied into this repository;
no redistribution grant for those materials is assumed. Decoder code and
synthetic fixtures are independent project source.

## Capability, values and scheduling

The collector follows supported-PID continuation bits through `00`, `20`, `40`
and `60`. An unadvertised or unknown PID cannot be read as EGT. A positive PID
bitmap is still insufficient to select a sensor automatically: a complete bank
reply must establish its sensor-support bits first. The first valid layout is
cached for that worker/endpoint lifetime. A conflicting later layout blanks
that bank and backs off; it cannot silently change the capability report.

One bank request serves all selected sensors in that bank, at most once every
two seconds. Malformed/refused bank reads use a five-second backoff. Requests
reuse the existing lease, transport deadlines and close path. No session change,
dynamic DID or write is added. MOST uses the existing DME tunnel only.

The decoder requires exactly nine bytes and rejects unrecognized high support
bits. Each supported sensor is validated independently. Its app plausibility
envelope is -40 to 1800 °C; values outside it stay unavailable. This is not an
OEM operating limit or an assertion that a particular raw value is a standard
unavailable marker. The visual scale is 0–1200 °C; higher accepted values keep
their numeric reading while the graphic fills. Neither range is a BMW limit.
EGT expires after 3.5 seconds. Missing, invalid or stale values display `--`.
Zero Celsius remains a valid measurement when its sensor is supported.

Detected defaults insert at most one EGT after intake temperature, preferring
the first supported bank/sensor in numeric order. Other supported temperatures
remain selectable in Custom layout. The order is an app policy, not an ECU
profile or a choice of the best DPF sensor.

## Wire and preference compatibility

Original channel IDs 0–30 are unchanged; EGT IDs 31–38 are appended. Internal
signal masks are 64-bit. Ordinary collection still emits the exact 31-channel
`NBG7` packet when no extended channel is valid, including normal gasoline and
legacy demo selections. Valid extended readings use `NBG8`, with 39 channel
fields and a mandatory final newline so truncation of the final number is
rejected. Older `NBG2`–`NBG7` parsing and bounds remain intact.

Automatic reports retain `NBC1` for legacy capability masks and use `NBC2` for
extended evidence. The same token, PID, matching timestamp, freshness and
immutable capability checks apply. The receive buffer is bounded at 1184 bytes;
the UI drains at most eight datagrams per tick. The matching collector, plugin
and manifests must be built and deployed together. This source contribution
does not update the packaged QNX binaries.

Existing B1/B2 assignments retain their original ID bounds and are read without
rewriting them. Assignments using only IDs 0–30 are saved as B2. Choosing an EGT
writes B3, with the same two nine-slot layouts and extended ID bounds. G1/G2
preferences and the D1 layout-mode sidecar are unchanged. Before any authorized
update, back up preferences as well as binaries/resources. An older binary
cannot interpret B3; rollback must restore the saved assignment file too.

## Validation limits

```sh
make -C source/native test-egt
python3 -B source/native/tests/test_egt_transport.py
python3 -B source/tools/test_boost_defaults_resources.py
```

Tests cover all support bytes, sensor scales/bounds, exact lengths, wide-mask
round trips, a synthetic packet emitted by the pinned old core, all 2,097,152
default-candidate subsets, production native callbacks/UDP, B3 persistence and
legacy rejection, both transport paths, unavailable sensors, rate/backoff,
layout conflicts and unchanged gasoline rail reads. The fixture at
`tests/fixtures/boost_legacy_frame.h` is generated from source `12b2e6c`, not a
vehicle. UI launch interception never executes a diagnostic worker. The original
dashboard geometry and all 906 image pixels are preserved; six resource checks
cover native bindings and regeneration. CI includes GCC/Clang, C++98 syntax,
static analysis and sanitizers.

This is host/simulation validation. PID support, sensor identity/position,
independent temperature comparisons, firmware compatibility, QNX loading and
native HMI rendering remain unverified on the owner's exact DDE and X5.
