from contextlib import redirect_stdout
import io
import json
from fractions import Fraction
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

import discover_diesel as discovery

TOOL = Path(__file__).with_name("discover_diesel.py")
V1_FIXTURE = TOOL.parent / "fixtures" / "discovery-standard-v1.json"
V2_FIXTURE = TOOL.parent / "fixtures" / "discovery-egt-v2.json"
V3_FIXTURE = TOOL.parent / "fixtures" / "discovery-diesel-v3.json"


def row(request, reply, ecu="12"):
    return {"ecu": ecu, "request": request, "reply": reply}


def egt_rows():
    return [row("0100", "410000000001"), row("0120", "412000000001"),
            row("0140", "414000000001"), row("0160", "416000000180"),
            row("0178", "41780b0190ffff000047e0"),
            row("0179", "4179040000000019050000")]


def diesel_rows():
    return [row("0100", "410000000001"), row("0120", "412000180001"),
            row("0140", "414000000001"), row("0160", "416000800180"),
            row("0169", "41693f008080ff4000"), row("012c", "412cff"), row("012d", "412dff"),
            *egt_rows()[4:]]


def pressure_rows():
    records = diesel_rows()
    records[3] = row("0160", "416000890180")  # 69, 6D, 70, 78, 79
    return records + [row("016d", "416d3f27102328ff4e20ffffff"),
                      row("0170", "41703f1900192a3200ffffff")]


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

    def test_all_profiles_reserve_full_transaction_time_inside_45_seconds(self):
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

    def test_v2_plan_and_report_remain_identical_to_pinned_source(self):
        fixture = json.loads(V2_FIXTURE.read_text())
        records = fixture["input_records"]
        unsolicited = diesel_rows()[4:7]
        self.assertEqual(discovery.plan("standard-egt"), fixture["plan"])
        self.assertEqual(discovery.report(records, profile="standard-egt"), fixture["report"])
        self.assertEqual(discovery.report(records + unsolicited, profile="standard-egt"), fixture["report"])
        legacy = json.loads(V1_FIXTURE.read_text())
        self.assertEqual(discovery.report(legacy["input_records"] + unsolicited), legacy["report"])

    def test_v3_preserves_requested_actual_error_and_unspecified_sources(self):
        result = discovery.report(diesel_rows(), profile="standard-diesel")
        self.assertEqual(result["format"], "idrive-diesel-discovery-v3")
        self.assertEqual(result["profile"], "standard-diesel")
        entries = {s["pid"]: s for s in result["signals"]}
        fields = entries["69"]["fields"]
        self.assertEqual([(f["system"], f["quantity"]) for f in fields],
                         [(system, quantity) for system in ("A", "B") for quantity in ("requested", "actual", "error")])
        expected = [0, 12800/255, 0, 100, 6400/255, -100]
        for field, percent in zip(fields, expected):
            self.assertEqual(field["capability"], "supported")
            self.assertEqual(field["percent"], percent)
            self.assertEqual(field["measurement"], "decoded_requires_independent_validation")
        self.assertEqual(entries["2C"]["system"], "unspecified")
        self.assertEqual(entries["2C"]["quantity"], "requested")
        self.assertEqual(entries["2C"]["percent"], 100)
        self.assertEqual(entries["2D"]["quantity"], "error")
        self.assertEqual(entries["2D"]["percent"], 99.21875)
        self.assertEqual(entries["78"]["sensors"][0]["celsius"], 0)
        self.assertEqual(entries["69"]["reply"], "41 69 3f 00 80 80 ff 40 00")
        self.assertTrue(all("record_status" in entry for entry in entries.values()))

    def test_all_egr_support_bytes_and_raw_values_match_exact_rational_scales(self):
        for flags in range(256):
            payload = bytes((0x41, 0x69, flags, 0, 255, 128, 128, 0, 255))
            complete, fields = discovery.egr_fields("advertised", payload)
            self.assertEqual(complete, flags < 64)
            for index, field in enumerate(fields):
                self.assertEqual("percent" in field, flags < 64 and bool(flags & (1 << index)))
        for raw in range(256):
            commanded = float(Fraction(raw*100, 255))
            error = float(Fraction((raw-128)*100, 128))
            self.assertEqual(discovery.egr_percent(raw), commanded)
            self.assertEqual(discovery.egr_percent(raw, True), error)
            complete, fields = discovery.egr_fields("advertised", bytes((0x41, 0x69, 63)) + bytes((raw,))*6)
            self.assertTrue(complete)
            self.assertEqual([f["percent"] for f in fields], [commanded, commanded, error]*2)

    def test_actual_fields_require_their_own_flags_even_with_legacy_values(self):
        records = diesel_rows()
        records[4]["reply"] = "41692dff00ffff00ff"  # A/B request+error; both actual flags absent
        result = discovery.report(records, profile="standard-diesel")
        grouped = next(s for s in result["signals"] if s["pid"] == "69")
        for index in (1, 4):
            self.assertEqual(grouped["fields"][index]["capability"], "unsupported")
            self.assertNotIn("percent", grouped["fields"][index])
        self.assertEqual(grouped["fields"][0]["percent"], 100)
        self.assertEqual(grouped["fields"][2]["percent"], 99.21875)

    def test_egr_bank_and_scalar_replies_have_exact_prefix_length_and_flags(self):
        for reply in ("41693f008080ff4000", "41693f008080ff400000", "4169ff008080ff4000", "416a3f008080ff4000", "7f0131"):
            records = diesel_rows()
            records[4]["reply"] = reply
            signal = discovery.report(records, profile="standard-diesel")["signals"][0]
            valid = reply == "41693f008080ff4000"
            self.assertEqual("reply" in signal, valid)
            self.assertEqual(any("percent" in f for f in signal["fields"]), valid)
        payload = bytes.fromhex("41693f008080ff400000")
        for length in range(len(payload)+1):
            complete, _ = discovery.egr_fields("advertised", payload[:length])
            self.assertEqual(complete, length == 9)
        for pid in ("2c", "2d"):
            for reply in ("41"+pid, "41"+pid+"0000", "410500", "7f0131", "PRIVATE"):
                records = diesel_rows()
                next(r for r in records if r["request"] == "01"+pid)["reply"] = reply
                signal = next(s for s in discovery.report(records, profile="standard-diesel")["signals"] if s["pid"] == pid.upper())
                self.assertNotIn("percent", signal)
                self.assertNotIn("reply", signal)

    def test_egr_capability_chain_is_required_and_duplicates_remain_ambiguous(self):
        for records, capability in ((diesel_rows()[4:], "unknown"),
                                    ([row("0100", "410000000000"), *diesel_rows()[4:]], "unsupported"),
                                    ([*diesel_rows(), row("0160", "416000800180")], "unknown")):
            signal = discovery.report(records, profile="standard-diesel")["signals"][0]
            self.assertEqual(signal["capability"], capability)
            self.assertTrue(all(f["capability"] == capability and "percent" not in f for f in signal["fields"]))
            self.assertNotIn("reply", signal)
        records = diesel_rows() + [diesel_rows()[4]]
        signal = discovery.report(records, profile="standard-diesel")["signals"][0]
        self.assertEqual(signal["record_status"], "ambiguous_duplicate")
        self.assertTrue(all(f["capability"] == "unknown" and "percent" not in f for f in signal["fields"]))

    def test_v3_record_statuses_distinguish_partial_ambiguous_and_failed_records(self):
        variants = [(diesel_rows()[:4], "not_present"),
                    ([*diesel_rows()[:4], {"ecu":"12", "request":"0169", "error":"private failure"}], "present_no_valid_reply"),
                    ([*diesel_rows()[:4], row("0169", "")], "present_no_valid_reply"),
                    ([*diesel_rows()[:4], row("0169", "not hex")], "present_no_valid_reply"),
                    ([*diesel_rows(), diesel_rows()[4]], "ambiguous_duplicate"),
                    (diesel_rows(), "present_reply_bytes")]
        for records, expected in variants:
            signal = discovery.report(records, profile="standard-diesel")["signals"][0]
            self.assertEqual(signal["record_status"], expected)
            if expected != "present_reply_bytes":
                self.assertTrue(all("percent" not in f for f in signal["fields"]))
        records = diesel_rows()
        records[4]["reply"] = "7f0131"
        signal = discovery.report(records, profile="standard-diesel")["signals"][0]
        self.assertEqual(signal["nrc"], "31")
        self.assertEqual(signal["capability"], "advertised")
        self.assertTrue(all("percent" not in f for f in signal["fields"]))

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.exchange")
    @patch("discover_diesel.socket.create_connection")
    def test_diesel_policy_is_exactly_22_reads_with_egr_priority_and_no_retries(self, connect, exchange, sleep):
        replies = {bytes.fromhex(r["request"]): r["reply"] for r in diesel_rows()[4:]}
        def answer(connection, ecu, request):
            if request == bytes.fromhex("22f150"): return {"reply":"7f2231"}
            if request[1] in (0, 0x20, 0x40, 0x60):
                return {"reply":bytes((0x41,request[1])).hex()+"ffffffff"}
            if request in replies: return {"reply":replies[request]}
            return {"reply":bytes((0x41,request[1])).hex()+"00"*discovery.WIDTHS[request[1]]}
        exchange.side_effect = answer
        records = discovery.collect("169.254.10.2", "169.254.10.3", profile="standard-diesel")
        self.assertEqual(len(records), 22)
        self.assertEqual({(int(r["ecu"],16),bytes.fromhex(r["request"])) for r in records}, discovery.policy("standard-diesel")[3])
        self.assertEqual([r["request"] for r in records[6:11]], ["01 69", "01 2c", "01 2d", "01 78", "01 79"])
        self.assertEqual(sum(r["request"] == "01 69" for r in records), 1)
        self.assertEqual(connect.call_count, 22)
        self.assertEqual(sleep.call_count, 22)
        self.assertTrue(all(c.args == (0.2,) for c in sleep.call_args_list))
        self.assertTrue(all(c.kwargs["source_address"] == ("169.254.10.2",0) for c in connect.call_args_list))
        self.assertEqual(discovery.plan("standard-diesel")["measurements_if_advertised"][:3], ["01 69","01 2c","01 2d"])

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.exchange")
    @patch("discover_diesel.socket.create_connection")
    def test_diesel_policy_does_not_probe_unadvertised_legacy_or_group_fields(self, connect, exchange, sleep):
        for fourth in ("416000000000", "416000000080", "7f0131", "41600080"):
            def answer(connection, ecu, request):
                return {"reply": {"0100":"410000000001", "0120":"412000000001",
                                  "0140":"414000000001", "0160":fourth,
                                  "0179":"4179000000000000000000"}.get(request.hex(),"7f2231")}
            exchange.side_effect = answer
            records = discovery.collect("169.254.10.2", "169.254.10.3", profile="standard-diesel")
            self.assertFalse(any(r["request"] in ("01 69", "01 2c", "01 2d") for r in records))

    def test_slow_diesel_session_collects_explicit_egr_first_within_budget(self):
        clock = [0.0]
        def advance(seconds): clock[0] += seconds
        def answer(connection, ecu, request):
            advance(6)
            if request == bytes.fromhex("0169"): return {"reply":"41693f008080ff4000"}
            return {"reply":bytes((0x41,request[1])).hex()+"ffffffff" if request[0] == 1 else "7f2231"}
        with patch("discover_diesel.time.monotonic", side_effect=lambda:clock[0]), \
             patch("discover_diesel.time.sleep", side_effect=advance), \
             patch("discover_diesel.exchange", side_effect=answer), \
             patch("discover_diesel.socket.create_connection") as connect:
            records = discovery.collect("169.254.10.2", "169.254.10.3", profile="standard-diesel")
        self.assertEqual(connect.call_count, 7)
        self.assertLessEqual(clock[0], 45)
        self.assertEqual(records[-1]["request"], "01 69")
        report = discovery.report(records, profile="standard-diesel")
        self.assertEqual(report["signals"][0]["fields"][2]["percent"], 0)
        self.assertEqual(report["signals"][1]["record_status"], "not_present")

    def test_v3_privacy_filter_excludes_wrong_ecu_unknown_commands_and_raw_errors(self):
        secret = "PRIVATE-VIN-SERIAL-CREDENTIAL-NAVIGATION"
        records = diesel_rows()
        records[4]["error"] = secret
        records[4]["metadata"] = secret
        records += [row("0169", "41693f008080ff4000", "18"), row("22f190", secret.encode().hex()),
                    row("017a", secret.encode().hex()), {"hostname":secret}]
        result = discovery.report(records, {"hostname":secret,"vin":secret}, profile="standard-diesel")
        encoded = json.dumps(result)
        self.assertNotIn(secret, encoded)
        self.assertNotIn(secret.encode().hex(), encoded)
        self.assertNotIn("f190", encoded)
        records = [*diesel_rows()[:4], row("0169", "41693f008080ff4000", "18"), *diesel_rows()[5:]]
        grouped = discovery.report(records, profile="standard-diesel")["signals"][0]
        self.assertEqual(grouped["record_status"], "not_present")
        self.assertTrue(all("percent" not in f for f in grouped["fields"]))

    def test_diesel_plan_and_replay_are_offline_private_and_exclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path/"synthetic.jsonl"
            source.write_text("\n".join(json.dumps(r) for r in diesel_rows()))
            output = path/"public.json"
            for args in (("--plan",), ("--replay",str(source),"--out",str(output))):
                with patch.object(sys,"argv",[str(TOOL),"--profile","standard-diesel",*args]), \
                     patch("discover_diesel.socket.create_connection") as connect, redirect_stdout(io.StringIO()):
                    discovery.main()
                connect.assert_not_called()
            original = output.read_bytes()
            self.assertEqual(output.stat().st_mode & 0o777, 0o600)
            self.assertEqual(json.loads(original)["evidence"], "replay_unverified_provenance")
            with patch.object(sys,"argv",[str(TOOL),"--profile","standard-diesel","--replay",str(source),"--out",str(output)]), \
                 self.assertRaises(SystemExit):
                discovery.main()
            self.assertEqual(output.read_bytes(), original)


class PressureDiscoveryTest(unittest.TestCase):
    def signals(self, records):
        return {s["pid"]: s for s in discovery.report(records, profile="standard-pressure")["signals"]}

    def test_all_three_old_contracts_ignore_unsolicited_pressure(self):
        for path, profile in ((V1_FIXTURE, "standard"), (V2_FIXTURE, "standard-egt"),
                              (V3_FIXTURE, "standard-diesel")):
            fixture = json.loads(path.read_text())
            self.assertEqual(discovery.plan(profile), fixture["plan"])
            self.assertEqual(discovery.report(fixture["input_records"], profile=profile), fixture["report"])
            self.assertEqual(discovery.report(fixture["input_records"]+pressure_rows()[-2:], profile=profile), fixture["report"])

    def test_v4_schema_keeps_pressure_units_references_and_quantities_distinct(self):
        result = discovery.report(pressure_rows(), profile="standard-pressure")
        self.assertEqual(result["format"], "idrive-diesel-discovery-v4")
        self.assertEqual(result["profile"], "standard-pressure")
        self.assertEqual(len(result["signals"]), 18)
        signals = self.signals(pressure_rows())
        for pid, reference, unit, values in (("6D", "relative_to_atmosphere", "bar", [1000,900,2000,6553.5]),
                                             ("70", "absolute", "kPa", [200,201.3125,400,2047.96875])):
            fields = signals[pid]["fields"]
            self.assertEqual([(f["system"],f["quantity"]) for f in fields],
                             [("A","requested"),("A","actual"),("B","requested"),("B","actual")])
            self.assertEqual([f["value"] for f in fields], values)
            self.assertTrue(all(f["unit"]==unit and f["pressure_reference"]==reference for f in fields))
            self.assertTrue(all(f["measurement"]=="decoded_requires_independent_validation" for f in fields))
        self.assertEqual(signals["69"]["fields"][2]["percent"], 0)
        self.assertEqual(signals["78"]["sensors"][0]["celsius"], 0)
        self.assertEqual(result["vehicle_platform"], "not_inferred")
        self.assertEqual(result["firmware_approval"], "not_established")

    def test_each_pressure_support_byte_uses_only_bits_zero_one_three_four(self):
        for record in pressure_rows()[-2:]:
            reply = bytes.fromhex(record["reply"])
            pid = reply[1]
            for flags in range(256):
                complete, fields = discovery.pressure_fields(pid, "advertised", reply[:2]+bytes((flags,))+reply[3:])
                self.assertEqual(complete, flags < 64)
                for field, bit in zip(fields, (0,1,3,4)):
                    supported = flags < 64 and bool(flags & (1 << bit))
                    self.assertEqual("value" in field, supported)
                    self.assertEqual(field["capability"], "supported" if supported else "unsupported" if flags < 64 else "unknown")

    def test_unsigned_extremes_zero_and_endian_boundaries_are_numeric_only_when_supported(self):
        for pid in discovery.PRESSURE_PIDS:
            for raw in (0,1,255,256,6442,10000,65534,65535):
                pair = raw.to_bytes(2,"big")
                payload = b"\x3f"+pair+pair+b"\xff"+pair+pair+b"\xff" if pid==0x6d else b"\x3f"+pair*4+b"\xff"
                complete, fields = discovery.pressure_fields(pid,"advertised",bytes((0x41,pid))+payload)
                self.assertTrue(complete)
                expected = float(Fraction(raw,10 if pid==0x6d else 32))
                self.assertEqual([f["value"] for f in fields], [expected]*4)
                self.assertTrue(all(type(f["value"]) is float for f in fields))
        # Independent preOBD encoder vector: actual A support, D-E raw 6442.
        complete, fields = discovery.pressure_fields(0x70,"advertised",bytes.fromhex("4170020000192a0000000000"))
        self.assertTrue(complete)
        self.assertEqual(fields[1]["value"], 201.3125)
        self.assertTrue(all("value" not in fields[i] for i in (0,2,3)))

    def test_pressure_prefix_lengths_and_unrecognized_flags_are_strict(self):
        for record in pressure_rows()[-2:]:
            good = bytes.fromhex(record["reply"])
            for length in range(len(good)+2):
                reply = (good+b"\0")[:length]
                self.assertEqual(discovery.pressure_fields(good[1],"advertised",reply)[0], length==len(good))
            for bad in (good[:2]+b"\x80"+good[3:], b"\x42"+good[1:], good[:1]+b"\x6f"+good[2:]):
                complete, fields = discovery.pressure_fields(good[1],"advertised",bad)
                self.assertFalse(complete)
                self.assertTrue(all("value" not in f for f in fields))
        for pid in (0x23,0x6e,0x6f):
            with self.assertRaises(ValueError): discovery.pressure_fields(pid,"advertised",None)

    def test_missing_actual_never_uses_requested_or_legacy_map_rail_or_baro(self):
        records = pressure_rows()
        for record in records[-2:]:
            record["reply"] = record["reply"][:4]+"09"+record["reply"][6:]
        records[0]["reply"] = "4100ffffffff"
        records[1]["reply"] = "4120ffffffff"
        records += [row("0123","41232710"), row("010b","410bff"), row("0133","413364")]
        signals = self.signals(records)
        for pid in ("6D","70"):
            fields = signals[pid]["fields"]
            self.assertTrue(all("value" in fields[i] for i in (0,2)))
            self.assertTrue(all(f["capability"]=="unsupported" and "value" not in f for f in (fields[1],fields[3])))
        self.assertEqual(signals["0B"]["measurement"], "saturated_255_kpa_absolute")

    def test_pressure_advertisement_requires_a_complete_unambiguous_chain(self):
        for records, state in ((pressure_rows()[4:],"unknown"),
                               ([row("0100","410000000000"),*pressure_rows()[4:]],"unsupported"),
                               ([*pressure_rows(),pressure_rows()[3]],"unknown")):
            for pid in ("6D","70"):
                signal = self.signals(records)[pid]
                self.assertEqual(signal["capability"], state)
                self.assertNotIn("reply",signal)
                self.assertTrue(all(f["capability"]==state and "value" not in f for f in signal["fields"]))

    def test_v4_record_statuses_and_negative_replies_do_not_authorize_numbers(self):
        for pid, record in zip(("6D","70"), pressure_rows()[-2:]):
            variants = [(pressure_rows()[:4],"not_present"),
                        ([*pressure_rows()[:4], {"ecu":"12","request":record["request"],"error":"PRIVATE"}],"present_no_valid_reply"),
                        ([*pressure_rows()[:4],row(record["request"],"")],"present_no_valid_reply"),
                        ([*pressure_rows()[:4],row(record["request"],"not hex")],"present_no_valid_reply"),
                        ([*pressure_rows(),record],"ambiguous_duplicate"),
                        ([*pressure_rows()[:4],row(record["request"],"7f0131")],"present_reply_bytes")]
            for records, status in variants:
                signal = self.signals(records)[pid]
                self.assertEqual(signal["record_status"], status)
                self.assertTrue(all("value" not in f for f in signal["fields"]))
                self.assertNotIn("reply",signal)
                if status=="present_reply_bytes": self.assertEqual(signal["nrc"],"31")

    def test_privacy_wrong_ecus_and_unallowlisted_pressure_sources_are_filtered(self):
        secret = "PRIVATE-VIN-SERIAL-CREDENTIAL-NAVIGATION"
        records = pressure_rows()
        records[-2].update(metadata=secret,error=secret,hostname=secret)
        records[-1]["reply"] += secret.encode().hex()
        records += [row("016d",pressure_rows()[-2]["reply"],"18"), row("016e",secret.encode().hex()),
                    row("016f",secret.encode().hex()), row("22f190",secret.encode().hex())]
        result = discovery.report(records,{"vin":secret,"gateway_ip":"169.254.10.3"},profile="standard-pressure")
        encoded = json.dumps(result)
        for private in (secret,secret.encode().hex(),"169.254.10.3","f190"):
            self.assertNotIn(private,encoded)
        signals = {s["pid"]:s for s in result["signals"]}
        self.assertIn("reply",signals["6D"])
        self.assertNotIn("reply",signals["70"])
        self.assertTrue(all("value" not in f for f in signals["70"]["fields"]))
        records = pressure_rows()[:4]+[row("016d",pressure_rows()[-2]["reply"],"18")]
        self.assertEqual(self.signals(records)["6D"]["record_status"],"not_present")

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.exchange")
    @patch("discover_diesel.socket.create_connection")
    def test_policy_is_exactly_24_read_only_requests_with_pressure_priority(self, connect, exchange, sleep):
        replies = {bytes.fromhex(r["request"]):r["reply"] for r in pressure_rows()[4:]}
        def answer(connection, ecu, request):
            if request==bytes.fromhex("22f150"): return {"reply":"7f2231"}
            if request[1] in (0,0x20,0x40,0x60): return {"reply":bytes((0x41,request[1])).hex()+"ffffffff"}
            return {"reply":replies.get(request,bytes((0x41,request[1])).hex()+"00"*discovery.WIDTHS.get(request[1],1))}
        exchange.side_effect = answer
        records = discovery.collect("169.254.10.2","169.254.10.3",profile="standard-pressure")
        self.assertEqual(len(records),24)
        self.assertEqual({(int(r["ecu"],16),bytes.fromhex(r["request"])) for r in records},discovery.policy("standard-pressure")[3])
        self.assertEqual([r["request"] for r in records[6:13]],["01 6d","01 70","01 69","01 2c","01 2d","01 78","01 79"])
        self.assertEqual(connect.call_count,24)
        self.assertEqual(sleep.call_count,24)
        self.assertTrue(all(c.args==(0.2,) for c in sleep.call_args_list))
        self.assertTrue(all(c.kwargs["source_address"]==("169.254.10.2",0) for c in connect.call_args_list))
        self.assertTrue(all(bytes.fromhex(r["request"])[0] in (1,0x22) for r in records))

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.exchange")
    @patch("discover_diesel.socket.create_connection")
    def test_collector_never_probes_unadvertised_or_unknown_pressure(self, connect, exchange, sleep):
        for fourth, expected in (("416000080000",{"01 6d"}),("416000010000",{"01 70"}),
                                  ("416000000000",set()),("41600009",set()),("7f0131",set())):
            def answer(connection,ecu,request):
                return {"reply":{"0100":"410000000001","0120":"412000000001","0140":"414000000001",
                                 "0160":fourth}.get(request.hex(),"7f0131")}
            exchange.side_effect = answer
            records = discovery.collect("169.254.10.2","169.254.10.3",profile="standard-pressure")
            self.assertEqual({r["request"] for r in records if r["request"] in ("01 6d","01 70")},expected)

    def test_slow_session_prioritizes_pressure_and_stays_within_45_seconds(self):
        clock = [0.0]
        def advance(seconds): clock[0] += seconds
        def answer(connection,ecu,request):
            advance(6)
            if request==bytes.fromhex("016d"): return {"reply":pressure_rows()[-2]["reply"]}
            return {"reply":bytes((0x41,request[1])).hex()+"ffffffff" if request[0]==1 else "7f2231"}
        with patch("discover_diesel.time.monotonic",side_effect=lambda:clock[0]), \
             patch("discover_diesel.time.sleep",side_effect=advance), \
             patch("discover_diesel.exchange",side_effect=answer), patch("discover_diesel.socket.create_connection") as connect:
            records = discovery.collect("169.254.10.2","169.254.10.3",profile="standard-pressure")
        self.assertEqual(connect.call_count,7)
        self.assertLessEqual(clock[0],45)
        self.assertEqual(records[-1]["request"],"01 6d")
        signals = self.signals(records)
        self.assertEqual(signals["6D"]["fields"][1]["value"],900)
        self.assertEqual(signals["70"]["record_status"],"not_present")

    def test_plan_and_replay_are_offline_and_output_remains_private_exclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source,output = path/"synthetic.jsonl",path/"public.json"
            source.write_text("\n".join(json.dumps(r) for r in pressure_rows()))
            for args in (("--plan",),("--replay",str(source),"--out",str(output))):
                with patch.object(sys,"argv",[str(TOOL),"--profile","standard-pressure",*args]), \
                     patch("discover_diesel.socket.create_connection") as connect, redirect_stdout(io.StringIO()):
                    discovery.main()
                connect.assert_not_called()
            original = output.read_bytes()
            self.assertEqual(output.stat().st_mode&0o777,0o600)
            result = json.loads(original)
            self.assertEqual(result["evidence"],"replay_unverified_provenance")
            self.assertEqual(result["format"],"idrive-diesel-discovery-v4")
            with patch.object(sys,"argv",[str(TOOL),"--profile","standard-pressure","--replay",str(source),"--out",str(output)]), \
                 self.assertRaises(SystemExit): discovery.main()
            self.assertEqual(output.read_bytes(),original)

    @patch("discover_diesel.time.sleep")
    @patch("discover_diesel.exchange")
    @patch("discover_diesel.socket.create_connection")
    def test_failed_pressure_reads_are_not_retried_and_do_not_hide_the_other_group(self, connect, exchange, sleep):
        for failure in ("7f0131",OSError("PRIVATE ERROR")):
            def answer(connection,ecu,request):
                if request==bytes.fromhex("016d"):
                    if isinstance(failure,OSError): raise failure
                    return {"reply":failure}
                if request==bytes.fromhex("0170"): return {"reply":pressure_rows()[-1]["reply"]}
                return {"reply":bytes((0x41,request[1])).hex()+"ffffffff" if request[0]==1 else "7f2231"}
            exchange.side_effect = answer
            records = discovery.collect("169.254.10.2","169.254.10.3",profile="standard-pressure")
            self.assertEqual(sum(r["request"]=="01 6d" for r in records),1)
            self.assertEqual(sum(r["request"]=="01 70" for r in records),1)
            signals = self.signals(records)
            self.assertTrue(all("value" not in f for f in signals["6D"]["fields"]))
            self.assertEqual(signals["70"]["fields"][1]["value"],201.3125)
            self.assertNotIn("PRIVATE",json.dumps(signals))


if __name__ == "__main__":
    unittest.main()
