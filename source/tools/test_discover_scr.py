"""Synthetic SCR evidence; no vehicle, network endpoint or proprietary files."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

import discover_diesel as discovery
import test_discover_diesel as fixtures


PROFILE = "standard-scr"
TOOL = Path(discovery.__file__)


def records():
    rows = fixtures.AftertreatmentDiscoveryTest.records()
    next(r for r in rows if r["request"] == "0180")["reply"] = "418009200000"
    return rows + [fixtures.row("0188", "41888981420001010001028000ffff")]


def scr(rows):
    return next(s for s in discovery.report(rows, profile=PROFILE)["signals"] if s["pid"] == "88")


class ScrDiscoveryTest(unittest.TestCase):
    def test_six_frozen_plans_reports_and_allowlists_ignore_unsolicited_scr(self):
        for name, profile in (("standard-v1", "standard"), ("egt-v2", "standard-egt"),
                              ("diesel-v3", "standard-diesel"), ("pressure-v4", "standard-pressure"),
                              ("dpf-v5", "standard-dpf"), ("aftertreatment-v6", "standard-aftertreatment")):
            fixture = json.loads((TOOL.parent/"fixtures"/("discovery-"+name+".json")).read_text())
            self.assertEqual(discovery.plan(profile), fixture["plan"])
            self.assertNotIn((0x12, b"\x01\x88"), discovery.policy(profile)[3])
            self.assertEqual(discovery.report(fixture["input_records"] + records()[-1:], profile=profile), fixture["report"])

    def test_v7_preserves_false_current_state_separate_from_causes_history_and_counters(self):
        rows = records()
        rows[-1]["reply"] = "41880fffff0001010001028000ffff"  # active false; cause/history bits true
        result = discovery.report(rows, profile=PROFILE)
        self.assertEqual(result["format"], "idrive-diesel-discovery-v7")
        self.assertEqual(result["profile"], PROFILE)
        self.assertEqual(len(result["signals"]), 24)
        self.assertEqual([s["pid"] for s in result["signals"][:3]], ["88", "8B", "85"])
        fields = scr(rows)["fields"]
        self.assertEqual(len(fields), 26)
        self.assertIs(fields[0]["value"], False)
        self.assertTrue(all(f["value"] is True for f in fields[1:21]))
        self.assertTrue(all(f["scope"] == "current" for f in fields[:5]))
        self.assertTrue(all(f["scope"] == "historical" for f in fields[5:21]))
        self.assertEqual([f["value"] for f in fields[21:]], [1, 256, 258, 32768, 65535])
        self.assertEqual([f["outside_documented_block_range"] for f in fields[21:]], [False, False, False, True, True])
        self.assertEqual([f["block"] for f in fields[21:]], ["current_10000_km", "current_10000_km",
                         "history_10000_20000_km", "history_20000_30000_km", "history_30000_40000_km"])
        self.assertTrue(all(f["unit"] == "km" for f in fields[21:]))
        self.assertEqual(result["firmware_approval"], "not_established")
        self.assertTrue(any("not remaining range" in s for s in result["limitations"]))
        other = {s["pid"]: s for s in result["signals"]}
        self.assertEqual(other["8B"]["fields"][1]["value"], "active")
        self.assertEqual(other["85"]["fields"][3]["value"], 16777217)

    def test_every_current_status_byte_rejects_reserved_bits_without_field_support_inference(self):
        for status in range(256):
            complete, fields = discovery.scr_inducement_fields("advertised", b"\x41\x88"+bytes((status,))+bytes(12))
            valid = not status & 0x70
            self.assertEqual(complete, valid)
            self.assertTrue(all(("value" in f) == valid for f in fields))
            self.assertTrue(all(f["capability"] == ("supported" if valid else "unknown") for f in fields))
            if valid:
                self.assertEqual([f["value"] for f in fields[:5]], [bool(status & bit) for bit in (128, 8, 4, 2, 1)])
        complete, fields = discovery.scr_inducement_fields("advertised", b"\x41\x88"+bytes(13))
        self.assertTrue(complete)
        self.assertTrue(all(f["value"] == 0 and f["capability"] == "supported" for f in fields))

    def test_all_history_bytes_keep_four_blocks_and_wire_nibbles_distinct(self):
        for byte in (1, 2):
            for value in range(256):
                data = bytearray(13);data[byte] = value
                complete, fields = discovery.scr_inducement_fields("advertised", b"\x41\x88"+data)
                self.assertTrue(complete)
                self.assertTrue(all(f["value"] is False for f in fields[:5]))
                for block, source, shift in ((0, 1, 0), (1, 1, 4), (2, 2, 0), (3, 2, 4)):
                    expected = [bool(data[source] & (1 << (shift+3-i))) for i in range(4)]
                    self.assertEqual([f["value"] for f in fields[5+4*block:9+4*block]], expected)

    def test_complete_unsigned_counter_domain_and_documented_block_boundary(self):
        for raw in range(65536):
            complete, fields = discovery.scr_inducement_fields("advertised", b"\x41\x88\0\0\0"+raw.to_bytes(2, "big")*5)
            self.assertTrue(complete)
            for field in fields[21:]:
                self.assertEqual(field["value"], raw)
                self.assertEqual(field["outside_documented_block_range"], raw > 10000)

    def test_exact_prefix_length_and_capability_are_required_for_every_value(self):
        good = bytes.fromhex(records()[-1]["reply"])
        for reply in ([good[:n] for n in range(15)] + [good+b"\0", good+b"PRIVATE", b"\x42"+good[1:],
                       good[:1]+b"\x85"+good[2:], bytes.fromhex("7f0131"), None]):
            complete, fields = discovery.scr_inducement_fields("advertised", reply)
            self.assertFalse(complete)
            self.assertTrue(all("value" not in f for f in fields))
        for state in ("unknown", "unsupported"):
            complete, fields = discovery.scr_inducement_fields(state, good)
            self.assertFalse(complete)
            self.assertTrue(all(f["capability"] == state and "value" not in f for f in fields))

    def test_missing_broken_or_nonadvertised_bitmap_never_authorizes_unsolicited_status(self):
        for request, reply, expected in (("0140", None, "unknown"), ("0160", "4160008901f0", "unsupported"),
                                         ("0180", "418008200000", "unsupported"), ("0180", "4180092000", "unknown"),
                                         ("0180", "7f0131", "unknown")):
            rows = records()
            rows = [r for r in rows if r["request"] != request] if reply is None else rows
            if reply is not None: next(r for r in rows if r["request"] == request)["reply"] = reply
            signal = scr(rows)
            self.assertEqual(signal["capability"], expected)
            self.assertNotIn("reply", signal)
            self.assertTrue(all("value" not in f for f in signal["fields"]))

    def test_duplicate_wrong_ecu_nrc_and_private_payloads_are_not_exported(self):
        secret = "PRIVATE-VIN-SERIAL-CREDENTIAL-NAVIGATION"
        rows = records();rows[-1].update(error=secret, hostname=secret, vin=secret)
        output = discovery.report(rows + [fixtures.row("22f190", secret.encode().hex())], {"vin": secret}, profile=PROFILE)
        self.assertNotIn(secret, json.dumps(output))
        self.assertNotIn(secret.encode().hex(), json.dumps(output))
        self.assertEqual(scr(rows+rows[-1:])["record_status"], "ambiguous_duplicate")
        wrong = records();wrong[-1]["ecu"] = "18"
        self.assertEqual(scr(wrong)["record_status"], "not_present")
        self.assertTrue(all("value" not in f for f in scr(wrong)["fields"]))
        for reply in (rows[-1]["reply"]+secret.encode().hex(), "7f0131"):
            bad = records();bad[-1]["reply"] = reply
            signal = scr(bad)
            self.assertNotIn("reply", signal)
            self.assertTrue(all("value" not in f for f in signal["fields"]))
            if reply == "7f0131":
                self.assertEqual(signal["nrc"], "31")
                self.assertEqual(signal["capability"], "advertised")

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.socket.create_connection")
    def test_exact_31_read_policy_prioritizes_scr_without_writes_or_retries(self, connect, sleep):
        replies = {bytes.fromhex(r["request"]): r["reply"] for r in records()}
        def answer(connection, ecu, request):
            if request[0] == 0x22: return {"reply": "7f2231"}
            if request[1] in (0, 0x20, 0x40, 0x60, 0x80): return {"reply": bytes((0x41, request[1])).hex()+"ffffffff"}
            return {"reply": replies.get(request, bytes((0x41, request[1])).hex()+"00"*discovery.WIDTHS.get(request[1], 1))}
        with patch("discover_diesel.exchange", side_effect=answer):
            rows = discovery.collect("169.254.10.2", "169.254.10.3", profile=PROFILE)
        sent = [(int(r["ecu"], 16), bytes.fromhex(r["request"])) for r in rows]
        self.assertEqual(len(sent), 31)
        self.assertEqual(len(set(sent)), 31)
        self.assertEqual(set(sent), discovery.policy(PROFILE)[3])
        self.assertEqual([r["request"] for r in rows[7:10]], ["01 88", "01 8b", "01 85"])
        self.assertEqual(sleep.call_count, 31)
        self.assertTrue(all(c.args == (0.2,) for c in sleep.call_args_list))
        self.assertEqual(connect.call_count, 31)
        self.assertTrue(all(c.kwargs["source_address"] == ("169.254.10.2", 0) for c in connect.call_args_list))

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.socket.create_connection")
    def test_collection_requires_advertised_88_and_does_not_retry_refusal_or_socket_failure(self, connect, sleep):
        for page in ("418008200000", "418001000000", "4180092000", "7f0131"):
            def answer(connection, ecu, request):
                if request[0] == 0x22: return {"reply": "7f2231"}
                return {"reply": page if request[1] == 0x80 else bytes((0x41, request[1])).hex()+"00000001"}
            with patch("discover_diesel.exchange", side_effect=answer):
                rows = discovery.collect("169.254.10.2", "169.254.10.3", profile=PROFILE)
            self.assertEqual(sum(r["request"] == "01 88" for r in rows), page == "418001000000")
        for failure in ("7f0131", OSError("PRIVATE ERROR")):
            def answer(connection, ecu, request):
                if request == b"\x01\x88":
                    if isinstance(failure, OSError): raise failure
                    return {"reply": failure}
                if request == b"\x01\x8b": return {"reply": "418b7f03ff01000200"}
                return {"reply": bytes((0x41, request[1])).hex()+"ffffffff" if request[0] == 1 else "7f2231"}
            with patch("discover_diesel.exchange", side_effect=answer):
                rows = discovery.collect("169.254.10.2", "169.254.10.3", profile=PROFILE)
            self.assertEqual(sum(r["request"] == "01 88" for r in rows), 1)
            self.assertTrue(all("value" not in f for f in scr(rows)["fields"]))
            self.assertNotIn("PRIVATE", json.dumps(discovery.report(rows, profile=PROFILE)))
            other = next(s for s in discovery.report(rows, profile=PROFILE)["signals"] if s["pid"] == "8B")
            self.assertIs(other["fields"][0]["value"], True)

    def test_45_second_budget_reserves_deadlines_and_prioritizes_scr_in_partial_capture(self):
        for duration, count in ((6, 7), (5, 8)):
            clock = [0.0]
            def advance(seconds): clock[0] += seconds
            def answer(connection, ecu, request):
                advance(duration)
                if request == b"\x01\x88": return {"reply": records()[-1]["reply"]}
                return {"reply": bytes((0x41, request[1])).hex()+"ffffffff" if request[0] == 1 else "7f2231"}
            with patch("discover_diesel.time.monotonic", side_effect=lambda: clock[0]), \
                 patch("discover_diesel.time.sleep", side_effect=advance), \
                 patch("discover_diesel.exchange", side_effect=answer), patch("discover_diesel.socket.create_connection"):
                rows = discovery.collect("169.254.10.2", "169.254.10.3", profile=PROFILE)
            self.assertEqual(len(rows), count)
            self.assertLessEqual(clock[0], 45)
            if count == 8:
                self.assertEqual(rows[-1]["request"], "01 88")
                self.assertIs(scr(rows)["fields"][0]["value"], True)

    def test_plan_replay_private_exclusive_output_and_approval_gates(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory)/"replay.jsonl", Path(directory)/"report.json"
            source.write_text("\n".join(json.dumps(r) for r in records()))
            for args in (("--plan",), ("--replay", str(source), "--out", str(output))):
                with patch.object(sys, "argv", [str(TOOL), "--profile", PROFILE, *args]), \
                     patch("discover_diesel.socket.create_connection") as connect, redirect_stdout(io.StringIO()):
                    discovery.main()
                connect.assert_not_called()
            original = output.read_bytes()
            self.assertEqual(output.stat().st_mode & 0o777, 0o600)
            self.assertEqual(json.loads(original)["evidence"], "replay_unverified_provenance")
            for args in (("--replay", str(source), "--out", str(output)),
                         ("--collect", "--local", "169.254.10.2", "--gateway", "169.254.10.3"),
                         ("--collect", "--owner-approved-parked", "--local", "169.254.10.2", "--gateway", "169.254.10.2")):
                with patch.object(sys, "argv", [str(TOOL), "--profile", PROFILE, *args]), \
                     patch("discover_diesel.socket.create_connection") as connect, redirect_stdout(io.StringIO()), \
                     patch("sys.stderr", io.StringIO()), self.assertRaises(SystemExit):
                    discovery.main()
                connect.assert_not_called()
            self.assertEqual(output.read_bytes(), original)

    def test_full_31_read_hsfz_stream_with_wrong_ecu_pid_pending_heartbeat_and_split_reply(self):
        replies = {bytes.fromhex(r["request"]): bytes.fromhex(r["reply"]) for r in records()}
        seen, threads, failures = [], [], []
        def exact(connection, size):
            data = b""
            while len(data) < size:
                chunk = connection.recv(size-len(data))
                if not chunk: raise ConnectionError("synthetic gateway closed")
                data += chunk
            return data
        def frame(control, payload): return struct.pack(">IH", len(payload), control)+payload
        def respond(gateway):
            with gateway:
                gateway.settimeout(2)
                try:
                    size, control = struct.unpack(">IH", exact(gateway, 6))
                    payload = exact(gateway, size)
                    self.assertEqual(control, 1);self.assertEqual(payload[0], 0xf4)
                    ecu, request = payload[1], payload[2:]
                    seen.append((ecu, request))
                    if request[0] == 0x22: reply = bytes.fromhex("62f150010203")
                    elif request[1] in (0, 0x20, 0x40, 0x60, 0x80): reply = bytes((0x41, request[1]))+b"\xff"*4
                    else: reply = replies.get(request, bytes((0x41, request[1]))+b"\0"*discovery.WIDTHS.get(request[1], 1))
                    if request == b"\x01\x88":
                        gateway.sendall(frame(0x12, b""))
                        self.assertEqual(exact(gateway, 8), frame(0x13, b"\0\xf4"))
                        gateway.sendall(frame(1, bytes((ecu^1, 0xf4))+reply))
                        gateway.sendall(frame(1, bytes((ecu, 0xf4))+b"\x7f\x01\x78"))
                        gateway.sendall(frame(1, bytes((ecu, 0xf4))+b"\x41\x05\xff"))
                    packet = frame(1, bytes((ecu, 0xf4))+reply)
                    for part in (packet[:2], packet[2:7], packet[7:]): gateway.sendall(part)
                except Exception as error: failures.append(error)
        def connect(endpoint, *, timeout, source_address):
            self.assertEqual(endpoint, ("169.254.10.3", 6801));self.assertEqual(timeout, 2)
            self.assertEqual(source_address, ("169.254.10.2", 0))
            client, gateway = socket.socketpair()  # AF_UNIX only; no external network.
            thread = threading.Thread(target=respond, args=(gateway,), daemon=True)
            threads.append(thread);thread.start()
            return client
        with patch("discover_diesel.socket.create_connection", side_effect=connect), patch("discover_diesel.time.sleep"):
            rows = discovery.collect("169.254.10.2", "169.254.10.3", profile=PROFILE)
        for thread in threads:
            thread.join(3);self.assertFalse(thread.is_alive())
        self.assertFalse(failures, failures)
        self.assertEqual(len(seen), 31)
        self.assertEqual(set(seen), discovery.policy(PROFILE)[3])
        self.assertEqual([f["value"] for f in scr(rows)["fields"][21:]], [1, 256, 258, 32768, 65535])


if __name__ == "__main__":
    unittest.main()
