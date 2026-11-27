"""Synthetic DDE responses through the production workers and loopback HSFZ.

The PID 40 unsupported-oil example is reported in issue #162. All other
responses here are simulated, not captures from the target X5.
"""
from contextlib import contextmanager
from pathlib import Path
import os
import socket
import struct
import subprocess
import threading
import unittest

from test_transport import exact, frame

BUILD = Path(__file__).resolve().parents[1] / "build"


def bitmap(base, supported):
    value = sum(1 << (32-(pid-base)) for pid in supported if base < pid <= base+32)
    return bytes((0x41, base)) + value.to_bytes(4, "big")


@contextmanager
def ecu_server(responses, idle_timeout=5):
    requests, errors = [], []
    stop = threading.Event()
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(0.1)

        def serve():
            while not stop.is_set():
                try:
                    conn, _ = listener.accept()
                except socket.timeout:
                    continue
                with conn:
                    conn.settimeout(idle_timeout)  # allow a test to exercise a longer application backoff
                    try:
                        while True:
                            size, control = struct.unpack(">IH", exact(conn, 6))
                            assert control == 1
                            request = exact(conn, size)
                            assert request[0] == 0xf4
                            assert request[2] in (1, 0x22), "unexpected state-changing service"
                            requests.append(request[1:])
                            assert request[1:] in responses, request.hex()
                            response = responses[request[1:]]
                            if response is None:  # lost response and connection
                                break
                            conn.sendall(frame(1, bytes((request[1], 0xf4)) + response))
                    except (EOFError, ConnectionResetError, BrokenPipeError):
                        pass
                    except Exception as error:
                        errors.append(error)

        thread = threading.Thread(target=serve)
        thread.start()
        try:
            yield listener.getsockname()[1], requests
        finally:
            stop.set()
            thread.join(3)
            assert not thread.is_alive(), "ECU mock failed to terminate"
            if errors:
                raise errors[0]


class DieselCapabilitiesTest(unittest.TestCase):
    def responses(self, oil=False):
        supported = {5, 0x0f, 0x20, 0x23, 0x40}
        if oil:
            supported.add(0x5c)
        return {
            bytes((0x12, 1, base)): bitmap(base, supported) for base in (0, 0x20, 0x40)
        } | {
            bytes.fromhex("120105"): bytes.fromhex("410563"),
            bytes.fromhex("12010f"): bytes.fromhex("410f50"),
            bytes.fromhex("120123"): bytes.fromhex("41232710"),  # 1000 bar, not gasoline DID scale
            bytes.fromhex("12015c"): bytes.fromhex("415c7d"),
            bytes.fromhex("1822da12"): bytes.fromhex("62da120058"),
        }

    def run_boost(self, responses, selected=None, diesel=True, automatic=False):
        if selected is None:
            selected = (1 << 9) | (1 << 7) | (1 << 5) | (1 << 3)
        with ecu_server(responses) as (port, requests), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(3)
            env = dict(os.environ)
            env.pop("ID6_FAKE_MOST", None)
            args = [BUILD/"boost-deck", "--host", "127.0.0.1", "--transport", "hsfz",
                    "--port", str(port), "--cycles", "80",
                    "--udp-port", str(udp.getsockname()[1])]
            args += ["--auto-diesel", "1234"] if automatic else ["--signals", str(selected)]
            if diesel:
                args.append("--diesel")
            process = subprocess.Popen(args, stderr=subprocess.PIPE, text=True, env=env)
            try:
                packets = [udp.recv(1024).decode().split() for _ in range(80)]
                _, error = process.communicate(timeout=3)
                self.assertEqual(process.returncode, 0, error)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
        return packets, requests

    def test_automatic_report_matches_ecu_advertisement_and_valid_transmission(self):
        packets, requests = self.run_boost(self.responses(), automatic=True)
        for packet in packets:
            self.assertIn(packet[0], ("NBC1", "NBC2"))
            self.assertEqual(packet[1], "1234")
            self.assertGreater(int(packet[2]), 0)
            self.assertEqual(packet[6], "NBG7")
            self.assertEqual(packet[3], packet[10])  # capability and measurement timestamps match
            self.assertEqual(int(packet[5]) & ~int(packet[4]), 0)
        known, supported = map(int, packets[-1][4:6])
        self.assertEqual(packets[-1][0], "NBC2")  # extended sensors explicitly known unsupported
        self.assertTrue(known & (1 << 9))
        self.assertFalse(supported & (1 << 9))
        self.assertEqual(supported, (1 << 7) | (1 << 4) | (1 << 5) | (1 << 3))
        self.assertNotIn(bytes.fromhex("12015c"), requests)
        self.assertEqual(requests.count(bytes.fromhex("120100")), 1)
        self.assertEqual(requests.count(bytes.fromhex("120120")), 1)
        self.assertEqual(requests.count(bytes.fromhex("120140")), 1)
        self.assertTrue(all(r[1] in (1, 0x22) for r in requests))

    def test_automatic_unknown_bitmap_and_refused_transmission_never_become_support(self):
        responses = self.responses(oil=True)
        responses[bytes.fromhex("120140")] = bytes.fromhex("4140000010")
        responses[bytes.fromhex("1822da12")] = bytes.fromhex("7f2231")
        packets, requests = self.run_boost(responses, automatic=True)
        for packet in packets:
            known, supported = map(int, packet[4:6])
            self.assertFalse(known & ((1 << 9) | (1 << 4)))
            self.assertFalse(supported & ((1 << 9) | (1 << 4)))
        self.assertTrue(int(packets[-1][5]) & (1 << 7))
        self.assertNotIn(bytes.fromhex("12015c"), requests)
        self.assertEqual(requests.count(bytes.fromhex("120140")), 1)
        self.assertLessEqual(requests.count(bytes.fromhex("1822da12")), 2)

    def test_automatic_advertisement_does_not_fabricate_a_measurement(self):
        responses = self.responses(oil=True)
        responses[bytes.fromhex("12015c")] = bytes.fromhex("7f0122")
        packets, requests = self.run_boost(responses, automatic=True)
        self.assertTrue(int(packets[-1][5]) & (1 << 9))
        self.assertFalse(int(packets[-1][9]) & (1 << 9))  # embedded NBG7 valid mask
        self.assertIn(bytes.fromhex("12015c"), requests)
        self.assertTrue(int(packets[-1][9]) & (1 << 7))

    def test_automatic_token_and_mode_errors_exit_before_transport(self):
        for token in ("0", "-1", "+1", "1x", "18446744073709551616"):
            with self.subTest(token=token):
                result = subprocess.run([BUILD/"boost-deck", "--auto-diesel", token], capture_output=True, timeout=2)
                self.assertEqual(result.returncode, 2)
        for options in (["--demo"], ["--signals", "1"]):
            result = subprocess.run([BUILD/"boost-deck", "--auto-diesel", "1", *options], capture_output=True, timeout=2)
            self.assertEqual(result.returncode, 2)

    def run_warm(self, responses):
        with ecu_server(responses) as (port, requests), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 42862))
            udp.settimeout(5)
            result = subprocess.run([BUILD/"warmdeck-collector", "--host", "127.0.0.1",
                                     "--port", str(port), "--cycles", "3"],
                                    capture_output=True, text=True, timeout=6)
            self.assertEqual(result.returncode, 0, result.stderr)
            packets = [struct.unpack("=QQQddd", udp.recv(64)) for _ in range(3)]
        return packets, requests

    def test_unadvertised_oil_is_never_requested_other_diesel_values_survive(self):
        responses = self.responses()
        responses[bytes.fromhex("120140")] = bytes.fromhex("41404cd00001")  # #162
        packets, requests = self.run_boost(responses)
        self.assertNotIn(bytes.fromhex("12015c"), requests)
        self.assertEqual(requests.count(bytes.fromhex("120140")), 1)
        packet = packets[-1]
        self.assertFalse(int(packet[3]) & (1 << 9))
        self.assertEqual(int(packet[3]) & ((1 << 7) | (1 << 5) | (1 << 3)), 168)
        self.assertEqual(float(packet[15]), 1000)
        self.assertEqual(float(packet[19]), 59)

    def test_advertised_oil_retains_existing_decoder(self):
        packets, requests = self.run_boost(self.responses(oil=True))
        self.assertIn(bytes.fromhex("12015c"), requests)
        self.assertTrue(int(packets[-1][3]) & (1 << 9))
        self.assertEqual(float(packets[-1][23]), 85)

    def test_gasoline_oil_respects_support_and_retains_its_units(self):
        for supported in (False, True):
            with self.subTest(supported=supported):
                packets, requests = self.run_boost(self.responses(oil=supported),
                                                   selected=1 << 9, diesel=False)
                self.assertEqual(bytes.fromhex("12015c") in requests, supported)
                self.assertEqual(bool(int(packets[-1][3]) & (1 << 9)), supported)
                if supported:
                    self.assertEqual(float(packets[-1][23]), 85)

    def test_malformed_or_negative_bitmap_never_enables_oil_or_floods(self):
        for reply in (bytes.fromhex("4140000010"), bytes.fromhex("414000000010aa"),
                      bytes.fromhex("7f0112"), None):
            with self.subTest(reply=reply):
                responses = self.responses(oil=True)
                responses[bytes.fromhex("120140")] = reply
                packets, requests = self.run_boost(responses)
                self.assertNotIn(bytes.fromhex("12015c"), requests)
                self.assertEqual(requests.count(bytes.fromhex("120140")), 1)
                self.assertTrue(any(int(p[3]) & (1 << 7) for p in packets))
                self.assertTrue(all(not int(p[3]) & (1 << 9) for p in packets))

    def test_missing_continuation_never_queries_higher_pages(self):
        responses = self.responses(oil=True)
        responses[bytes.fromhex("120100")] = bitmap(0, {5, 0x0f})
        packets, requests = self.run_boost(responses)
        self.assertNotIn(bytes.fromhex("120120"), requests)
        self.assertNotIn(bytes.fromhex("120140"), requests)
        self.assertNotIn(bytes.fromhex("120123"), requests)
        self.assertFalse(int(packets[-1][3]) & (1 << 5))
        self.assertTrue(int(packets[-1][3]) & (1 << 7))

    def test_warmdeck_keeps_coolant_and_transmission_without_oil(self):
        packets, requests = self.run_warm(self.responses())
        self.assertNotIn(bytes.fromhex("12015c"), requests)
        oil, coolant, trans, _, coolant_c, trans_c = packets[-1]
        self.assertEqual(oil, 0)
        self.assertGreater(coolant, 0)
        self.assertGreater(trans, 0)
        self.assertEqual((coolant_c, trans_c), (59, 40))

    def test_warmdeck_advertised_oil_and_exact_bitmap_chain(self):
        packets, requests = self.run_warm(self.responses(oil=True))
        self.assertIn(bytes.fromhex("12015c"), requests)
        self.assertEqual([r for r in requests if r in {
            bytes.fromhex("120100"), bytes.fromhex("120120"), bytes.fromhex("120140")
        }], [bytes.fromhex("120100"), bytes.fromhex("120120"), bytes.fromhex("120140")])
        self.assertGreater(packets[-1][0], 0)
        self.assertEqual(packets[-1][3], 85)

    def test_saturated_standard_map_never_becomes_a_precise_diesel_boost(self):
        for pressure in (254, 255):
            with self.subTest(kpa=pressure):
                responses = self.responses()
                responses[bytes.fromhex("120100")] = bitmap(0, {0x0b, 0x20})
                responses[bytes.fromhex("120120")] = bitmap(0x20, {0x33})
                responses[bytes.fromhex("12010b")] = bytes((0x41, 0x0b, pressure))
                responses[bytes.fromhex("120133")] = bytes.fromhex("413365")
                packets, _ = self.run_boost(responses, selected=1 | (1 << 14))
                packet = packets[-1]
                if pressure == 255:
                    self.assertEqual(int(packet[3]) & (1 | (1 << 14)), 0)
                else:
                    self.assertEqual(int(packet[3]) & (1 | (1 << 14)), 1 | (1 << 14))
                    self.assertAlmostEqual(float(packet[5]), (254-101)*0.145037738, places=5)


if __name__ == "__main__":
    unittest.main()
