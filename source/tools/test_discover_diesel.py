from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

import discover_diesel as discovery

TOOL = Path(__file__).with_name("discover_diesel.py")
V1_FIXTURE = TOOL.parent / "fixtures" / "discovery-standard-v1.json"


def row(request, reply, ecu="12"):
    return {"ecu": ecu, "request": request, "reply": reply}


def egt_rows():
    return [row("0100", "410000000001"), row("0120", "412000000001"),
            row("0140", "414000000001"), row("0160", "416000000180"),
            row("0178", "41780b0190ffff000047e0"),
            row("0179", "4179040000000019050000")]


class DiscoveryTest(unittest.TestCase):
    def sample(self):
        # Synthetic capabilities with the oil-unsupported PID 40 reply from #162.
        return [row("22f150", "62f15000000f1234"), row("22f150", "62f1500f14b8", "18"),
                row("0100", "410000000001"), row("0120", "412000000001"),
                row("0140", "41404cd00001"), row("015c", "415c7d")]

    def test_no_oil_support_is_not_overridden_by_an_unsolicited_measurement(self):
        result = discovery.report(self.sample())
        oil = next(s for s in result["signals"] if s["pid"] == "5C")
        self.assertEqual(oil, {"pid": "5C", "capability": "unsupported", "measurement": "unavailable"})
        self.assertEqual(result["ecu_identification"]["12"]["sgbd_index_candidate"], "0F1234")
        self.assertEqual(result["firmware_approval"], "not_established")

    def test_missing_chain_and_malformed_bitmap_preserve_unknown(self):
        for reply in ("4140000010", "414000000010aa", "7f0131", "not hex"):
            rows = self.sample()
            rows[4]["reply"] = reply
            result = discovery.report(rows)
            oil = next(s for s in result["signals"] if s["pid"] == "5C")
            self.assertEqual(oil["capability"], "unknown")
        rows = self.sample()
        rows[2]["reply"] = "410000000000"
        oil = next(s for s in discovery.report(rows)["signals"] if s["pid"] == "5C")
        self.assertEqual(oil["capability"], "unsupported")

    def test_duplicate_bitmap_is_ambiguous_not_silently_trusted(self):
        rows = self.sample()
        rows.append(row("0140", "414000000010"))
        oil = next(s for s in discovery.report(rows)["signals"] if s["pid"] == "5C")
        self.assertEqual(oil["capability"], "unknown")

    def test_negative_reply_proves_transport_but_not_ecu_capabilities(self):
        result = discovery.report([row("22f150", "7f2231"), row("0100", "7f0111")])
        self.assertEqual(result["transport"]["hsfz"], "response_observed")
        self.assertEqual(result["signals"][0]["capability"], "unknown")

    def test_positive_exact_oil_and_saturated_map_are_distinguished(self):
        rows = [row("0100", "410000200001"), row("0120", "412000000001"),
                row("0140", "414000000010"), row("015c", "415c7d"), row("010b", "410bff")]
        signals = {s["pid"]: s for s in discovery.report(rows)["signals"]}
        self.assertEqual(signals["5C"]["reply"], "41 5c 7d")
        self.assertEqual(signals["0B"]["measurement"], "saturated_255_kpa_absolute")

    def test_public_report_reconstructs_only_allowed_fields(self):
        secret = "PRIVATE-VIN-SERIAL-CREDENTIAL-NAVIGATION-DEVICE"
        rows = self.sample() + [row("22f190", secret), {"vin": secret},
                               row("0105", "410500" + secret.encode().hex())]
        rows[0]["secret"] = secret
        rows[0]["error"] = secret
        rows[0]["reply"] = "62f150" + secret.encode().hex()  # too large to parse as an index
        head = {"firmware": "NBTevo_Y23236I", "architecture": "armle", "ssh_node": "hu-jacinto",
                "display": {"width": 1920, "height": 720, "secret": secret},
                "frameworks": {name: {"sha256": "a"*64, "binary": secret} for name in discovery.FRAMEWORKS},
                "vin": secret, "credentials": secret, "hmi_node_ip": "169.254.1.2", "hostname": secret}
        result = discovery.report(rows, head)
        encoded = json.dumps(result)
        for value in (secret, secret.encode().hex(), "169.254.1.2", "f190"):
            self.assertNotIn(value, encoded)
        self.assertEqual(result["headunit"]["display"], {"width": 1920, "height": 720})
        self.assertEqual(set(result["headunit"]["frameworks"]), set(discovery.FRAMEWORKS))
        self.assertIsNone(result["ecu_identification"]["12"]["sgbd_index_candidate"])

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.exchange")
    @patch("discover_diesel.socket.create_connection")
    def test_live_policy_is_fixed_bounded_and_only_polls_advertised_measurements(self, connect, exchange, sleep):
        connect.return_value = MagicMock()
        def answer(connection, ecu, request):
            self.assertIn((ecu, request), discovery.ALLOWLIST)
            if request == bytes.fromhex("0100"):
                return {"reply": "410008000000"}  # coolant only, no continuation
            if request == bytes.fromhex("0105"):
                return {"reply": "410563"}
            return {"reply": "7f2231"}
        exchange.side_effect = answer
        rows = discovery.collect("169.254.10.2", "169.254.10.3")
        self.assertEqual([r["request"] for r in rows], ["22 f1 50", "22 f1 50", "01 00", "01 05"])
        self.assertEqual(connect.call_count, 4)
        self.assertLessEqual(connect.call_count, discovery.MAX_REQUESTS)
        self.assertTrue(all(c.kwargs["source_address"] == ("169.254.10.2", 0) for c in connect.call_args_list))
        self.assertTrue(all(c.args == (0.2,) for c in sleep.call_args_list))

    @patch("discover_diesel.time.monotonic", side_effect=[0, 42, 42, 42])
    @patch("discover_diesel.socket.create_connection")
    def test_deadline_prevents_new_connections(self, connect, monotonic):
        self.assertEqual(discovery.collect("169.254.10.2", "169.254.10.3"), [])
        connect.assert_not_called()

    def test_plan_and_replay_are_offline_and_output_is_exclusive_private(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            capture = path/"probe.jsonl"
            capture.write_text("\n".join(json.dumps(r) for r in self.sample()))
            output = path/"report.json"
            for args in (("--plan",), ("--replay", str(capture), "--out", str(output))):
                result = subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.stat().st_mode & 0o777, 0o600)
            original = output.read_bytes()
            result = subprocess.run([sys.executable, TOOL, "--replay", str(capture), "--out", str(output)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(json.loads(original)["evidence"], "replay_unverified_provenance")

    def test_live_collection_requires_explicit_reviewed_parked_approval(self):
        for profile in discovery.POLICIES:
            for args in (("--collect",), ("--collect", "--local", "169.254.10.2", "--gateway", "169.254.10.3"),
                         ("--collect", "--owner-approved-parked", "--local", "169.254.10.2", "--gateway", "169.254.10.2"),
                         ("--plan", "--local", "169.254.10.2")):
                result = subprocess.run([sys.executable, TOOL, "--profile", profile, *args], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)

    def test_invalid_headunit_metadata_is_never_published(self):
        self.assertIsNone(discovery.safe_headunit({"firmware": "VIN", "architecture": "private-host",
                                                  "display": {"width": True, "height": "serial"}}))
        with self.assertRaises(ValueError):
            discovery.report({"vin": "example"})
        with self.assertRaises(ValueError):
            discovery.report([{}]*257)

    def test_standard_v1_matches_pinned_old_plan_and_report(self):
        fixture = json.loads(V1_FIXTURE.read_text())
        self.assertEqual(discovery.plan(), fixture["plan"])
        self.assertEqual(discovery.report(fixture["input_records"]), fixture["report"])
        self.assertEqual(discovery.plan("standard"), fixture["plan"])
        self.assertEqual(discovery.report(fixture["input_records"] + egt_rows()[3:]), fixture["report"])

    def test_egt_v2_distinguishes_zero_absent_implausible_and_both_banks(self):
        result = discovery.report(egt_rows(), profile="standard-egt")
        self.assertEqual(result["format"], "idrive-diesel-discovery-v2")
        self.assertEqual(result["profile"], "standard-egt")
        signals = {s["pid"]: s for s in result["signals"]}
        sensors = signals["78"]["sensors"]
        self.assertEqual(sensors[0]["celsius"], 0)
        self.assertEqual(sensors[1]["measurement"], "outside_app_plausibility_range")
        self.assertEqual(sensors[1]["capability"], "supported")
        self.assertEqual(sensors[2]["capability"], "unsupported")
        self.assertEqual(sensors[3]["celsius"], 1800)
        self.assertTrue(all("celsius" not in s for s in sensors[1:3]))
        self.assertEqual(signals["79"]["sensors"][2]["celsius"], 600.5)
        self.assertEqual(signals["79"]["sensors"][2]["bank"], 2)
        self.assertEqual(signals["79"]["sensors"][2]["sensor"], 3)
        self.assertEqual(sensors[0]["measurement"], "decoded_requires_independent_validation")
        self.assertEqual(signals["78"]["reply"], "41 78 0b 01 90 ff ff 00 00 47 e0")

    def test_egt_requires_complete_unambiguous_capability_chain(self):
        cases = [([], "unknown"), ([row("0100", "410000000000")], "unsupported"),
                 (egt_rows()[:3], "unknown"),
                 (egt_rows()[:3] + [row("0160", "416000000000")], "unsupported"),
                 (egt_rows() + [row("0160", "416000000180")], "unknown")]
        for records, expected in cases:
            with self.subTest(records=records):
                result = discovery.report(records + egt_rows()[4:], profile="standard-egt")
                for signal in result["signals"][-2:]:
                    self.assertEqual(signal["capability"], expected)
                    self.assertNotIn("reply", signal)
                    self.assertTrue(all(s["capability"] == expected for s in signal["sensors"]))
                    self.assertTrue(all("celsius" not in s for s in signal["sensors"]))

    def test_egt_duplicate_or_invalid_bank_reply_cannot_supply_sensor_values(self):
        samples = [egt_rows() + [egt_rows()[4]]]
        for reply in ("7f0131", "4178050190", "41788b0190ffff000047e0",
                      "41790b0190ffff000047e0", "41780b0190ffff000047e000", "PRIVATE"):
            samples.append(egt_rows()[:4] + [row("0178", reply)])
        for records in samples:
            with self.subTest(records=records):
                signal = discovery.report(records, profile="standard-egt")["signals"][-2]
                self.assertEqual(signal["capability"], "advertised")
                self.assertNotIn("reply", signal)
                self.assertTrue(all(s["capability"] == "unknown" and "celsius" not in s
                                    for s in signal["sensors"]))
        negative = discovery.report(samples[1], profile="standard-egt")
        self.assertEqual(negative["signals"][-2]["nrc"], "31")

    def test_egt_sensor_flags_and_exact_lengths(self):
        for flags in range(256):
            reply = bytes((0x41, 0x78, flags)) + bytes(8)
            complete, sensors = discovery.egt_sensors(0x78, "advertised", reply)
            self.assertEqual(complete, flags < 16)
            for i, sensor in enumerate(sensors):
                present = flags < 16 and flags & (1 << i)
                self.assertEqual("celsius" in sensor, bool(present))
                if present:
                    self.assertEqual(sensor["celsius"], -40)
        reply = bytes.fromhex("41780f019001900190019000")
        for length in range(len(reply)+1):
            complete, sensors = discovery.egt_sensors(0x78, "advertised", reply[:length])
            self.assertEqual(complete, length == 11)
            self.assertEqual(any("celsius" in s for s in sensors), length == 11)

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.exchange")
    @patch("discover_diesel.socket.create_connection")
    def test_explicit_egt_policy_is_exactly_19_reads_default_stays_16(self, connect, exchange, sleep):
        def answer(connection, ecu, request):
            if request == bytes.fromhex("22f150"):
                return {"reply": "7f2231"}
            if request[1] in (0, 0x20, 0x40, 0x60):
                return {"reply": bytes((0x41, request[1])).hex() + "ffffffff"}
            if request[1] in discovery.EGT_PIDS:
                return {"reply": bytes((0x41, request[1], 15)).hex() + "0190"*4}
            return {"reply": bytes((0x41, request[1])).hex() + "00"*discovery.WIDTHS[request[1]]}
        exchange.side_effect = answer
        for profile, count in (("standard", 16), ("standard-egt", 19)):
            connect.reset_mock(); exchange.reset_mock(); sleep.reset_mock()
            records = discovery.collect("169.254.10.2", "169.254.10.3", profile=profile)
            sent = [(int(r["ecu"], 16), bytes.fromhex(r["request"])) for r in records]
            self.assertEqual(len(sent), count)
            self.assertEqual(len(set(sent)), count)
            self.assertEqual(set(sent), discovery.policy(profile)[3])
            self.assertEqual(connect.call_count, count)
            self.assertEqual(sleep.call_count, count)
            self.assertTrue(all(c.args == (0.2,) for c in sleep.call_args_list))

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.exchange")
    @patch("discover_diesel.socket.create_connection")
    def test_egt_live_policy_follows_continuations_and_never_reads_absent_bank(self, connect, exchange, sleep):
        for fourth, expect in (("416000000100", {"01 78"}), ("416000000000", set()),
                               ("4160000001", set()), ("7f0111", set())):
            def answer(connection, ecu, request):
                reply = {"0100": "410000000001", "0120": "412000000001",
                         "0140": "414000000001", "0160": fourth, "0178": "4178010190000000000000"}
                return {"reply": reply.get(request.hex(), "7f2231")}
            exchange.side_effect = answer
            records = discovery.collect("169.254.10.2", "169.254.10.3", profile="standard-egt")
            self.assertEqual({r["request"] for r in records if r["request"] in ("01 78", "01 79")}, expect)
        exchange.side_effect = lambda conn, ecu, request: {"reply": "410000000000" if request == b"\x01\x00" else "7f2231"}
        records = discovery.collect("169.254.10.2", "169.254.10.3", profile="standard-egt")
        self.assertEqual([r["request"] for r in records], ["22 f1 50", "22 f1 50", "01 00"])

    def test_both_profiles_reserve_full_transaction_time_inside_45_seconds(self):
        for profile in discovery.POLICIES:
            clock = [0.0]
            def advance(seconds):
                clock[0] += seconds
            def answer(connection, ecu, request):
                advance(6)  # each reserved connect/send/receive deadline is consumed
                return {"reply": bytes((0x41, request[1])).hex() + "ffffffff" if request[0] == 1 else "7f2231"}
            with patch("discover_diesel.time.monotonic", side_effect=lambda: clock[0]), \
                 patch("discover_diesel.time.sleep", side_effect=advance), \
                 patch("discover_diesel.exchange", side_effect=answer), \
                 patch("discover_diesel.socket.create_connection") as connect:
                records = discovery.collect("169.254.10.2", "169.254.10.3", profile=profile)
            self.assertEqual(connect.call_count, 7)
            self.assertEqual(len(records), 7)
            self.assertLessEqual(clock[0], 45)

    def test_extended_plan_and_replay_open_no_network_and_keep_private_output(self):
        with tempfile.TemporaryDirectory() as directory:
            capture = Path(directory) / "synthetic.jsonl"
            capture.write_text("\n".join(json.dumps(r) for r in egt_rows()))
            output = Path(directory) / "public.json"
            for args in (("--plan",), ("--replay", str(capture), "--out", str(output))):
                with patch.object(sys, "argv", [str(TOOL), "--profile", "standard-egt", *args]), \
                     patch("discover_diesel.socket.create_connection") as connect, redirect_stdout(io.StringIO()):
                    discovery.main()
                connect.assert_not_called()
            self.assertEqual(output.stat().st_mode & 0o777, 0o600)
            original = output.read_bytes()
            self.assertEqual(json.loads(original)["evidence"], "replay_unverified_provenance")
            with patch.object(sys, "argv", [str(TOOL), "--profile", "standard-egt", "--replay", str(capture), "--out", str(output)]), \
                 self.assertRaises(SystemExit):
                discovery.main()
            self.assertEqual(output.read_bytes(), original)

    def test_egt_report_excludes_private_fields_wrong_ecus_and_unallowlisted_reads(self):
        secret = "PRIVATE-VIN-SERIAL-CREDENTIAL"
        records = egt_rows() + [row("22f190", secret.encode().hex()), row("0178", "4178010190000000000000", "18"),
                               row("017a", secret.encode().hex())]
        records[4]["metadata"] = secret
        records[4]["error"] = secret
        records[5]["reply"] += secret.encode().hex()
        result = discovery.report(records, {"hostname": secret}, profile="standard-egt")
        encoded = json.dumps(result)
        self.assertNotIn(secret, encoded)
        self.assertNotIn(secret.encode().hex(), encoded)
        self.assertNotIn("f190", encoded)
        self.assertEqual(result["signals"][-2]["sensors"][0]["celsius"], 0)
        self.assertNotIn("reply", result["signals"][-1])


if __name__ == "__main__":
    unittest.main()
