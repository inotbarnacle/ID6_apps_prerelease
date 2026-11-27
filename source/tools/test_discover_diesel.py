import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

import discover_diesel as discovery

TOOL = Path(__file__).with_name("discover_diesel.py")


def row(request, reply, ecu="12"):
    return {"ecu": ecu, "request": request, "reply": reply}


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
        for args in (("--collect",), ("--collect", "--local", "169.254.10.2", "--gateway", "169.254.10.3"),
                     ("--collect", "--owner-approved-parked", "--local", "169.254.10.2", "--gateway", "169.254.10.2"),
                     ("--plan", "--local", "169.254.10.2")):
            result = subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_invalid_headunit_metadata_is_never_published(self):
        self.assertIsNone(discovery.safe_headunit({"firmware": "VIN", "architecture": "private-host",
                                                  "display": {"width": True, "height": "serial"}}))
        with self.assertRaises(ValueError):
            discovery.report({"vin": "example"})
        with self.assertRaises(ValueError):
            discovery.report([{}]*257)


if __name__ == "__main__":
    unittest.main()
