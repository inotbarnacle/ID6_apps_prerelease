"""Synthetic standard EGT replies through production workers; no vehicle captures."""
from pathlib import Path
import os
import socket
import subprocess
import time
import unittest

import test_diesel_capabilities as capabilities

BUILD = Path(__file__).resolve().parents[1] / "build"
EGT = tuple(range(31, 39))
ALL_EGT = sum(1 << index for index in EGT)


class Responses(dict):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.times = {}

    def __getitem__(self, key):
        self.times.setdefault(key, []).append(time.monotonic())
        value = super().__getitem__(key)
        if isinstance(value, list):
            return value[min(len(self.times[key])-1, len(value)-1)]
        return value


def responses(supported=(0x20, 0x40, 0x60, 0x78, 0x79)):
    return Responses({bytes((0x12, 1, base)): capabilities.bitmap(base, supported) for base in (0, 0x20, 0x40, 0x60)} | {
        bytes.fromhex("120178"): bytes.fromhex("41780f1905196919cd1a31"),  # 600.5, 610.5, 620.5, 630.5 C
        bytes.fromhex("120179"): bytes.fromhex("41790800000000000020d5"),  # only bank 2 sensor 4: 800.5 C
        bytes.fromhex("1822da12"): bytes.fromhex("7f2231"),
        bytes.fromhex("120105"): bytes.fromhex("41057d"),
        bytes.fromhex("122256d7"): bytes.fromhex("6256d71388"),
    })


def decode(packet):
    fields = packet.decode().split()
    if fields[0] in ("NBC1", "NBC2"):
        report = (int(fields[4]), int(fields[5]))
        fields = fields[6:]
    else:
        report = None
    return fields[0], int(fields[3]), [float(value) for value in fields[5::2]], report


class EgtTransportTest(unittest.TestCase):
    def run_worker(self, replies, selected=ALL_EGT, *, diesel=True, automatic=False, cycles=80, most=False, most_mode="egt-up"):
        env = dict(os.environ)
        env.pop("ID6_FAKE_MOST", None)
        with capabilities.ecu_server(replies, idle_timeout=8) as (port, requests), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0)); udp.settimeout(.05)
            args = [BUILD / "boost-deck", "--cycles", str(cycles), "--udp-port", str(udp.getsockname()[1])]
            args += ["--auto-diesel", "1234"] if automatic else ["--signals", str(selected)]
            if diesel: args.append("--diesel")
            if most:
                args += ["--transport", "most"]; env["ID6_FAKE_MOST"] = most_mode
            else:
                args += ["--host", "127.0.0.1", "--port", str(port), "--transport", "hsfz"]
            process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
            packets = []
            deadline = time.monotonic() + 15
            try:
                while process.poll() is None:
                    self.assertLess(time.monotonic(), deadline, "worker did not stop within its bounded test run")
                    try: packets.append(decode(udp.recv(2048)))
                    except socket.timeout: pass
                while True:
                    try: packets.append(decode(udp.recv(2048)))
                    except socket.timeout: break
                _, error = process.communicate(timeout=3)
                self.assertEqual(process.returncode, 0, error.decode())
            finally:
                if process.poll() is None: process.kill(); process.communicate()
            self.assertTrue(packets)
        return packets, requests, error.decode()

    def test_two_banks_share_reads_and_preserve_high_sensor_bits(self):
        source = responses()
        packets, requests, _ = self.run_worker(source)
        final = packets[-1]
        self.assertEqual(final[0], "NBG8")
        self.assertEqual(final[1] & ALL_EGT, sum(1 << i for i in (31, 32, 33, 34, 38)))
        self.assertEqual(final[2][31:35], [600.5, 610.5, 620.5, 630.5])
        self.assertEqual(final[2][38], 800.5)
        for pid in (0, 0x20, 0x40, 0x60): self.assertEqual(requests.count(bytes((0x12, 1, pid))), 1)
        for pid in (0x78, 0x79): self.assertLessEqual(requests.count(bytes((0x12, 1, pid))), 2)

    def test_slow_capability_reply_cannot_shorten_bank_read_interval(self):
        class SlowBitmap(Responses):
            def __getitem__(self, key):
                value = super().__getitem__(key)
                if key == bytes.fromhex("120160"):
                    time.sleep(.3)  # valid reply within the production 500-ms deadline
                return value

        source = SlowBitmap(responses())
        packets, _, _ = self.run_worker(source, selected=(1 << 31), cycles=100)
        reads = source.times[bytes.fromhex("120178")]
        self.assertGreaterEqual(len(reads), 2)
        # The server observes arrivals; allow 100 ms of scheduling variability.
        self.assertTrue(all(b-a >= 1.9 for a, b in zip(reads, reads[1:])), reads)
        self.assertTrue(packets[-1][1] & (1 << 31))

    def test_missing_continuation_or_pid_never_reads_egt(self):
        for advertised in ((), (0x20,), (0x20, 0x40), (0x20, 0x40, 0x60)):
            with self.subTest(advertised=advertised):
                packets, requests, _ = self.run_worker(responses(advertised), cycles=12)
                self.assertTrue(all(packet[0] == "NBG7" and not packet[1] for packet in packets))
                self.assertFalse(any(request[1:] in (b"\x01\x78", b"\x01\x79") for request in requests))

    def test_absent_and_implausible_sensors_do_not_become_zero_temperatures(self):
        source = responses()
        source[bytes.fromhex("120178")] = bytes.fromhex("41780fffff0000019047e1")
        packets, _, _ = self.run_worker(source, selected=sum(1 << i for i in EGT[:4]), cycles=30)
        self.assertEqual(packets[-1][1] & ALL_EGT, (1 << 32) | (1 << 33))
        self.assertEqual(packets[-1][2][32:34], [-40, 0])
        source = responses()
        source[bytes.fromhex("120178")] = bytes.fromhex("4178001905196919cd1a31")
        packets, _, _ = self.run_worker(source, selected=(1 << 31), cycles=30)
        self.assertTrue(all(not (packet[1] & ALL_EGT) for packet in packets))

    def test_bad_layout_and_nrc_back_off_without_repeated_probing(self):
        for reply in (bytes.fromhex("41780f0000"), bytes.fromhex("41788f1905196919cd1a31"),
                      bytes.fromhex("41780f1905196919cd1a3100"), bytes.fromhex("7f0112")):
            with self.subTest(reply=reply.hex()):
                source = responses(); source[bytes.fromhex("120178")] = reply
                packets, requests, _ = self.run_worker(source, selected=(1 << 31), cycles=45)
                self.assertEqual(requests.count(bytes.fromhex("120178")), 1)
                self.assertTrue(all(not packet[1] & ALL_EGT for packet in packets))

    def test_sensor_layout_conflict_invalidates_only_that_bank(self):
        source = responses((5, 0x20, 0x40, 0x60, 0x78, 0x79))
        source[bytes.fromhex("120178")] = [source[bytes.fromhex("120178")], bytes.fromhex("4178000000000000000000")]
        source.times.clear()
        packets, _, _ = self.run_worker(source, selected=(1 << 7) | (1 << 31), cycles=110)
        self.assertTrue(any(packet[1] & (1 << 31) for packet in packets))
        self.assertFalse(packets[-1][1] & (1 << 31))
        self.assertTrue(packets[-1][1] & (1 << 7))
        self.assertEqual(packets[-1][2][7], 85)

    def test_bank_read_recovers_after_the_five_second_backoff(self):
        source = responses()
        key = bytes.fromhex("120178")
        good = source[key]
        source[key] = [bytes.fromhex("7f0121"), good]
        source.times.clear()
        packets, _, _ = self.run_worker(source, selected=(1 << 31), cycles=190)
        self.assertEqual(len(source.times[key]), 2)
        self.assertGreaterEqual(source.times[key][1]-source.times[key][0], 4.8)
        self.assertTrue(packets[-1][1] & (1 << 31))
        self.assertEqual(packets[-1][2][31], 600.5)

    def test_automatic_evidence_requires_sensor_flags_and_chooses_one_sensor(self):
        source = responses()
        source[bytes.fromhex("120178")] = bytes.fromhex("41780a1905196919cd1a31")
        packets, _, _ = self.run_worker(source, automatic=True, cycles=45)
        known, supported = packets[-1][3]
        self.assertEqual(known & ALL_EGT, ALL_EGT)
        self.assertEqual(supported & ALL_EGT, (1 << 32) | (1 << 34) | (1 << 38))
        self.assertEqual(packets[-1][0], "NBG8")

    def test_gasoline_legacy_packet_and_fixed_rail_remain_unchanged(self):
        packets, requests, _ = self.run_worker(responses(), selected=(1 << 5), diesel=False, cycles=20)
        self.assertTrue(all(packet[0] == "NBG7" for packet in packets))
        self.assertEqual(packets[-1][2][5], 100)
        self.assertTrue(requests and all(request == bytes.fromhex("122256d7") for request in requests))
        packets, requests, _ = self.run_worker(responses(), selected=(1 << 38), diesel=False, cycles=30)
        self.assertTrue(packets[-1][1] & (1 << 38))
        self.assertIn(bytes.fromhex("120160"), requests)

    def test_most_decodes_multisensor_payloads_and_cleans_up(self):
        packets, _, error = self.run_worker(responses(), most=True, cycles=30)
        self.assertEqual(packets[-1][0], "NBG8")
        self.assertEqual(packets[-1][1] & ALL_EGT, (1 << 31) | (1 << 33) | (1 << 38))
        self.assertAlmostEqual(packets[-1][2][38], 800.5)
        self.assertIn("unregister_target=1 unregister_client=1 free=1", error)

    def test_wide_signal_mask_rejects_unknown_bits_and_bad_numbers_before_connecting(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0)); listener.listen(); listener.settimeout(.05)
            for value in ("0", "-1", "+1", "0x20", str(1 << 47), str(1 << 63), str(1 << 64), "123x"):
                with self.subTest(value=value):
                    result = subprocess.run([BUILD / "boost-deck", "--host", "127.0.0.1", "--port",
                                             str(listener.getsockname()[1]), "--signals", value],
                                            capture_output=True, timeout=2)
                    self.assertEqual(result.returncode, 2)
                    with self.assertRaises(socket.timeout): listener.accept()


if __name__ == "__main__":
    unittest.main()
