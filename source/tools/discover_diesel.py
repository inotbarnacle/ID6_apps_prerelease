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
EGT_PIDS = (0x78, 0x79)
EGR_PIDS = (0x69, 0x2c, 0x2d)
PRESSURE_PIDS = (0x6d, 0x70)
FRAMEWORKS = ("libAppFramework.A.so", "libCoreFramework.A.so")
ALLOWLIST = {(ecu, bytes.fromhex("22f150")) for ecu in (0x12, 0x18)} | {
    (0x12, bytes((1, pid))) for pid in (0, 0x20, 0x40, *PIDS)
}
POLICIES = {
    "standard": (PIDS, (0, 0x20, 0x40), MAX_REQUESTS, ALLOWLIST),
    "standard-egt": (PIDS + EGT_PIDS, (0, 0x20, 0x40, 0x60), 19, ALLOWLIST | {
        (0x12, bytes((1, pid))) for pid in (0x60, *EGT_PIDS)
    }),
    # Prioritize the explicit A/B EGR block when the bounded session is partial.
    "standard-diesel": (EGR_PIDS + EGT_PIDS + PIDS, (0, 0x20, 0x40, 0x60), 22, ALLOWLIST | {
        (0x12, bytes((1, pid))) for pid in (0x60, *EGR_PIDS, *EGT_PIDS)
    }),
    # A separate plan retains the three older request sets and their ordering.
    "standard-pressure": (PRESSURE_PIDS + EGR_PIDS + EGT_PIDS + PIDS, (0, 0x20, 0x40, 0x60), 24, ALLOWLIST | {
        (0x12, bytes((1, pid))) for pid in (0x60, *PRESSURE_PIDS, *EGR_PIDS, *EGT_PIDS)
    }),
}


def policy(profile):
    try:
        return POLICIES[profile]
    except KeyError:
        raise ValueError("unknown discovery profile") from None


def plan(profile="standard"):
    pids, bases, limit, _ = policy(profile)
    result = {
        "transport": "HSFZ TCP 6801, explicit wired link-local endpoint only",
        "identity": [{"ecu": f"{ecu:02x}", "request": "22 f1 50"} for ecu in (0x12, 0x18)],
        "capabilities": [f"01 {base:02x}" for base in bases],
        "measurements_if_advertised": [f"01 {pid:02x}" for pid in pids],
        "max_requests": limit,
        "max_duration_seconds": MAX_SECONDS,
        "minimum_gap_seconds": 0.2,
        "retries": 0,
        "writes_or_session_changes": False,
        "ssh_or_most": False,
    }
    if profile != "standard":
        result["profile"] = profile
    return result


def collect(local, gateway, *, profile="standard"):
    pids, bases, limit, allowlist = policy(profile)
    rows = []
    deadline = time.monotonic() + MAX_SECONDS

    def read(ecu, request):
        assert (ecu, request) in allowlist
        # Reserve connect, send and receive deadlines (two seconds each),
        # plus the inter-request gap, before beginning another transaction.
        if len(rows) >= limit or time.monotonic() + 6.2 > deadline:
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
    for base in bases:
        row = read(0x12, bytes((1, base)))
        reply = reply_bytes(row)
        if reply is None or len(reply) != 6 or reply[:2] != bytes((0x41, base)):
            break
        mask = int.from_bytes(reply[2:], "big")
        supported.update(base+offset for offset in range(1, 33) if mask & (1 << (32-offset)))
        if not mask & 1:
            break
    for pid in pids:
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


def egt_sensors(pid, state, reply):
    """Decode standard fields, never infer physical location or regeneration.

    Matches the app's -40..1800 C plausibility envelope, not an OEM limit.
    Format/scaling provenance is documented in DIESEL-VALIDATION.md.
    """
    complete = (state == "advertised" and reply is not None and len(reply) == 11
                and reply[:2] == bytes((0x41, pid)) and not reply[2] & 0xf0)
    sensors = []
    for index in range(4):
        capability = "unsupported" if state == "unsupported" else "unknown"
        item = {"bank": pid - 0x77, "sensor": index + 1,
                "capability": capability, "measurement": "unavailable"}
        if complete:
            supported = bool(reply[2] & (1 << index))
            item["capability"] = "supported" if supported else "unsupported"
            if supported:
                raw = int.from_bytes(reply[3+2*index:5+2*index], "big")
                celsius = (raw - 400) / 10
                if celsius <= 1800:
                    item.update(celsius=celsius, measurement="decoded_requires_independent_validation")
                else:
                    item["measurement"] = "outside_app_plausibility_range"
        sensors.append(item)
    return complete, sensors


def egr_percent(raw, error=False):
    return (raw - 128) * 100 / 128 if error else raw * 100 / 255


def egr_fields(state, reply):
    """Decode only explicit PID 69 fields; never derive actual from error.

    Actual is normalized duty/position, not measured EGR flow. A/B hardware
    applicability and every value still require independent validation.
    """
    complete = (state == "advertised" and reply is not None and len(reply) == 9
                and reply[:2] == bytes((0x41, 0x69)) and not reply[2] & 0xc0)
    fields = []
    for index in range(6):
        quantity = ("requested", "actual", "error")[index % 3]
        capability = "unsupported" if state == "unsupported" else "unknown"
        item = {"system": "A" if index < 3 else "B", "quantity": quantity,
                "capability": capability, "measurement": "unavailable"}
        if complete:
            supported = bool(reply[2] & (1 << index))
            item["capability"] = "supported" if supported else "unsupported"
            if supported:
                item.update(percent=egr_percent(reply[3+index], quantity == "error"),
                            measurement="decoded_requires_independent_validation")
        fields.append(item)
    return complete, fields


def pressure_fields(pid, state, reply):
    """Standard request/actual pairs; rail gauge bar or absolute boost kPa.

    No atmospheric default, MAP substitution or physical A/B identity inferred.
    Unused temperature/status bytes do not invalidate the pressure fields.
    Format/scaling provenance is documented in DIESEL-VALIDATION.md.
    """
    if pid not in PRESSURE_PIDS:
        raise ValueError("unknown standard pressure PID")
    rail = pid == 0x6d
    complete = (state == "advertised" and reply is not None and len(reply) == (13 if rail else 12)
                and reply[:2] == bytes((0x41, pid)) and not reply[2] & 0xc0)
    fields = []
    for index, bit in enumerate((0, 1, 3, 4)):
        capability = "unsupported" if state == "unsupported" else "unknown"
        item = {"system": "A" if index < 2 else "B", "quantity": "actual" if index % 2 else "requested",
                "pressure_reference": "relative_to_atmosphere" if rail else "absolute",
                "unit": "bar" if rail else "kPa", "capability": capability, "measurement": "unavailable"}
        if complete:
            supported = bool(reply[2] & (1 << bit))
            item["capability"] = "supported" if supported else "unsupported"
            if supported:
                offset = 3 + 2*index + (1 if rail and index >= 2 else 0)
                raw = int.from_bytes(reply[offset:offset+2], "big")
                item.update(value=raw / (10 if rail else 32), measurement="decoded_requires_independent_validation")
        fields.append(item)
    return complete, fields


def report(rows, headunit=None, evidence="replay", *, profile="standard"):
    """Export only known fixed requests and validated response shapes.

    In particular no complete F150 payload, raw diagnostic errors, arbitrary
    DID responses, VIN, hostnames, IP addresses or manual free text survive.
    """
    if not isinstance(rows, list) or len(rows) > 256:
        raise ValueError("expected at most 256 probe records")
    pids, bases, _, allowlist = policy(profile)
    records = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        try:
            ecu = int(row.get("ecu", ""), 16)
            request = bytes.fromhex(row.get("request", ""))
        except (TypeError, ValueError):
            continue
        if (ecu, request) in allowlist:
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
    for base in bases:
        reply = reply_bytes(records.get((0x12, bytes((1, base)))))
        if reply is None or len(reply) != 6 or reply[:2] != bytes((0x41, base)):
            break
        mask = int.from_bytes(reply[2:], "big")
        bitmaps.append({"pid": f"{base:02X}", "reply": reply.hex(" ")})
        advertised.update(base+offset for offset in range(1, 33) if mask & (1 << (32-offset)))
        if not mask & 1:
            checked.update(range(base+1, bases[-1]+32))
        else:
            checked.update(range(base+1, base+32))
        if not mask & 1:
            break

    signals = []
    for pid in pids:
        key = (0x12, bytes((1, pid)))
        row = records.get(key)
        reply = reply_bytes(row)
        state = "unknown" if pid not in checked else "unsupported" if pid not in advertised else "advertised"
        entry = {"pid": f"{pid:02X}", "capability": state, "measurement": "unavailable"}
        if profile in ("standard-diesel", "standard-pressure"):
            entry["record_status"] = ("not_present" if key not in records else
                                      "ambiguous_duplicate" if row is None else
                                      "present_no_valid_reply" if not reply else "present_reply_bytes")
        if pid in PRESSURE_PIDS:
            complete, entry["fields"] = pressure_fields(pid, state, reply)
            if complete:
                entry["reply"] = reply.hex(" ")
                entry["measurement"] = "field_layout_requires_independent_validation"
        elif pid == 0x69:
            complete, entry["fields"] = egr_fields(state, reply)
            if complete:
                entry["reply"] = reply.hex(" ")
                entry["measurement"] = "field_layout_requires_independent_validation"
        elif pid in (0x2c, 0x2d):
            entry.update(system="unspecified", quantity="error" if pid == 0x2d else "requested")
            if state == "advertised" and reply is not None and len(reply) == 3 and reply[:2] == bytes((0x41, pid)):
                entry.update(reply=reply.hex(" "), percent=egr_percent(reply[2], pid == 0x2d),
                             measurement="decoded_requires_independent_validation")
        elif pid in EGT_PIDS:
            complete, entry["sensors"] = egt_sensors(pid, state, reply)
            if complete:
                entry["reply"] = reply.hex(" ")
                entry["measurement"] = "sensor_layout_requires_independent_validation"
        elif state == "advertised" and reply is not None and len(reply) == WIDTHS[pid]+2 and reply[:2] == bytes((0x41, pid)):
            entry["reply"] = reply.hex(" ")
            entry["measurement"] = "response_requires_decoder_and_independent_validation"
            if pid == 0x0b and reply[2] == 255:
                entry["measurement"] = "saturated_255_kpa_absolute"
        if reply is not None and len(reply) == 3 and reply[:2] == bytes((0x7f, 1)):
            entry["nrc"] = f"{reply[2]:02X}"
        signals.append(entry)

    hsfz_observed = bool(bitmaps) or any(v["sgbd_index_candidate"] for v in identities.values())
    for (_, request), record in records.items():
        reply = reply_bytes(record)
        if reply is not None and len(reply) == 3 and reply[:2] == bytes((0x7f, request[0])):
            hsfz_observed = True
    result = {
        "format": ("idrive-diesel-discovery-v1" if profile == "standard" else
                   "idrive-diesel-discovery-v2" if profile == "standard-egt" else
                   "idrive-diesel-discovery-v3" if profile == "standard-diesel" else "idrive-diesel-discovery-v4"),
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
    if profile != "standard":
        result["profile"] = profile
        result["limitations"].append("EGT bank/sensor fields do not establish physical sensor locations or regeneration")
    if profile in ("standard-diesel", "standard-pressure"):
        result["limitations"][1] = "No DPF, SCR or BMW-specific oil/EGR mapping is established"
        result["limitations"].append("EGR A/B actual is reported normalized duty/position, not inferred flow or an independent position measurement")
        result["limitations"].append("Legacy EGR request/error never establishes A/B actual values")
    if profile == "standard-pressure":
        result["limitations"].append("Standard pressure A/B fields do not establish physical rail/turbo identity or exact ECU support")
        result["limitations"].append("Boost pressure is absolute, not manifold or derived gauge boost; no atmospheric value is assumed")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--plan", action="store_true", help="print the exact bounded request policy; no connection")
    mode.add_argument("--replay", type=Path, help="offline probe JSONL input; no connection")
    mode.add_argument("--collect", action="store_true", help="execute the reviewed parked read-only plan")
    parser.add_argument("--profile", choices=POLICIES, default="standard",
                        help="standard: 16 reads/v1; standard-egt: 19 reads/v2; standard-diesel: 22 reads/v3; standard-pressure: separate 24-read plan/v4")
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
            result = plan(args.profile)
        else:
            headunit = json.loads(args.headunit_report.read_text()) if args.headunit_report else None
            if args.replay:
                if args.replay.stat().st_size > 1024*1024:
                    raise ValueError("replay input exceeds 1 MiB")
                rows = [json.loads(line) for line in args.replay.read_text().splitlines() if line.strip()]
            else:
                rows = collect(args.local, args.gateway, profile=args.profile)
            result = report(rows, headunit, "replay_unverified_provenance" if args.replay else "parked_live_read_only",
                            profile=args.profile)
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
