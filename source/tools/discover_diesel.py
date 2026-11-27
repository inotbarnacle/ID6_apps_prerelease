#!/usr/bin/env python3
"""Plan, replay or collect a bounded read-only diesel compatibility report.

Planning and replay never open sockets. Live collection requires owner approval
and a parked vehicle; read DIESEL-VALIDATION.md and review --plan first.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import time

from probe_signals import enet_address, exchange

PIDS = (0x04, 0x05, 0x0b, 0x0c, 0x0f, 0x10, 0x23, 0x33, 0x42, 0x5c, 0x5e)
WIDTHS = {pid: 2 if pid in (0x0c, 0x10, 0x23, 0x42, 0x5e) else 1 for pid in PIDS}
MAX_REQUESTS, MAX_SECONDS = 16, 45
FRAMEWORKS = ("libAppFramework.A.so", "libCoreFramework.A.so")
ALLOWLIST = {(ecu, bytes.fromhex("22f150")) for ecu in (0x12, 0x18)} | {
    (0x12, bytes((1, pid))) for pid in (0, 0x20, 0x40, *PIDS)
}


def plan():
    return {
        "transport": "HSFZ TCP 6801, explicit wired link-local endpoint only",
        "identity": [{"ecu": f"{ecu:02x}", "request": "22 f1 50"} for ecu in (0x12, 0x18)],
        "capabilities": ["01 00", "01 20", "01 40"],
        "measurements_if_advertised": [f"01 {pid:02x}" for pid in PIDS],
        "max_requests": MAX_REQUESTS,
        "max_duration_seconds": MAX_SECONDS,
        "minimum_gap_seconds": 0.2,
        "retries": 0,
        "writes_or_session_changes": False,
        "ssh_or_most": False,
    }


def collect(local, gateway):
    rows = []
    deadline = time.monotonic() + MAX_SECONDS

    def read(ecu, request):
        assert (ecu, request) in ALLOWLIST
        # Two seconds to connect plus the exchange's two-second deadline.
        if len(rows) >= MAX_REQUESTS or time.monotonic() + 4.2 > deadline:
            return None
        row = {"ecu": f"{ecu:02x}", "request": request.hex(" ")}
        try:
            with socket.create_connection((gateway, 6801), timeout=2,
                                          source_address=(local, 0)) as connection:
                row.update(exchange(connection, ecu, request))
        except (OSError, ValueError):
            row["error"] = "no_valid_response"
        rows.append(row)
        time.sleep(0.2)
        return row

    for ecu in (0x12, 0x18):
        read(ecu, bytes.fromhex("22f150"))
    supported = set()
    for base in (0, 0x20, 0x40):
        row = read(0x12, bytes((1, base)))
        reply = reply_bytes(row)
        if reply is None or len(reply) != 6 or reply[:2] != bytes((0x41, base)):
            break
        mask = int.from_bytes(reply[2:], "big")
        supported.update(base+offset for offset in range(1, 33) if mask & (1 << (32-offset)))
        if not mask & 1:
            break
    for pid in PIDS:
        if pid in supported:
            read(0x12, bytes((1, pid)))
    return rows


def reply_bytes(row):
    if not isinstance(row, dict) or not isinstance(row.get("reply"), str):
        return None
    try:
        value = bytes.fromhex(row["reply"])
    except ValueError:
        return None
    return value if len(value) <= 8192 else None


def safe_headunit(value):
    """Reconstruct allowed public fields; never copy arbitrary capture metadata."""
    if not isinstance(value, dict):
        return None
    out = {}
    firmware = value.get("firmware", "")
    if isinstance(firmware, str) and re.fullmatch(r"NBTevo_[A-Z][0-9]{5}[A-Z]", firmware):
        out["firmware"] = firmware
    if value.get("architecture") == "armle":
        out["architecture"] = "armle"
    if value.get("ssh_node") in ("hu-jacinto", "hu-omap", "unknown"):
        out["ssh_node"] = value["ssh_node"]
    display = value.get("display")
    if isinstance(display, dict) and all(type(display.get(k)) is int and 100 <= display[k] <= 4096
                                         for k in ("width", "height")):
        out["display"] = {k: display[k] for k in ("width", "height")}
    hashes = value.get("frameworks")
    if isinstance(hashes, dict):
        out["frameworks"] = {}
        for name in FRAMEWORKS:
            entry = hashes.get(name)
            if isinstance(entry, dict) and isinstance(entry.get("sha256"), str) and re.fullmatch(
                    r"[0-9a-f]{64}", entry["sha256"]):
                out["frameworks"][name] = {"sha256": entry["sha256"]}
    return out or None


def report(rows, headunit=None, evidence="replay"):
    """Export only known fixed requests and validated response shapes.

    In particular no complete F150 payload, raw diagnostic errors, arbitrary
    DID responses, VIN, hostnames, IP addresses or manual free text survive.
    """
    if not isinstance(rows, list) or len(rows) > 256:
        raise ValueError("expected at most 256 probe records")
    records = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        try:
            ecu = int(row.get("ecu", ""), 16)
            request = bytes.fromhex(row.get("request", ""))
        except (TypeError, ValueError):
            continue
        if (ecu, request) in ALLOWLIST:
            # Ambiguous duplicate reads cannot establish a single capability.
            key = (ecu, request)
            records[key] = row if key not in records else None

    identities = {}
    for ecu in (0x12, 0x18):
        reply = reply_bytes(records.get((ecu, bytes.fromhex("22f150"))))
        identities[f"{ecu:02x}"] = {
            "sgbd_index_candidate": reply[-3:].hex().upper() if reply is not None and
            6 <= len(reply) <= 16 and reply[:3] == bytes.fromhex("62f150") else None,
            "hardware_software_identifiers": "not_collected",
            "engine_family": "requires_independent_confirmation" if ecu == 0x12 else None,
        }

    bitmaps, advertised, checked = [], set(), set()
    for base in (0, 0x20, 0x40):
        reply = reply_bytes(records.get((0x12, bytes((1, base)))))
        if reply is None or len(reply) != 6 or reply[:2] != bytes((0x41, base)):
            break
        mask = int.from_bytes(reply[2:], "big")
        bitmaps.append({"pid": f"{base:02X}", "reply": reply.hex(" ")})
        advertised.update(base+offset for offset in range(1, 33) if mask & (1 << (32-offset)))
        if not mask & 1:
            checked.update(range(base+1, 0x60))
        else:
            checked.update(range(base+1, base+32))
        if not mask & 1:
            break

    signals = []
    for pid in PIDS:
        row = records.get((0x12, bytes((1, pid))))
        reply = reply_bytes(row)
        state = "unknown" if pid not in checked else "unsupported" if pid not in advertised else "advertised"
        entry = {"pid": f"{pid:02X}", "capability": state, "measurement": "unavailable"}
        if state == "advertised" and reply is not None and len(reply) == WIDTHS[pid]+2 and reply[:2] == bytes((0x41, pid)):
            entry["reply"] = reply.hex(" ")
            entry["measurement"] = "response_requires_decoder_and_independent_validation"
            if pid == 0x0b and reply[2] == 255:
                entry["measurement"] = "saturated_255_kpa_absolute"
        elif reply is not None and len(reply) == 3 and reply[:2] == bytes((0x7f, 1)):
            entry["nrc"] = f"{reply[2]:02X}"
        signals.append(entry)

    hsfz_observed = bool(bitmaps) or any(v["sgbd_index_candidate"] for v in identities.values())
    for (_, request), record in records.items():
        reply = reply_bytes(record)
        if reply is not None and len(reply) == 3 and reply[:2] == bytes((0x7f, request[0])):
            hsfz_observed = True
    return {
        "format": "idrive-diesel-discovery-v1",
        "evidence": evidence,
        "vehicle_platform": "not_inferred",
        "ecu_identification": identities,
        "obd_bitmaps": bitmaps,
        "signals": signals,
        "headunit": safe_headunit(headunit),
        "transport": {"hsfz": "response_observed" if hsfz_observed else "not_established",
                      "gateway": "explicit_endpoint_not_exported", "most": "not_tested"},
        "firmware_approval": "not_established",
        "physical_installation": "not_performed_by_this_tool",
        "limitations": ["SGBD tail is a candidate index, not a full ECU identity",
                        "No DPF, EGR, SCR or BMW-specific oil mapping is established",
                        "No standalone, QNX ABI or factory-function compatibility is established"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--plan", action="store_true", help="print the exact bounded request policy; no connection")
    mode.add_argument("--replay", type=Path, help="offline probe JSONL input; no connection")
    mode.add_argument("--collect", action="store_true", help="execute the reviewed parked read-only plan")
    parser.add_argument("--owner-approved-parked", action="store_true", help="owner has explicitly approved this exact collection")
    parser.add_argument("--local", type=enet_address)
    parser.add_argument("--gateway", type=enet_address)
    parser.add_argument("--headunit-report", type=Path, help="existing private capture_headunit compatibility.json")
    parser.add_argument("--out", type=Path, help="new report file; existing files are never overwritten")
    args = parser.parse_args()
    if args.collect and (not args.owner_approved_parked or not args.local or not args.gateway or args.local == args.gateway):
        parser.error("collection needs explicit owner approval, Park, and separate wired --local and --gateway addresses")
    if not args.collect and (args.local or args.gateway or args.owner_approved_parked):
        parser.error("network and approval options apply only to --collect")
    try:
        if args.out and args.out.exists():
            raise ValueError("output already exists")
        if args.plan:
            result = plan()
        else:
            headunit = json.loads(args.headunit_report.read_text()) if args.headunit_report else None
            if args.replay:
                if args.replay.stat().st_size > 1024*1024:
                    raise ValueError("replay input exceeds 1 MiB")
                rows = [json.loads(line) for line in args.replay.read_text().splitlines() if line.strip()]
            else:
                rows = collect(args.local, args.gateway)
            result = report(rows, headunit, "replay_unverified_provenance" if args.replay else "parked_live_read_only")
        output = json.dumps(result, indent=2) + "\n"
        if args.out:
            fd = os.open(args.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "w") as file:
                file.write(output)
        else:
            print(output, end="")
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from None


if __name__ == "__main__":
    main()
