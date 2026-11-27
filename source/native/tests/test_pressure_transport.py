"""Synthetic standard pressure replies; production worker, no vehicle captures."""
import time
import unittest

import test_egt_transport as transport

RAIL = sum(1 << signal for signal in range(47, 51))
BOOST = sum(1 << signal for signal in range(51, 55))
PRESSURE = RAIL | BOOST
RAIL_READ = bytes.fromhex("12016d")
BOOST_READ = bytes.fromhex("120170")


def responses(supported=(0x20, 0x40, 0x60, 0x6d, 0x70)):
    result = transport.responses(supported)
    # Unused temperature/control-status bytes may contain FF.
    result.update({RAIL_READ: bytes.fromhex("416d3f27102328ff4e20ffffff"),
                   BOOST_READ: bytes.fromhex("41703f1900192a3200ffffff")})
    return result


class PressureTransportTest(unittest.TestCase):
    def run_worker(self, replies, selected=PRESSURE, **kwargs):
        return transport.EgtTransportTest().run_worker(replies, selected, **kwargs)

    def test_grouped_pressure_units_and_rates(self):
        source = responses()
        packets, requests, _ = self.run_worker(source, cycles=100)
        self.assertEqual(packets[-1][0], "NBG10")
        self.assertEqual(packets[-1][1] & PRESSURE, PRESSURE)
        self.assertEqual(packets[-1][2][47:55], [1000, 900, 2000, 6553.5, 200, 201.3125, 400, 2047.96875])
        for pid in (0, 0x20, 0x40, 0x60):
            self.assertEqual(requests.count(bytes((0x12, 1, pid))), 1)
        for key in (RAIL_READ, BOOST_READ):
            reads = source.times[key]
            self.assertEqual(len(reads), 2)
            self.assertGreaterEqual(reads[1]-reads[0], 1.9)
        self.assertTrue(all(request[1] == 1 for request in requests))

    def test_missing_continuation_or_unadvertised_pids_never_probe(self):
        for supported in ((), (0x20,), (0x20, 0x40), (0x20, 0x40, 0x60)):
            with self.subTest(supported=supported):
                packets, requests, _ = self.run_worker(responses(supported), cycles=12)
                self.assertTrue(all(not packet[1] & PRESSURE for packet in packets))
                self.assertNotIn(RAIL_READ, requests)
                self.assertNotIn(BOOST_READ, requests)

    def test_unknown_capability_does_not_grant_pressure_reads(self):
        source = responses(); source[bytes.fromhex("120160")] = bytes.fromhex("7f0131")
        packets, requests, _ = self.run_worker(source, cycles=60)
        self.assertTrue(all(not packet[1] & PRESSURE for packet in packets))
        self.assertNotIn(RAIL_READ, requests); self.assertNotIn(BOOST_READ, requests)

    def test_absent_actual_is_not_request_or_legacy_fallback(self):
        source = responses()
        source[RAIL_READ] = bytes.fromhex("416d0127100000000000000000")
        source[BOOST_READ] = bytes.fromhex("417001190000000000000000")
        packets, requests, _ = self.run_worker(source, selected=(1 << 48) | (1 << 52), cycles=65)
        self.assertTrue(all(not packet[1] & PRESSURE for packet in packets))
        self.assertNotIn(bytes.fromhex("120123"), requests)
        self.assertNotIn(bytes.fromhex("12010b"), requests)
        self.assertNotIn(bytes.fromhex("122256d7"), requests)

    def test_flags_are_rechecked_and_zero_is_valid_without_harming_coolant(self):
        source = responses((5, 0x20, 0x40, 0x60, 0x6d, 0x70))
        source[RAIL_READ] = [source[RAIL_READ], bytes.fromhex("416d0200000000ff00000000ff")]
        source.times.clear()
        packets, _, _ = self.run_worker(source, selected=PRESSURE | (1 << 7), cycles=100)
        self.assertTrue(any(packet[1] & (1 << 47) for packet in packets))
        self.assertEqual(packets[-1][1] & RAIL, 1 << 48)
        self.assertEqual(packets[-1][2][48], 0)
        self.assertEqual(packets[-1][1] & BOOST, BOOST)
        self.assertTrue(packets[-1][1] & (1 << 7))
        self.assertEqual(packets[-1][2][7], 85)

    def test_invalid_lengths_high_flags_and_nrc_back_off(self):
        for key, selected in ((RAIL_READ, RAIL), (BOOST_READ, BOOST)):
            good = responses()[key]
            for reply in (good[:-1], good+b"\0", good[:2]+b"\x80"+good[3:], bytes.fromhex("7f0131")):
                with self.subTest(pid=key.hex(), reply=reply.hex()):
                    source = responses(); source[key] = reply
                    packets, requests, _ = self.run_worker(source, selected=selected, cycles=45)
                    self.assertEqual(requests.count(key), 1)
                    self.assertTrue(all(not packet[1] & selected for packet in packets))

    def test_refusal_of_one_group_preserves_other_pressure(self):
        source = responses(); source[RAIL_READ] = bytes.fromhex("7f0131")
        packets, requests, _ = self.run_worker(source, cycles=100)
        self.assertFalse(packets[-1][1] & RAIL)
        self.assertEqual(packets[-1][1] & BOOST, BOOST)
        self.assertEqual(requests.count(RAIL_READ), 1)

    def test_recovers_after_five_second_backoff(self):
        source = responses(); source[RAIL_READ] = [bytes.fromhex("7f0121"), source[RAIL_READ]]
        source.times.clear()
        packets, _, _ = self.run_worker(source, selected=RAIL, cycles=190)
        reads = source.times[RAIL_READ]
        self.assertEqual(len(reads), 2)
        self.assertGreaterEqual(reads[1]-reads[0], 4.8)
        self.assertEqual(packets[-1][1] & RAIL, RAIL)

    def test_slow_bitmap_cannot_shorten_actual_request_interval(self):
        class SlowBitmap(transport.Responses):
            def __getitem__(self, key):
                value = super().__getitem__(key)
                if key == bytes.fromhex("120160"): time.sleep(.3)
                return value
        source = SlowBitmap(responses())
        packets, _, _ = self.run_worker(source, cycles=100)
        for key in (RAIL_READ, BOOST_READ):
            reads = source.times[key]
            self.assertGreaterEqual(len(reads), 2)
            self.assertTrue(all(b-a >= 1.9 for a, b in zip(reads, reads[1:])), reads)
        self.assertEqual(packets[-1][1] & PRESSURE, PRESSURE)

    def test_most_flags_absolute_pressure_valid_zero_and_cleanup(self):
        packets, _, error = self.run_worker(responses(), most=True, most_mode="pressure-up", cycles=90)
        self.assertEqual(packets[-1][0], "NBG10")
        self.assertEqual(packets[-1][1] & PRESSURE, sum(1 << i for i in (48, 50, 52, 54)))
        self.assertEqual([packets[-1][2][i] for i in (48, 50, 52, 54)], [1000, 0, 200, 0])
        self.assertIn("unregister_target=1 unregister_client=1 free=1", error)

    def test_gasoline_keeps_legacy_queries_and_gates_optional_pressure(self):
        packets, requests, _ = self.run_worker(responses(), selected=1 << 5, diesel=False, cycles=20)
        self.assertTrue(all(packet[0] == "NBG7" for packet in packets))
        self.assertTrue(all(request == bytes.fromhex("122256d7") for request in requests))
        packets, requests, _ = self.run_worker(responses(), selected=1 << 54, diesel=False, cycles=65)
        self.assertTrue(packets[-1][1] & (1 << 54))
        self.assertIn(bytes.fromhex("120160"), requests)
        self.assertNotIn(RAIL_READ, requests)

    def test_detected_defaults_do_not_read_optional_pressure(self):
        packets, requests, _ = self.run_worker(responses(), automatic=True, cycles=30)
        self.assertNotIn(RAIL_READ, requests); self.assertNotIn(BOOST_READ, requests)
        self.assertTrue(all(not packet[1] & PRESSURE for packet in packets))


if __name__ == "__main__":
    unittest.main()
