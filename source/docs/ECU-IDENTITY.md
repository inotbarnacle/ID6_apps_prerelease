# Standard ECU identity evidence

`tools/collect_ecu_identity.py` is a separate host tool for six bounded reads of
standard hardware/software identity DIDs. It can plan or replay offline. Physical
collection needs explicit owner approval of this exact plan while parked; ENET
availability or approval of an earlier discovery plan is not authorization.
The owner's vehicle has not been queried with this tool.

The existing SGBD probe does not establish full DDE/EGS identity. This tool
provides private evidence for independent review, without guessing a BMW codec,
engine family or HWEL/SWFL interpretation. Its public summary never exports
identity values or hashes of them. Unknown manufacturer records could contain
identifying data, so hashing them is not treated as anonymization.

## Fixed request set

Each row below is read once from ECU address 12 and once from 18, using separate
HSFZ TCP 6801 connections and one DID per request. These are the project's
engine/transmission address candidates, not independently confirmed ECU types.

| Request | Standard assignment |
| --- | --- |
| 22 F191 | Vehicle manufacturer ECU hardware number |
| 22 F188 | Vehicle manufacturer ECU software number |
| 22 F189 | Vehicle manufacturer ECU software version number |

Assignments are corroborated by the maintained
[udsoncan 1.22.1 reference](https://udsoncan.readthedocs.io/en/v1.22.1/udsoncan/helper_classes.html#dataidentifier)
and [uds_protocol reference](https://docs.rs/uds_protocol/latest/uds_protocol/enum.UdsIdentifier.html)
(inspected version 0.7.0). The
[DidCodec documentation](https://udsoncan.readthedocs.io/en/v1.22.1/udsoncan/helper_classes.html#didcodec)
also makes the need for an explicit record codec clear. These references do not
establish BMW support, record length, byte order, text encoding or applicability
to this X5. No third-party library or database is bundled or required.

The fixed plan has at most six requests, a 200-ms gap, no retries and a 45-second
budget. It reserves 6.2 seconds before starting another transaction: two seconds
each for connect/send/receive plus the gap. Heartbeat acknowledgements share the
receive deadline. Time or transport failures leave missing evidence unavailable.
Negative responses are retained as statuses, without session or security retries.

There are no arbitrary DID/ECU parameters, VIN/serial/programming-fingerprint
reads, discovery scans, ECU writes, session changes, fault clears, actuators,
SSH, MOST or installation commands. The tool reuses only the existing HSFZ
exchange; it does not instantiate a UDS client with implicit session behavior.
Existing standard PID profiles and reports remain unchanged.

For an approved live session, use one collector and close other diagnostic
clients and head-unit telemetry apps. A laptop cannot exclude external testers
or claim the head unit's project lease. Stop for unexpected warnings, freezes
or communication/factory-function regressions.

## Offline use and private output

Run from `source/`:

```sh
python3 tools/collect_ecu_identity.py --plan
python3 tools/collect_ecu_identity.py --replay /private/existing-probe.jsonl \
  --out /private/new-ecu-identity-evidence
python3 tools/test_collect_ecu_identity.py
```

The output directory must be new and is created with mode 0700. Both files use
0600 and exclusive creation. Existing directories, files and symlinks are
rejected before any live request. Keep the directory outside the checkout.

- `private-records.jsonl` contains bounded raw replies only for the six allowed
  requests. Keep it private; inspect it locally alongside an authorized inventory.
- `summary.json` contains fixed DID labels, response status, positive record
  lengths and negative response codes. It includes no raw value, hash, endpoint,
  VIN, serial, free text or copied capture metadata. Review it before sharing.

A positive response requires the exact 62/DID prefix and 1–256 data bytes to be
classified as `positive_uninterpreted`. That bound is an evidence-summary policy,
not a standard or BMW record-length claim. Longer bounded replies remain in the
private records for review and are marked out of bounds in the summary.
Unsupported, refused, malformed, duplicate and absent evidence never becomes
an identified hardware/software version. A negative response does not establish
permanent absence of support.

Replay is labeled `replay_unverified_provenance`, with HSFZ marked as a replay
shape only. The tool never upgrades replay to physical evidence. Successful live
collection would indicate observed replies, while `hardware_software_identified`
always remains false in this tool. A separate private review must establish any
identification claim. It never approves firmware or
populates an ECU signal profile automatically.

After separate approval, the CLI requires `--collect --owner-approved-parked`,
explicit distinct wired link-local `--local`/`--gateway` addresses and a new
`--out` directory. Review `--plan` first. A code flag records an approval already
given; it does not grant permission to use the physical vehicle.

## Host validation and remaining evidence

Fourteen tests cover exact request scope/order, timing, no retries, refusals,
positive/negative bounds, duplicate and malformed records, privacy, output
permissions/exclusivity and approval gates. Six real HSFZ exchanges run over
local stream socket pairs with split replies, heartbeat/pending responses and
wrong-ECU/DID messages. They cannot reach a vehicle or external network.
The 62 earlier discovery, four probe and six head-unit capture cases also pass.

Physical support remains unknown. Before describing an ECU as identified, match
its raw records against a trusted inventory, verify the manufacturer encoding
and confirm the exact DDE/EGS family and hardware/software scope. Preserve the
private provenance and redact identifying data before sharing any manually
reviewed values. This does not replace exact framework hashes, QNX ABI checks,
parked app/factory-function validation or standalone transport testing.
