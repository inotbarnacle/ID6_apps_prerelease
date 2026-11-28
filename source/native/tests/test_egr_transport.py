"""Synthetic EGR responses through production workers; no vehicle captures."""
import unittest
import time

import test_egt_transport as transport

EGR = tuple(range(39, 47))
ALL_EGR = sum(1 << signal for signal in EGR)
GROUP = sum(1 << signal for signal in range(41, 47))


def responses(supported=(0x20, 0x40, 0x60, 0x2c, 0x2d, 0x69)):
    result = transport.responses(supported)
    result.update({bytes.fromhex("12012c"): bytes.fromhex("412c80"),
                   bytes.fromhex("12012d"): bytes.fromhex("412d80"),
                   bytes.fromhex("120169"): bytes.fromhex("41693f804080ff00ff")})
    return result


class EgrTransportTest(unittest.TestCase):
    def run_worker(self, replies, selected=ALL_EGR, **kwargs):
        return transport.EgtTransportTest().run_worker(replies, selected, **kwargs)

    def test_grouped_fields_and_legacy_sources_are_separate_and_rate_bounded(self):
        source = responses()
        packets, requests, _ = self.run_worker(source, cycles=100)
        self.assertEqual(packets[-1][0], "NBG9")
        self.assertEqual(packets[-1][1] & ALL_EGR, ALL_EGR)
        expected = [50.196078, 0, 50.196078, 25.098039, 0, 100, 0, 99.21875]
        for actual, value in zip(packets[-1][2][39:47], expected):
            self.assertAlmostEqual(actual, value, places=5)
        for pid in (0, 0x20, 0x40, 0x60):
            self.assertEqual(requests.count(bytes((0x12, 1, pid))), 1)
        reads = source.times[bytes.fromhex("120169")]
        self.assertEqual(len(reads), 2)
        self.assertGreaterEqual(reads[1]-reads[0], 1.9)
        self.assertEqual(requests.count(bytes.fromhex("12012c")), 1)
        self.assertEqual(requests.count(bytes.fromhex("12012d")), 1)
        self.assertTrue(all(r[1] == 1 for r in requests))

    def test_missing_continuation_and_unadvertised_pids_never_probe_egr(self):
        for supported in ((), (0x20,), (0x20, 0x40, 0x60)):
            with self.subTest(supported=supported):
                packets, requests, _ = self.run_worker(responses(supported), cycles=65)
                self.assertTrue(all(not p[1] & ALL_EGR for p in packets))
                self.assertFalse(any(r[1:] in (b"\x01\x2c", b"\x01\x2d", b"\x01\x69") for r in requests))

    def test_actual_is_unavailable_without_its_flag_and_has_no_legacy_fallback(self):
        source = responses()
        source[bytes.fromhex("120169")] = bytes.fromhex("416905ff00ff000000")  # command+error A, no actual
        packets, requests, _ = self.run_worker(source, selected=(1 << 42), cycles=70)
        self.assertTrue(all(not p[1] & (1 << 42) for p in packets))
        self.assertNotIn(bytes.fromhex("12012c"), requests)
        self.assertNotIn(bytes.fromhex("12012d"), requests)
        source = responses((0x20, 0x40, 0x60, 0x2c, 0x2d))
        packets, requests, _ = self.run_worker(source, selected=(1 << 42), cycles=65)
        self.assertTrue(all(not p[1] & ALL_EGR for p in packets))
        self.assertNotIn(bytes.fromhex("120169"), requests)
        self.assertNotIn(bytes.fromhex("12012c"), requests)

    def test_group_layout_changes_clear_absent_fields_without_harming_coolant(self):
        source = responses((5, 0x20, 0x40, 0x60, 0x69))
        key = bytes.fromhex("120169")
        source[key] = [source[key], bytes.fromhex("416901800000000000")]
        source.times.clear()
        packets, _, _ = self.run_worker(source, selected=(1 << 7) | GROUP, cycles=100)
        self.assertTrue(any(p[1] & (1 << 42) for p in packets))
        self.assertFalse(packets[-1][1] & (1 << 42))
        self.assertEqual(packets[-1][1] & GROUP, 1 << 41)
        self.assertTrue(packets[-1][1] & (1 << 7))
        self.assertEqual(packets[-1][2][7], 85)

    def test_bad_group_lengths_flags_and_nrc_back_off(self):
        for reply in (bytes.fromhex("41693f0102"), bytes.fromhex("4169ff804080ff00ff"),
                      bytes.fromhex("41693f804080ff00ff00"), bytes.fromhex("7f0131")):
            with self.subTest(reply=reply.hex()):
                source = responses(); source[bytes.fromhex("120169")] = reply
                packets, requests, _ = self.run_worker(source, selected=GROUP, cycles=70)
                self.assertEqual(requests.count(bytes.fromhex("120169")), 1)
                self.assertTrue(all(not p[1] & GROUP for p in packets))

    def test_refused_legacy_command_backs_off_without_clearing_coolant(self):
        source = responses((5, 0x20, 0x2c))
        source[bytes.fromhex("12012c")] = bytes.fromhex("7f0131")
        packets, requests, _ = self.run_worker(source, selected=(1 << 7) | (1 << 39), cycles=130)
        self.assertEqual(requests.count(bytes.fromhex("12012c")), 1)
        self.assertTrue(all(not p[1] & (1 << 39) for p in packets))
        self.assertTrue(packets[-1][1] & (1 << 7))

    def test_group_recovers_after_actual_five_second_backoff(self):
        source = responses()
        key = bytes.fromhex("120169")
        source[key] = [bytes.fromhex("7f0121"), source[key]]
        source.times.clear()
        packets, _, _ = self.run_worker(source, selected=GROUP, cycles=190)
        self.assertEqual(len(source.times[key]), 2)
        self.assertGreaterEqual(source.times[key][1]-source.times[key][0], 4.8)
        self.assertEqual(packets[-1][1] & GROUP, GROUP)
        self.assertEqual(packets[-1][2][43], 0)

    def test_slow_bitmap_cannot_shorten_the_group_read_interval(self):
        class SlowBitmap(transport.Responses):
            def __getitem__(self, key):
                value = super().__getitem__(key)
                if key == bytes.fromhex("120160"): time.sleep(.3)
                return value
        source = SlowBitmap(responses())
        packets, _, _ = self.run_worker(source, selected=GROUP, cycles=100)
        reads = source.times[bytes.fromhex("120169")]
        self.assertGreaterEqual(len(reads), 2)
        self.assertTrue(all(b-a >= 1.9 for a, b in zip(reads, reads[1:])), reads)
        self.assertTrue(packets[-1][1] & GROUP)

    def test_most_preserves_support_flags_valid_zero_and_cleanup(self):
        packets, _, error = self.run_worker(responses(), most=True, most_mode="egr-up", cycles=90)
        self.assertEqual(packets[-1][0], "NBG9")
        self.assertEqual(packets[-1][1] & ALL_EGR, sum(1 << i for i in (39, 40, 42, 46)))
        self.assertEqual(packets[-1][2][42], 100)
        self.assertEqual(packets[-1][2][46], 0)
        self.assertIn("unregister_target=1 unregister_client=1 free=1", error)

    def test_gasoline_stays_legacy_until_new_gauges_are_selected(self):
        packets, requests, _ = self.run_worker(responses(), selected=1 << 5, diesel=False, cycles=20)
        self.assertTrue(all(p[0] == "NBG7" for p in packets))
        self.assertTrue(all(r == bytes.fromhex("122256d7") for r in requests))
        packets, requests, _ = self.run_worker(responses(), selected=1 << 46, diesel=False, cycles=65)
        self.assertTrue(packets[-1][1] & (1 << 46))
        self.assertIn(bytes.fromhex("120160"), requests)


if __name__ == "__main__":
    unittest.main()
