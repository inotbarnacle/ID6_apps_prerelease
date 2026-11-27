# Engine fault-code handling

Engine Fault Codes reads the engine ECU at address `12` with `19 02 AF`.
Codes remain six hexadecimal digits, including leading zeroes. A 24-bit BMW
UDS code is not automatically an OBD-II P-code; no identifier is shortened or
converted to another DTC format without evidence of that format.

## Supported status bits

The positive response begins `59 02`, followed by a status-availability byte
and four-byte records (three code bytes plus status). Only failure flags shared
by the request, ECU availability and record status select a record. A successful
reply with no supported requested flags is reported as unavailable, not a clean
fault memory. See [AUTOSAR DCM R22-11, SWS_Dcm_00008/00377/00378](https://www.autosar.org/fileadmin/standards/R22-11/CP/AUTOSAR_SWS_DiagnosticCommunicationManager.pdf).

The UI interprets only supported bits. For example, raw status `09` with an
availability mask of `08` means **Confirmed**, not **Active**. The availability
mask accompanies each UI record rather than being discarded by the worker.
Status-bit assignments are cross-checked with the
[udsoncan protocol implementation](https://udsoncan.readthedocs.io/en/latest/_modules/udsoncan/common/dtc.html).

| Bit | Meaning | Primary display when no higher-priority failure flag is set |
| --- | --- | --- |
| 0 | Test failed | Active |
| 2 | Pending code | Pending |
| 3 | Confirmed code | Confirmed |
| 1 | Failure during this operation cycle | This cycle |
| 5 | Failure since clearing memory | Since clear |
| 7 | Warning indicator requested | Warning |
| 4, 6 | Test completion information | No failure conclusion from these alone |

These are read snapshots. **Confirmed** does not mean currently failing, and
**Since clear** does not establish whether a fault is repaired. Unknown support
does not become a zero/false state. A legacy worker record without an availability
mask is labelled **Raw**, with no inferred active/pending/confirmed classification.
All supported status bits remain inspectable in the raw status and mask; the
compact row shows one primary label and is not a complete diagnostic assessment.

## DDE/raw and reference views

Choose **OPTIONS → Description profile → DDE / unknown ECU: raw codes** on a
diesel or unverified ECU. This view suppresses the existing gasoline dictionary,
shows the numeric code and supported status, and states that the description is
unavailable. This is a display choice, not ECU identification or a DDE calibration.

The legacy gasoline lookup remains available with an explicit **gas ref** label
for existing installations. Its original table is unchanged; its applicability
is not asserted for other engines. Selecting a description profile does not start
diagnostics. It saves a validated four-byte `E1` preference at
`/var/opt/hmi/data/ID5/EngineCodes.prefs`, outside Plugins. Corrupt or unreadable
preferences fail closed to the raw view. With no preference file, the legacy
reference view remains the default. Save failures are indicated on the profile page.

No new fault descriptions or BMW diagnostic database are included. The source
referenced by the existing table distinguishes its MIT tooling from the
underlying BMW-derived data rights; see its
[license](https://github.com/gediz/bmw-unified-db/blob/0b2f53b3e298aa4f8700eaf704cdfae1d8e9e8c5/LICENSE).
A DDE-specific dictionary still needs verified ECU applicability and established
redistribution rights. The separate standard OBD read path below never converts
a BMW 24-bit record or uses this reference dictionary.

## Standard emissions-code reads

OPTIONS offers explicit **Read standard confirmed/pending/permanent emissions
codes** choices. The ordinary **Read engine codes** action remains the BMW UDS
read. Each standard choice sends one read to the existing engine ECU address:
`03` for confirmed, `07` for pending, or `0A` for permanent emission-related codes.
These categories follow [AUTOSAR DCM R22-11 §7.6.3.5,
SWS_Dcm_00289/00412/00330](https://www.autosar.org/fileadmin/standards/R22-11/CP/AUTOSAR_SWS_DiagnosticCommunicationManager.pdf).
Confirmed or permanent does not establish a current failure. A zero result
applies only to the selected category, not all fault memory. Permanent-code
removal is ECU-managed; the standard-read display states this explicitly.

The parser accepts the CAN-counted form: positive service, one count byte,
then that many two-byte codes. Its P/C/B/U formatting and CAN count are
cross-checked against [ELM327DSJ p. 34](https://www.elmelectronics.com/wp-content/uploads/2016/07/ELM327DS.pdf).
It rejects incomplete counts, zero codes inside the count, nonzero trailing
data, odd-length payloads, wrong services and replies over 512 bytes. Extra
whole-pair zero filler is allowed. Older uncounted formats are unsupported;
the parser never guesses a format from plausible digits.

CLI choices are `--obd-confirmed`, `--obd-pending`, and `--obd-permanent`.
They use the shared diagnostic lease and transport timeouts: a 1.8-second HSFZ
request wait and a two-second nominal MOST wait, with MOST response-pending
waits bounded at five seconds. Link
setup and cleanup are separate. There is no retry, session change, clear, or
automatic fallback. Mixing these choices with
clearing, a status-mask request or tracing is rejected before connecting.
Refusals, lost replies and malformed data stay unavailable. The worker reports
the full category count but sends at most 128 records to the bounded UI pipe.
The display labels their service category independently of its saved description
profile. No standard-code description dictionary is included.

These are protocol fixtures and host tests, not captures or confirmed N57
support. The target DDE's implementation and gateway payload format remain
unverified. The matching plugin and worker must be built together; no additional
live command is authorized by this source contribution.

## Read default and clear confirmation

The worker defaults to reading when no operation is specified. `--codes` remains
supported. Its ordinary text records retain the existing CLI format; the plugin
opts into `--status-mask` for the additional `mask XX` field. Both the plugin and
worker must be updated together. The existing BMW `--codes` wire request and ECU
target are unchanged. An unsupported read, malformed record or lost link never falls back
to clearing memory or changing a diagnostic session.

Clearing still uses **Clear engine codes… → Confirm clear engine codes**.
The confirmation callback now supplies an additional backend confirmation flag.
Command-line use requires **both** `--clear` and `--confirm-clear`; an unconfirmed
or misplaced confirmation is rejected before a gateway connection or lease is
opened. Existing scripts that deliberately clear must add the second flag.
No tool in this contribution has cleared a physical vehicle's fault memory.

The previously existing confirmed-clear transaction and readback/session cleanup
remain bounded. It is not called by a read or profile change. The confirmation
flag is a software guard, not permission to perform a live vehicle test: owner
authorization is still required for vehicle commands that change state.

## Validation and packaging

The C parser validates prefix, exact complete-record length, maximum size and
24-bit byte order. HSFZ truncated records remain invalid. The existing limited
MOST reply can retain complete records with a visible **More codes may exist**
notice. Output is capped at 128 records and the metadata form fits the plugin's
4096-byte pipe buffer. UI parsing accepts only exact-width hexadecimal fields;
signs, `0x` prefixes and malformed/trailing fields cannot acquire a fault status.

Host tests cover all status/mask pairs, legacy/raw fields, leading zeroes,
negative/malformed/lost replies, unsupported status masks, storage errors,
production view/persistence callbacks and the read/clear argument boundary.
The UI callback tests use an argv-echo shell stub, not a diagnostic worker.
Mock ECU tests use loopback HSFZ and fake MOST. The framework fake is reused from
[PR #143](https://github.com/inotbarnacle/iDriveDeck/pull/143), also used in #203.
CI runs GCC/Clang, static analysis, AddressSanitizer and UndefinedBehaviorSanitizer.
The separate OBD decoder tests cover count/filler/format boundaries and all
possible service bytes. Eight mock transport tests exercise the three standard
categories, unavailable results, finite pending waits, output bounds, MOST
cleanup and invalid argument combinations. Production callback tests verify
the exact read flags and prevent description-profile changes from reinterpreting
standard results. The original BMW read/clear regressions still run.

```sh
make -C native test
make -C native test-faults
python3 tools/test_fault_resources.py
```

The updated XML adds a native settings page; existing dashboard geometry,
background image, connector and clear-confirmation controls are preserved.
No new raster image is referenced, so the original EngineCodes image archive is
staged unchanged with the new XML, plugin and worker. Public packages are not
replaced by this source contribution. Save original files and preferences before
any separately authorized installation. Restore the original preferences during
rollback; if none existed, remove only the newly created app preference file to
restore the legacy description-view default.

These results are host/simulation evidence only. The owner's exact N57 variant,
DDE/EGS software, firmware/hashes and physical fault behavior remain unverified.
No QNX build, head-unit installation, restart or factory-function validation is
claimed. Licensed descriptions, ECU identity matching and parked validation
remain open requirements.
