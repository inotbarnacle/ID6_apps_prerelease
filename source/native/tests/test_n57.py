"""Experimental N57 profile and xDrive interlocks over real loopback HSFZ sockets."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
import unittest

import test_drive_apps
from test_transport import exact, frame

BUILD = Path(__file__).resolve().parents[1] / "build"
VIN = "TEST0000000000001"  # synthetic fixture; not a vehicle capture
INDEX = "010203"  # synthetic identity, not an N57 compatibility claim


def dyn(did, raw):
    return [("2c03f303", ["6c03f303"]),
            ("2c01f303" + did + "0102", ["6c01f303"]),
            ("22f303", ["62f303" + raw])]


class N57Test(unittest.TestCase):
    def xdrive(self, extra, speed="0000", rpm="0640", vin=VIN, index=INDEX, ack="6edbe1", value=0,
               egs="0f14b0", selector="08", park_status="000100000000"):
        tx = [("22f150", ["62f1500f1860"]), ("22dbe1", ["62dbe101"]),
              ("22f150", ["62f1500f1480"]), ("224002", ["62400209c4200f"]),
              ("22f190", ["62f190" + vin.encode().hex()]), ("22f150", ["62f150" + egs])]
        targets = ["29", "29", "19", "19", "12", "18"]
        if egs == "0f14b0":
            tx += [("224306", ["624306" + selector])]; targets += ["18"]
            if selector == "08":
                tx += [("22da27", ["62da27" + park_status])]; targets += ["18"]
        park_ok = egs == "0f14b0" and selector == "08" and park_status in ("000100000000", "000100000001")
        if park_ok:
            tx += [("22f150", ["62f150" + index])]; targets += ["12"]
        if park_ok and index == INDEX:
            tx += [("2c03f303", ["6c03f303"]),
                   ("2c01f303595501024c140102", ["6c01f303"]),
                   ("22f303", ["62f303" + rpm + speed])]
            targets += ["12"] * 3
            if rpm not in ("0000", "ffff") and speed == "0000":
                tx += [("2edbe1%02x" % value, [ack] if ack else [])]
                targets += ["29"]
                if ack in ("6edbe1", None):
                    tx += [("224002", ["62400209c4" + ("20" if value else "28") + "0f"])]
                    targets += ["19"]
        return test_drive_apps.DriveAppsTest.run_worker(self, "xdrive-control", ["--drive-mode", str(value), "--n57-sgbd", INDEX, *extra],
                                       tx, targets=targets, env=dict(os.environ, NBTEVO_OWN_VIN=VIN))

    def test_xdrive_uses_dde_not_mode01(self):
        self.assertEqual(self.xdrive([]), "Transfer case confirms RWD.\n")
        self.assertEqual(self.xdrive([], value=1), "Transfer case confirms AWD.\n")

    def test_xdrive_refuses_motion_off_implausible_and_wrong_identity(self):
        for changes in ({"speed": "0001"}, {"speed": "0001", "value": 1},
                        {"speed": "ffff"}, {"rpm": "0000"},
                        {"rpm": "ffff"}, {"index": "040506"}):
            with self.subTest(changes=changes):
                self.assertIn("Needs engine running", self.xdrive([], **changes))

    def test_xdrive_uses_selector_not_actual_gear_for_park(self):
        for changes in ({"selector": "05"}, {"selector": "06"}, {"selector": "07"},
                        {"selector": "ff"}, {"selector": "0800"},
                        {"egs": "040506"}, {"egs": "0f14b000"}, {"park_status": "000300000000"},
                        {"park_status": "000100030000"}, {"park_status": "000100000002"},
                        {"park_status": "0001000000ff"}, {"park_status": "0001"},
                        {"park_status": "00010000000000"}):
            for value in (0, 1):
                with self.subTest(changes=changes, value=value):
                    self.assertIn("confirmed Park", self.xdrive([], value=value, **changes))
        self.assertEqual(self.xdrive([], park_status="000100000001"), "Transfer case confirms RWD.\n")

    def test_xdrive_refuses_nrc_without_claiming_success(self):
        self.assertIn("refused this request", self.xdrive([], ack="7f2e31"))

    def test_missing_write_ack_still_requires_transfer_case_readback(self):
        self.assertEqual(self.xdrive([], ack=None), "Transfer case confirms RWD.\n")

    def test_missing_owner_vin_sends_no_actuation(self):
        tx = [("22f150", ["62f1500f1860"]), ("22dbe1", ["62dbe101"]),
              ("22f150", ["62f1500f1480"]), ("224002", ["62400209c4200f"])]
        # Empty explicitly overrides any developer's real consent environment.
        out = test_drive_apps.DriveAppsTest.run_worker(self, "xdrive-control", ["--drive-mode", "0"], tx,
                                      targets=["29", "29", "19", "19"],
                                      env=dict(os.environ, NBTEVO_OWN_VIN=""))
        self.assertIn("Owner VIN missing", out)

    def test_mismatched_owner_vin_sends_no_actuation(self):
        tx = [("22f150", ["62f1500f1860"]), ("22dbe1", ["62dbe101"]),
              ("22f150", ["62f1500f1480"]), ("224002", ["62400209c4200f"]),
              ("22f190", ["62f190" + b"TEST0000000000002".hex()])]
        out = test_drive_apps.DriveAppsTest.run_worker(self, "xdrive-control", ["--drive-mode", "0"], tx,
                                      targets=["29", "29", "19", "19", "12"],
                                      env=dict(os.environ, NBTEVO_OWN_VIN=VIN))
        self.assertIn("Owner VIN missing, unreadable or mismatched", out)

    def gauges(self, actual=INDEX, selected=(1 << 39) - 1, overrides=None, dynamic_delay=0, cycles=1,
               refuse_definition=0, read_reply=None):
        errors, seen = [], []
        done = threading.Event()
        values = {0x4841: 25122, 0x4CF0: 32768, 0x4746: 39321, 0x461B: 19000,
                  0x4846: 2931, 0x4517: 19000, 0x5242: 14500, 0x5955: 1600,
                  0x4C14: 0, 0x47DC: 1000, 0x50A6: 2931,
                  0x44BE: 1000, 0x44F8: 22000, 0x44BF: 1500000,
                  0x44EF: 16000, 0x44F2: 18000, 0x5306: 50000, 0x48A5: 28000,
                  0x4458: 26000, 0x464C: 0x8000}
        values.update(overrides or {})
        with socket.socket() as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            tcp.bind(("127.0.0.1", 0)); tcp.listen(); tcp.settimeout(.1)
            udp.bind(("127.0.0.1", 0)); udp.settimeout(3)
            def serve():
                defined = []
                try:
                    while not done.is_set():
                        try:
                            conn = tcp.accept()[0]
                        except socket.timeout:
                            continue
                        with conn:
                            conn.settimeout(3)
                            while True:
                                try:
                                    size, ctrl = struct.unpack(">IH", exact(conn, 6))
                                except EOFError:
                                    break
                                body = exact(conn, size)
                                self.assertEqual(ctrl, 1)
                                ecu, req = body[1], body[2:]
                                self.assertEqual(body[0], 0xF4)
                                seen.append((ecu, req.hex()))
                                if req[:1] == b"\x2c" or req == b"\x22\xf3\x03":
                                    time.sleep(dynamic_delay)
                                reply = bytes([0x7F, req[0], 0x31])
                                if req == b"\x22\xf1\x50":
                                    reply = b"\x62\xf1\x50" + bytes.fromhex(actual)
                                elif req == b"\x2c\x03\xf3\x03":
                                    defined.clear(); reply = b"\x6c\x03\xf3\x03"
                                elif req[:4] == b"\x2c\x01\xf3\x03":
                                    did = int.from_bytes(req[4:6], "big")
                                    width = 4 if did == 0x44BF else 2
                                    self.assertEqual(req[6:], bytes([1, width]))
                                    if did != refuse_definition:
                                        defined.append((did, width)); reply = b"\x6c\x01\xf3\x03"
                                elif req == b"\x22\xf3\x03":
                                    if all(values[d] is not None for d, _ in defined):
                                        reply = b"\x62\xf3\x03" + b"".join(values[d].to_bytes(w, "big") for d, w in defined)
                                    if read_reply is not None:
                                        reply = bytes.fromhex(read_reply)
                                elif ecu == 0x18 and req == b"\x22\xda\x12":
                                    reply = b"\x62\xda\x12\x00\x83"
                                conn.sendall(frame(1, bytes([ecu, 0xF4]) + reply))
                except Exception as exc:
                    errors.append(exc)
            thread = threading.Thread(target=serve)
            thread.start()
            try:
                result = subprocess.run([BUILD / "boostdeck-n57", "--host", "127.0.0.1", "--port", str(tcp.getsockname()[1]),
                                         "--n57-sgbd", INDEX, "--signals", str(selected), "--cycles", str(cycles),
                                         "--udp-port", str(udp.getsockname()[1])],
                                        capture_output=True, timeout=5, check=True)
            finally:
                done.set()
            for _ in range(cycles):
                packet = udp.recv(2048).decode().split()
            thread.join(4)
            self.assertFalse(thread.is_alive())
            if errors:
                raise errors[0]
        return packet, seen, result

    def test_gauges_factory_values_and_gasoline_channels_blank(self):
        p, seen, _ = self.gauges()
        self.assertEqual(p[0], "NBGA")
        valid = int(p[3])
        value = lambda slot: float(p[5 + 2 * slot])
        self.assertAlmostEqual(value(0), 1.30 * 14.5037738, places=3)
        self.assertAlmostEqual(value(5), 1799.997417, places=5)  # 0x9999 * .045777
        self.assertEqual(value(7), 90)
        self.assertEqual(value(11), 800)
        expected = [152.59 / 10, 22000 * .045777 - 1000, 1500000,
                    16000 * .031281 - 50, 18000 * .031281 - 50,
                    50000 * .001526, 704.404, 704.404 * 800 * 6.283185307179586 / 60000]
        for slot, number in enumerate(expected, 31):
            self.assertTrue(valid & (1 << slot), f"slot {slot} is blank")
            self.assertAlmostEqual(value(slot), number, places=5)
        self.assertFalse(valid & ((1 << 1) | (1 << 2) | (1 << 6)))  # AFR/timing/oil pressure
        self.assertFalse(any(q.startswith("01") for _, q in seen))
        self.assertTrue(all(q.startswith(("22", "2c")) for _, q in seen))
        self.assertFalse(any("464c" in q for _, q in seen))  # Explicit selection only.

    def test_relative_torque_is_separate_from_sae_load_and_uses_f303(self):
        mask = 1 << 40
        p, seen, _ = self.gauges(selected=mask | (1 << 13) | (1 << 23))
        self.assertEqual(int(p[3]), mask)
        self.assertAlmostEqual(float(p[5 + 2 * 40]), 50.003968, places=6)
        self.assertEqual([q for _, q in seen], ["22f150"] + [q for q, _ in dyn("464c", "8000")])

    def test_relative_torque_rejects_wrong_pin_nrc_format_and_bounds(self):
        p, seen, _ = self.gauges(selected=1 << 40, actual="040506")
        self.assertEqual(int(p[3]), 0)
        self.assertEqual(seen, [(0x12, "22f150")])
        p, seen, _ = self.gauges(selected=1 << 40, refuse_definition=0x464C)
        self.assertEqual(int(p[3]), 0)
        self.assertNotIn((0x12, "22f303"), seen)
        for reply in ("7f2231", "62f30380", "62f303800000", "62f3048000", "62f303ffff"):
            with self.subTest(reply=reply):
                p, _, _ = self.gauges(selected=1 << 40, read_reply=reply)
                self.assertEqual(int(p[3]), 0)

    def test_relative_torque_sample_age_includes_hsfz_read_latency(self):
        p, _, _ = self.gauges(selected=1 << 40, dynamic_delay=.15)
        self.assertEqual(int(p[3]), 1 << 40)
        self.assertGreaterEqual(int(p[6 + 2 * 40]), 400)

    def test_power_alone_reads_both_dependencies(self):
        p, seen, _ = self.gauges(selected=1 << 38)
        self.assertEqual(int(p[3]), 1 << 38)
        self.assertEqual([q for _, q in seen], ["22f150"] +
                         [q for q, _ in dyn("48a5", "6d60")] +
                         [q for q, _ in dyn("5955", "0640")])

    def test_power_triple_uses_one_pair_with_identical_sample_age(self):
        mask = (1 << 11) | (1 << 37) | (1 << 38)
        p, seen, _ = self.gauges(selected=mask, dynamic_delay=.01)
        self.assertEqual(int(p[3]), mask)
        self.assertEqual([q for _, q in seen], ["22f150"] +
                         [q for q, _ in dyn("48a5", "6d60")] +
                         [q for q, _ in dyn("5955", "0640")])
        self.assertEqual(len({int(p[6 + 2 * slot]) for slot in (11, 37, 38)}), 1)
        self.assertAlmostEqual(float(p[5 + 2 * 38]),
                               float(p[5 + 2 * 37]) * float(p[5 + 2 * 11]) * 6.283185307179586 / 60000,
                               places=4)

    def test_power_triple_dependency_failure_blanks_whole_sample(self):
        mask = (1 << 11) | (1 << 37) | (1 << 38)
        for overrides in ({0x48A5: None}, {0x5955: None}, {0x5955: 65535}):
            with self.subTest(overrides=overrides):
                p, _, _ = self.gauges(selected=mask, overrides=overrides)
                self.assertEqual(int(p[3]), 0)

    def test_base_fuel_level_uses_catalog_dde_definition(self):
        p, seen, _ = self.gauges(selected=1 << 39)
        self.assertEqual(int(p[3]), 1 << 39)
        self.assertAlmostEqual(float(p[5 + 2 * 39]), 49.582, places=4)
        self.assertEqual([q for _, q in seen], ["22f150"] +
                         [q for q, _ in dyn("4458", "6590")])
        p, _, _ = self.gauges(selected=1 << 39, overrides={0x4458: None})
        self.assertEqual(int(p[3]), 0)
        p, seen, _ = self.gauges(selected=1 << 20)
        self.assertEqual(int(p[3]), 0)  # litres must never masquerade as the old percent channel
        self.assertEqual(seen, [(0x12, "22f150")])

    def test_missing_or_implausible_dependency_blanks_power(self):
        for overrides in ({0x48A5: None}, {0x5955: None}, {0x5955: 65535}, {0x48A5: 65535}):
            with self.subTest(overrides=overrides):
                p, _, _ = self.gauges(selected=1 << 38, overrides=overrides)
                self.assertEqual(int(p[3]), 0)

    def test_slow_power_pair_does_not_reset_sample_age(self):
        p, _, _ = self.gauges(selected=1 << 38, dynamic_delay=.45)
        self.assertEqual(int(p[3]), 1 << 38)
        self.assertGreaterEqual(int(p[6 + 2 * 38]), 2500)  # HMI must consider it stale immediately

    def test_slow_boost_pair_does_not_reset_sample_age(self):
        p, seen, _ = self.gauges(selected=1 << 0, dynamic_delay=.45)
        self.assertEqual(int(p[3]), 1 << 0)
        self.assertAlmostEqual(float(p[5]), 1.30 * 14.5037738, places=3)
        self.assertEqual([q for _, q in seen], ["22f150"] +
                         [q for q, _ in dyn("4841", "6222")] +
                         [q for q, _ in dyn("4cf0", "8000")])
        # Both F303 reads belong to one sample. Six delayed replies consume the
        # entire 2500 ms budget before publish, leaving no extra display lifetime.
        self.assertGreaterEqual(int(p[6]), 2500)

    def test_gateway_yields_do_not_starve_later_diesel_selectors(self):
        mask = (1 << 0) | (1 << 11) | (1 << 37) | (1 << 38)
        p, seen, _ = self.gauges(selected=mask, dynamic_delay=.09, cycles=10)
        self.assertIn((0x12, "2c01f30348a50102"), seen)
        self.assertTrue(int(p[3]) & (1 << 38))

    def test_rejected_dpf_does_not_hide_other_diesel_values(self):
        p, _, _ = self.gauges(selected=(1 << 31) | (1 << 36), overrides={0x44BE: None})
        self.assertEqual(int(p[3]), 1 << 36)

    def test_adblue_definition_nrc31_leaves_it_blank_and_keeps_dpf(self):
        # Captured 2C 01 F303 5306 01 02 -> 7F 2C 31; do not read a rejected slot.
        p, seen, _ = self.gauges(selected=(1 << 31) | (1 << 36), refuse_definition=0x5306)
        self.assertEqual(int(p[3]), 1 << 31)
        self.assertEqual([q for _, q in seen], ["22f150"] +
                         [q for q, _ in dyn("44be", "03e8")] +
                         ["2c03f303", "2c01f30353060102"])

    def test_bad_signal_mask_never_starts_a_collector(self):
        for mask in ("-1", "0", str(1 << 41), str(1 << 64), "1x"):
            with self.subTest(mask=mask):
                result = subprocess.run([BUILD / "boostdeck-n57", "--demo", "--signals", mask],
                                        capture_output=True, timeout=2)
                self.assertEqual(result.returncode, 2)

    def test_wrong_dde_pin_leaves_all_values_blank(self):
        p, seen, _ = self.gauges(actual="040506")
        self.assertEqual(int(p[3]), 0)
        self.assertEqual(seen, [(0x12, "22f150")])


if __name__ == "__main__":
    unittest.main()
