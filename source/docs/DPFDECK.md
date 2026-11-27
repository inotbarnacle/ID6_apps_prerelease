# DPFDeck development

This branch is an unfinished native DPFDeck contribution. The standard decoder,
worker and host checks are implemented; the native HMI, resources, installer
integration and rollback guide are still required before opening an upstream
app PR. No DPFDeck package, installation, firmware approval, QNX build or
physical vehicle validation is claimed.

## Evidence and supported source scope

The work follows the shared B47/N57 evidence discussion in
[#196](https://github.com/inotbarnacle/iDriveDeck/issues/196). There was no
contributor implementation branch at the checked revision. The separate
[#201 discovery profile](https://github.com/voioo/iDriveDeck/blob/15857d6f084dacb2e4cc21626e06048f7116ea62/source/docs/DIESEL-VALIDATION.md#optional-standard-dpf-pressure-and-temperature-evidence-profile)
can collect standard DPF evidence after review and explicit owner approval.

The worker uses only these Mode 01 reads, all addressed to DME/DDE 12:

- Supported-PID bitmaps 00, 20, 40 and 60, following continuation bits.
- Advertised coolant PID 05.
- Advertised PID 7A/7B bank 1/2 delta, inlet and outlet pressure.
- Advertised PID 7C bank 1/2 inlet/outlet temperatures.

It does not infer an engine family. Exact support, bank identity, pressure
reference and calibration on the owner's N57 or another B47/B57 remain
unverified. [DashLogic's PID reference](https://www.dashlogic.com/docs/technical/obdii_pids)
corroborates the layouts and signed delta pressure in the
[pinned OBDb reference](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/signalsets/v3/default.json).
No vendor database, descriptions or captures are copied into this branch.

Six pressure fields use kPa internally. Delta is signed big-endian / 100;
inlet/outlet are unsigned / 100 and have no assumed absolute/atmospheric
reference. Four temperatures use unsigned / 10 - 40 C; coolant is unsigned
minus 40 C. Pressure support bits 0/1/2 and temperature bits 0/1/2/3 are checked
independently, with exact lengths and reserved-bit rejection. No delta is
derived from inlet/outlet and no generic EGT substitutes for a DPF position.

Temperature above 1800 C remains supported but unavailable in this app. This
is an application plausibility envelope, not a BMW limit or regeneration
threshold. Unlike this app, #201's evidence report retains the full protocol
temperature domain for independent review. FFFF delta is -0.01 kPa and remains
valid; there is no invented universal FFFF sentinel. Supported zero stays valid.
Metric display arithmetic uses mbar and C; imperial uses psi and F.

Regeneration active/requested/inhibited, soot/ash mass, distance/time since the
last regeneration and inhibition reasons remain unimplemented. Temperature or
pressure alone never supplies those states or a completion/service estimate.
There are no commands for regeneration, reset, coding, emissions, actuators,
fault clearing or dynamic DIDs.

## Availability, polling and ownership

The capability helper is identical to #202's shared implementation. A worker
keeps evidence for one DME/endpoint during its lifetime, across short diagnostic
leases. Unknown is distinct from unsupported; failed bitmaps back off five
seconds. A new bitmap consumes the tick before any measurement is sent.

The worker sends at most one diagnostic request per 250-ms tick. Group reads
are no faster than two seconds from their actual start; malformed/refused
reads back off five seconds and clear only that group's valid values. Every
reply checks its current field flags. Samples expire independently after
4.5 seconds, including time spent receiving/publishing/queuing a reply.

HSFZ and the existing DME MOST tunnel share the project's lease and shutdown
paths. Projection-hook installation disables this new worker's MOST fallback.
The existing HSFZ-after-connect policy is preserved; this does not establish
cable-free operation or CarPlay/HUD coexistence on the owner's firmware.

DPFDeck uses separate loopback roles: data 42867, activity/source 42868 and MOST
ownership 42869. These are within #213's excluded host-receiver range. The HUD
consumer checks the new marker; the shared tunnel retains it through teardown.
An existing marker prevents another client. Activity socket ownership prevents
duplicate DPF workers and is released by the OS on process exit. A project
lease cannot exclude an external diagnostic device.

DPF1 is a separate, locale-independent ASCII text protocol with exactly eleven fields, strict masks,
bounded numbers, mandatory final newline and a 512-byte buffer bound. It
includes sequence, worker PID, launch token, monotonic send time and per-field
age. Invalid fields are encoded as zero with their valid bit clear; consumers
must use availability masks, not the numeric placeholder. Unknown, unsupported,
unavailable, stale and valid are separate states. No BoostDeck channel ID,
packet schema, saved setting or existing app layout is changed.

The future HMI must validate the expected live worker PID/token, source address
and port, generation, monotonic high-water mark and packet bounds before using
a frame. Core parsing alone does not establish sender ownership. New fields
require a new wire version rather than changing DPF1's eleven-field meaning.

## Host development checks

```sh
make -C source/native test-dpf
make -C source/native test
```

Tests use a synthetic HSFZ server on loopback and fake DSI/MOST. The private
receiver allocator is identical to #213. Decoder checks cover every 16-bit
value, all support bytes, malformed layouts, endian boundaries, independent
unavailable values, freshness, units and isolated DPF1 framing. Worker checks
cover advertisement, negative/extra-length replies, current flags, timing,
reconnection, duplicate ownership, MOST marker refusal and unregister/cleanup.
The CI also runs existing native/gasoline regressions, GCC/Clang, C++98 header
syntax, core ASan/UBSan and static analysis. It generates a German locale in a
temporary directory to verify the ASCII packet grammar with a comma decimal
locale, without changing the system locale. These are host/simulation checks.

Remaining app work: native HMI and resource generation, production callback/
UDP/worker lifecycle tests, installer staging/layout/conflict/rollback tests,
then a focused draft PR. Exact QNX ABI and physical validation remain separate
owner-authorized milestones. Do not stage the worker by itself on a head unit.
