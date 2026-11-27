"""Production reagent reads against synthetic loopback HSFZ and fake MOST."""
import socket
import time
import unittest
from unittest.mock import patch

import test_egt_transport as transport

LEVEL, AVERAGE, DEMAND = (1 << i for i in (55, 56, 57))
REAGENT = LEVEL | AVERAGE | DEMAND
PAGE = bytes.fromhex("120180")
READ = bytes.fromhex("120185")


def responses(supported=(5, 0x20, 0x40, 0x60, 0x80, 0x85)):
    result = transport.responses(supported)
    result[PAGE] = transport.capabilities.bitmap(0x80, supported)
    result[READ] = bytes.fromhex("41850f010000c88001000001")
    result[bytes.fromhex("12224300")] = bytes.fromhex("6243000dfe")
    return result


class ReagentTransportTest(unittest.TestCase):
    def run_worker(self, replies, selected=REAGENT, **kwargs):
        return transport.EgtTransportTest().run_worker(replies, selected, **kwargs)

    def test_independent_fields_share_one_advertised_read_with_exact_units_and_period(self):
        source = responses()
        packets, requests, _ = self.run_worker(source, cycles=100)
        self.assertEqual(packets[-1][0], "NBG11")
        self.assertEqual(packets[-1][1] & REAGENT, REAGENT)
        self.assertAlmostEqual(packets[-1][2][55], 128*100/255, places=5)
        self.assertEqual(packets[-1][2][56:58], [1.28, 1])
        for pid in (0, 0x20, 0x40, 0x60, 0x80):
            self.assertEqual(requests.count(bytes((0x12, 1, pid))), 1)
        stamps = source.times[READ]
        self.assertGreaterEqual(len(stamps), 2)
        self.assertTrue(all(b-a >= 1.9 for a, b in zip(stamps, stamps[1:])), stamps)
        self.assertTrue(all(request[1] == 1 for request in requests))

    def test_old_gasoline_and_diesel_selections_never_request_higher_pages_or_reagent(self):
        for diesel in (False, True):
            packets, requests, _ = self.run_worker(responses(), selected=1 << 7, diesel=diesel, cycles=18)
            self.assertNotIn(PAGE, requests);self.assertNotIn(READ, requests)
            self.assertTrue(all(not p[1] & REAGENT for p in packets))

    def test_detected_defaults_do_not_expand_their_reviewed_request_pool(self):
        packets, requests, _ = self.run_worker(responses(), automatic=True, cycles=30)
        self.assertNotIn(PAGE, requests);self.assertNotIn(READ, requests)
        self.assertTrue(all(not p[1] & REAGENT for p in packets))

    def test_missing_continuation_or_measurement_advertisement_never_probes(self):
        for supported in ((), (0x20,), (0x20, 0x40), (0x20, 0x40, 0x60), (0x20, 0x40, 0x60, 0x80)):
            source = responses(supported)
            packets, requests, _ = self.run_worker(source, cycles=25)
            self.assertNotIn(READ, requests)
            if 0x80 not in supported: self.assertNotIn(PAGE, requests)
            self.assertTrue(all(not p[1] & REAGENT for p in packets))

    def test_refused_or_malformed_bitmap_stays_unknown_with_backoff(self):
        for bad in (bytes.fromhex("7f0131"), bytes.fromhex("4180080000"), bytes.fromhex("41800800000000")):
            source = responses();source[PAGE] = bad
            packets, requests, _ = self.run_worker(source, cycles=60)
            self.assertEqual(requests.count(PAGE), 1)
            self.assertNotIn(READ, requests)
            self.assertTrue(all(not p[1] & REAGENT for p in packets))

    def test_live_support_flags_clear_missing_fields_without_fallback(self):
        source = responses()
        source[READ] = [source[READ], bytes.fromhex("418504ffffffff0000000000")]
        source.times.clear()  # setup inspection is not an actual ECU request
        packets, requests, _ = self.run_worker(source, selected=REAGENT | (1 << 7), cycles=100)
        self.assertTrue(any(p[1] & REAGENT == REAGENT for p in packets))
        self.assertEqual(packets[-1][1] & REAGENT, LEVEL)
        self.assertEqual(packets[-1][2][55], 0)
        self.assertTrue(packets[-1][1] & (1 << 7))
        self.assertNotIn(bytes.fromhex("12012f"), requests)
        self.assertNotIn(bytes.fromhex("12015e"), requests)

    def test_valid_zero_full_scale_and_runtime_only_support_remain_distinct(self):
        for reply, wanted, expected in (("418507000000000000000000", REAGENT, [0, 0, 0]),
                                         ("418507ffffffffff00000000", REAGENT, [100, 327.675, 327.675]),
                                         ("418508ffffffffffffffffff", 0, None)):
            source = responses();source[READ] = bytes.fromhex(reply)
            packets, _, _ = self.run_worker(source, cycles=35)
            self.assertEqual(packets[-1][1] & REAGENT, wanted)
            if expected is not None: self.assertEqual(packets[-1][2][55:58], expected)

    def test_invalid_read_clears_only_reagent_and_backs_off(self):
        for bad in (bytes.fromhex("7f0131"), bytes.fromhex("41850f010000c8800100000100"),
                    bytes.fromhex("41858f010000c88001000001")):
            source = responses();source[READ] = [source[READ], bad]
            source.times.clear()
            packets, requests, _ = self.run_worker(source, selected=REAGENT | (1 << 7), cycles=100)
            self.assertTrue(any(p[1] & REAGENT for p in packets))
            self.assertEqual(requests.count(READ), 2)
            self.assertFalse(packets[-1][1] & REAGENT)
            self.assertTrue(packets[-1][1] & (1 << 7))

    def test_delayed_reply_uses_actual_start_for_period_and_age(self):
        class Delayed(transport.Responses):
            def __getitem__(self, key):
                value = super().__getitem__(key)
                if key == READ: time.sleep(.3)
                return value
        source = Delayed(responses());ages = []
        original = transport.decode
        def observe(packet):
            result = original(packet)
            if result[0] == "NBG11" and result[1] & REAGENT:
                fields = packet.decode().split();ages.append([int(v) for v in fields[6::2]][55:58])
            return result
        with patch.object(transport, "decode", side_effect=observe):
            packets, _, _ = self.run_worker(source, cycles=100)
        self.assertTrue(packets[-1][1] & REAGENT)
        self.assertTrue(ages and all(all(age >= 250 for age in row) for row in ages))
        stamps = source.times[READ]
        self.assertGreaterEqual(len(stamps), 2)
        self.assertTrue(all(b-a >= 1.9 for a, b in zip(stamps, stamps[1:])), stamps)

    def test_most_uses_the_same_advertised_fields_and_releases_ownership(self):
        packets, requests, errors = self.run_worker(responses(), most=True, most_mode="reagent-up", cycles=65)
        self.assertEqual(packets[-1][1] & REAGENT, REAGENT)
        self.assertEqual(packets[-1][2][56:58], [1.28, 1])
        self.assertFalse(requests)
        self.assertIn("transport: MOST", errors)
        self.assertIn("unregister_target=1 unregister_client=1 free=1", errors)
        for port in (42861, 42871, 42875):
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(("127.0.0.1", port))


if __name__ == "__main__":
    unittest.main()
