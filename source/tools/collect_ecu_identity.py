#!/usr/bin/env python3
"""Plan, replay or collect six standard ECU identity reads.

Live use requires separate owner approval while parked. Raw data records are
private and uninterpreted. The shareable summary exports neither values nor
hashes of values. See ECU-IDENTITY.md before reviewing the plan.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import time

from discover_diesel import reply_bytes
from probe_signals import enet_address, exchange

DIDS = ((0xf191, "vehicle_manufacturer_ecu_hardware_number"),
        (0xf188, "vehicle_manufacturer_ecu_software_number"),
        (0xf189, "vehicle_manufacturer_ecu_software_version_number"))
REQUESTS = tuple((ecu, bytes((0x22, did >> 8, did & 255)))
                 for ecu in (0x12, 0x18) for did, _ in DIDS)
ALLOWLIST = frozenset(REQUESTS)
MAX_SECONDS = 45
MAX_REPLAY_BYTES = 1024*1024


def plan():
    return {"format": "idrive-ecu-identity-plan-v1",
            "requests": [{"ecu": f"{ecu:02x}", "request": request.hex(" ")}
                         for ecu, request in REQUESTS],
            "transport": "HSFZ TCP 6801; explicit wired link-local endpoint only",
            "max_requests": 6, "max_duration_seconds": MAX_SECONDS,
            "minimum_gap_seconds": 0.2, "transaction_reserve_seconds": 6.2,
            "retries": 0, "writes_or_session_changes": False, "ssh_or_most": False,
            "vin_serial_or_programming_fingerprint_reads": False,
            "raw_records": "private; manufacturer encoding is not established",
            "public_summary": "response status only; no raw values or value hashes"}


def normalize(rows):
    """Keep only bounded replies to the six reads, including ambiguous duplicates."""
    if not isinstance(rows, list) or len(rows) > 256:
        raise ValueError("expected at most 256 identity records")
    result = []
    for row in rows:
        if not isinstance(row, dict):
            continue
        try:
            ecu = int(row.get("ecu", ""), 16)
            request = bytes.fromhex(row.get("request", ""))
        except (TypeError, ValueError):
            continue
        if (ecu, request) not in ALLOWLIST:
            continue
        item = {"ecu": f"{ecu:02x}", "request": request.hex(" ")}
        reply = reply_bytes(row)
        if reply is not None:
            item["reply"] = reply.hex(" ")
        else:
            item["error"] = "no_valid_response"
        result.append(item)
    return result


def collect(local, gateway):
    """Execute only the fixed plan; the CLI enforces prior owner approval."""
    rows = []
    deadline = time.monotonic()+MAX_SECONDS
    for ecu, request in REQUESTS:
        if time.monotonic()+6.2 > deadline:
            break
        row = {"ecu": f"{ecu:02x}", "request": request.hex(" ")}
        try:
            with socket.create_connection((gateway, 6801), timeout=2,
                                          source_address=(local, 0)) as connection:
                response = exchange(connection, ecu, request)
            reply = reply_bytes(response)
            if reply is not None:
                row["reply"] = reply.hex(" ")
            else:
                row["error"] = "no_valid_response"
        except (OSError, ValueError):
            row["error"] = "no_valid_response"
        rows.append(row)
        time.sleep(0.2)
    return rows


def public_report(rows, evidence="replay_unverified_provenance"):
    if evidence not in ("replay_unverified_provenance", "parked_live_read_only"):
        raise ValueError("unknown evidence class")
    records = {}
    for row in normalize(rows):
        key = (row["ecu"], row["request"])
        records[key] = row if key not in records else None
    fields = []
    observed = False
    for ecu in (0x12, 0x18):
        for did, meaning in DIDS:
            request = bytes((0x22, did >> 8, did & 255))
            key = (f"{ecu:02x}", request.hex(" "))
            row = records.get(key)
            reply = reply_bytes(row)
            field = {"ecu": f"{ecu:02x}", "did": f"{did:04X}",
                     "expected_standard_meaning": meaning,
                     "record_status": "not_present" if key not in records else
                     "ambiguous_duplicate" if row is None else "no_valid_response",
                     "identifier": "unavailable_pending_private_review"}
            if reply is not None:
                if reply[:3] == bytes((0x62, did >> 8, did & 255)) and 4 <= len(reply) <= 259:
                    field.update(record_status="positive_uninterpreted", data_bytes=len(reply)-3)
                    observed = True
                elif len(reply) == 3 and reply[:2] == b"\x7f\x22" and reply[2] != 0x78:
                    field.update(record_status="negative_response", nrc=f"{reply[2]:02X}")
                    observed = True
                else:
                    field["record_status"] = "malformed_or_out_of_bounds"
            fields.append(field)
    return {"format": "idrive-ecu-identity-evidence-v1", "evidence": evidence,
            "fields": fields, "hsfz": ("response_observed" if evidence == "parked_live_read_only" else
                                        "replay_shape_only") if observed else "not_established",
            "raw_values_exported": False, "raw_value_hashes_exported": False,
            "hardware_software_identified": False, "engine_family": "not_inferred",
            "firmware_approval": "not_established", "physical_installation": "not_performed_by_this_tool",
            "limitations": ["Standard DID assignments do not establish BMW support or manufacturer record encoding",
                            "Positive replies require private review and comparison with an authorized ECU inventory",
                            "Negative responses do not establish permanent lack of support; no session/security retry is attempted",
                            "No VIN, serial, programming fingerprint, raw identity value or identity hash is exported",
                            "No exact N57/EGS variant, HWEL/SWFL mapping, QNX ABI or standalone operation is established"]}


def load_records(path):
    with path.open("rb") as source:
        data = source.read(MAX_REPLAY_BYTES+1)
    if len(data) > MAX_REPLAY_BYTES:
        raise ValueError("replay exceeds 1 MiB")
    return normalize([json.loads(line) for line in data.decode("utf-8").splitlines() if line.strip()])


def write_private(path, text):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as output:
        output.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--plan", action="store_true")
    modes.add_argument("--replay", type=Path, help="existing private probe JSONL; no connection")
    modes.add_argument("--collect", action="store_true")
    parser.add_argument("--owner-approved-parked", action="store_true")
    parser.add_argument("--local", type=enet_address)
    parser.add_argument("--gateway", type=enet_address)
    parser.add_argument("--out", type=Path, help="new private output directory; never reused")
    args = parser.parse_args()
    if args.collect:
        if not args.owner_approved_parked or not args.local or not args.gateway or args.local == args.gateway:
            parser.error("collection needs approval of this exact parked plan and separate wired endpoints")
    elif args.owner_approved_parked or args.local or args.gateway:
        parser.error("network and approval options apply only to --collect")
    if args.plan:
        if args.out:
            parser.error("--plan prints to stdout without creating files")
        print(json.dumps(plan(), indent=2))
        return
    if not args.out:
        parser.error("--out is required for a new private evidence directory")
    try:
        rows = load_records(args.replay) if args.replay else None
        # Preflight before any live request. An existing folder or symlink fails.
        args.out.mkdir(mode=0o700)
        if args.collect:
            rows = collect(args.local, args.gateway)
        rows = normalize(rows)
        evidence = "replay_unverified_provenance" if args.replay else "parked_live_read_only"
        write_private(args.out/"private-records.jsonl", "".join(json.dumps(row)+"\n" for row in rows))
        write_private(args.out/"summary.json", json.dumps(public_report(rows, evidence), indent=2)+"\n")
        print("Created private identity evidence and a status-only summary. Review before sharing.")
    except (OSError, ValueError) as error:
        raise SystemExit("Could not create identity evidence ("+type(error).__name__+").") from None


if __name__ == "__main__":
    main()
