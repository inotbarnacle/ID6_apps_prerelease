# Standard requested and actual pressure gauges

BoostDeck Custom layout can select rail A/B requested and actual gauge pressure
and boost A/B requested and actual absolute pressure. These are optional data
returned by standard read-only Mode 01 requests. Selecting a requested value
does not send a pressure target or actuator command. Detected defaults and the
existing MAP, derived boost and rail sources remain unchanged.

## Formats and references

| PID | Exact data after `41 <pid>` | Pressure fields | Conversion |
| --- | --- | --- | --- |
| `6D` | 11 bytes, A through K | B–C request A, D–E actual A, G–H request B, I–J actual B | Unsigned big-endian raw / 10 bar, relative to atmosphere |
| `70` | 10 bytes, A through J | B–C request A, D–E actual A, F–G request B, H–I actual B | Unsigned big-endian raw / 32 kPa, absolute |

In both formats A is the support byte. Bits 0, 1, 3 and 4 respectively authorize
request A, actual A, request B and actual B. Bits 2 and 5 describe temperature
fields in 6D and control status in 70; those fields are not displayed. High
support bits are unrecognized and rejected. Unused temperature/status bytes
are ignored rather than interpreted as invalid pressure or a fault.

Each displayed pressure requires its own support bit. Request and actual never
substitute for one another. A/B names preserve the standard labels and do not
establish physical rail/turbo identity or the location of a sensor. PID 6E is
injection-control pressure and is not treated as rail pressure; PID 6F is not
used. PID 70 does not replace manifold pressure or derive gauge boost. No
atmospheric reference is assumed or subtracted from these absolute fields.

The full layouts and rail atmospheric reference were inspected in the vendor's
[pinned OBDb reference](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/signalsets/v3/default.json).
The independent preOBD project's
[PID reference](https://github.com/preobd/preOBD/blob/45b371e2fd0edf0001971c0768754c237de7b2ce/docs/reference/OBD2_PID_REFERENCE.md)
and [encoder tests](https://github.com/preobd/preOBD/blob/45b371e2fd0edf0001971c0768754c237de7b2ce/test/test_obd_encode/test_obd_encode.cpp)
confirm PID 70's ten-byte shape, support bit 1, actual-A offset and absolute
conversion. Its raw 6442 fixture decodes to 201.3125 kPa, including quantization.
Its synthetic fallback atmospheric value is not used by this application.
The previously inspected CSS v4.5 DBC has single-byte placeholders for these
PIDs and is not evidence for their complete layouts. These references do not
establish support on a BMW N57 or this owner's X5.

OBDb data is published under
[CC BY-SA 4.0](https://github.com/OBDb/SAEJ1979/blob/1abdef6ae0c1b9134c8f2a10911a9daa1354649d/LICENSE).
This implementation uses independent protocol arithmetic and synthetic tests;
it does not vendor database entries, descriptions or polling recommendations.
No BMW diagnostic dictionary or proprietary standard is redistributed.

## Availability, timing and display

Both PIDs require the supported-bitmap continuation chain through 00/20/40/60,
including when selected in Gasoline. One request per PID serves its selected
fields, no faster than every two seconds measured at the actual request.
Malformed/refused reads back off five seconds; supported values expire after
3.5 seconds. Current field flags are checked on every reply. Missing flags,
unknown capabilities, invalid lengths and stale data leave gauges unavailable.
The existing transport deadlines, lease and cleanup paths apply.

The complete unsigned range has a defined conversion: 0–6553.5 bar for rail and
0–2047.96875 kPa absolute for boost. Supported zero and FFFF are not invented
unavailable markers. Packet input additionally rejects negative or out-of-range
valid pressures. No tighter BMW plausibility range is asserted without evidence.
Native pressure settings convert all eight gauges to psi, bar or kPa. Boost
captions explicitly retain `abs` in both wide and narrow tiles.

Visual scales are fixed app choices: 0–2000 bar for these rail fields and
0–500 kPa absolute for boost. A higher accepted reading retains its numeric
value while the graphic fills. These are neither OEM limits nor requested
targets. The separate legacy rail-scale setting in #205 applies to the original
rail channel only. No pressure comparison alert or control action is added.

## Wire, preferences and rollback

Existing IDs 0–46 remain stable; pressure appends IDs 47–54. Existing NBG7/NBG8/
NBG9 schemas and their 768/1024/1280-byte bounds remain intact. Synthetic golden
fixtures verify prior encoders byte for byte, including NBG9 from source
`5c750246`. A frame with valid new pressure uses NBG10: 55 fields, a mandatory
newline and a 1536-byte bound. The parser explicitly handles the five-character
tag and rejects longer prefix lookalikes. The combined receive bound becomes
1696 bytes; the eight-datagram drain limit remains.

B1/B2 retain IDs below 31, B3 below 39 and B4 below 47. New pressure assignments
use B5 with IDs below 55; old-only selections still save their older revision.
G1/G2 and D1 sidecars are unchanged. Matching collector, plugin and manifests
must be deployed together after separate owner authorization. Back up binaries,
resources and preferences: an older plugin cannot read B5, so rollback must
restore its saved assignments. No QNX binaries or firmware approval are added.

## Offline checks and remaining evidence

```sh
make -C source/native test-pressure
python3 -B source/native/tests/test_pressure_transport.py
python3 -B source/tools/test_boost_defaults_resources.py
```

Synthetic tests cover every support byte and every unsigned pressure value,
exact lengths, independent encoder vectors, prior NBG9 bytes, NBG10 framing,
units, visual clamping, production callbacks/UDP, B5 restoration and old bounds.
Worker cases cover both transports, unsupported/unknown capabilities, field
disappearance, valid zero, grouped timing, backoff/recovery, slow capability
responses, unaffected coolant and unchanged gasoline/default selections.
UI launch is intercepted and cannot execute a worker. All original dashboard
geometry and 906 image pixels are retained.

The owner's exact DDE support, hardware identities, independent pressure
comparisons, QNX loading and physical HMI behavior remain unverified. The
16/19/22-read discovery profiles in #201 are unchanged and do not collect these
new pressure PIDs. Any future pressure capture needs its own reviewed plan and
explicit owner approval before use on the physical vehicle.
