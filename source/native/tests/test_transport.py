"""Exercise the real C collector against a loopback HSFZ gateway; no car needed."""
import os
import re
import signal
import socket
import struct
import subprocess
import threading
import time
import unittest
from pathlib import Path

BIN = str(Path(__file__).resolve().parents[1] / "build/boost-deck")
VALUES = {0x4201: bytes.fromhex("63ca"), 0x4205: bytes.fromhex("31e5"),
          0x5889: bytes.fromhex("104e"), 0x4A49: bytes.fromhex("004e"),
          0x580F: bytes.fromhex("82"), 0xDA12: bytes.fromhex("008e")}
VALUES.update({0x56D7: bytes.fromhex("1169"), 0x586F: bytes.fromhex("09c1"), 0x4300: bytes.fromhex("0bf8")})
# DME8FF_R (G20, see dme_signal_table.h): its verified DIDs are the defaults above, so they are read
# first. UNKNOWN_SGBD_VALUES answers an index this table does not know instead.
VALUES[0xF150] = bytes.fromhex("0f2422")
# D73N57A0 (diesel DDE): its own DIDs for ambient, boost, intake air, rail and coolant.
DIESEL_VALUES = dict(VALUES)
DIESEL_VALUES[0xF150] = {0x12: bytes.fromhex("0f10a0"), 0x18: bytes.fromhex("000000")}
DIESEL_VALUES.update({0x4CF0: bytes.fromhex("7d00"), 0x4841: bytes.fromhex("4e20"),
                      0x520F: bytes.fromhex("5a"), 0x4748: bytes.fromhex("0190"),
                      0x461B: bytes.fromhex("2a30")})
# DME9X3 (G78): named in f150_table.h, no row in dme_signal_table.h, so no DID is known for it.
NO_ROW_VALUES = dict(VALUES)
NO_ROW_VALUES[0xF150] = {0x12: bytes.fromhex("0f3b30"), 0x18: bytes.fromhex("000000")}
UNKNOWN_SGBD_VALUES = dict(VALUES)
UNKNOWN_SGBD_VALUES[0xF150] = bytes.fromhex("000000")
CAR_VALUES = {0x4201: bytes.fromhex("6589"), 0x4205: bytes.fromhex("32dd"),
              0x5889: bytes.fromhex("1015"), 0x4A49: bytes.fromhex("ffff"),
              0x580F: bytes.fromhex("8c"), 0xDA12: bytes.fromhex("005a")}
CAR_VALUES.update({did: VALUES[did] for did in (0x56D7, 0x586F, 0x4300, 0xF150)})
# An F150 index this repo has calibration for (G65, see dme_signal_table.h): every slot but
# timing gets its own DID. 0x4A49 (timing, unchanged) and 0xDA12 (EGS, never in this table) stay
# the defaults above.
DME9X2_VALUES = {0xF150: bytes.fromhex("0f3690"), 0xEF51: bytes.fromhex("0c80"),
                  0x4C5D: bytes.fromhex("ea60"), 0xED50: bytes.fromhex("0400"),
                  0x4A49: VALUES[0x4A49], 0xEF50: bytes.fromhex("aa"), 0xDA12: VALUES[0xDA12],
                  0xEF2C: bytes.fromhex("1388"), 0x4C24: bytes.fromhex("09c4"),
                  0x4C97: bytes.fromhex("2c00"), 0xEC3F: bytes.fromhex("1400")}
# An EGS F150 index this repo has calibration for (GSZFBCE, see egs_signal_table.h): the DME side
# answers an index not in dme_signal_table.h, so it keeps the dme8ff_r default throughout.
EGS_OVERRIDE_VALUES = dict(VALUES)
EGS_OVERRIDE_VALUES[0xF150] = {0x12: bytes.fromhex("0f2422"), 0x18: bytes.fromhex("0f25d0")}
EGS_OVERRIDE_VALUES[0x6000] = bytes.fromhex("82")

def frame(ctrl, body):
    return struct.pack(">IH", len(body), ctrl) + body


def exact(conn, n):
    out = b""
    while len(out) < n:
        data = conn.recv(n - len(out))
        if not data:
            raise EOFError
        out += data
    return out


class TransportTest(unittest.TestCase):
    def run_collector(self, mode="normal", extra=(), cycles=1, env=None):
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(("127.0.0.1", 0))
        udp.settimeout(4)
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(4)
        requests, errors = [], []
        paced_delays = []

        def gateway():
            try:
                conn, _ = server.accept()
                with conn:
                    conn.settimeout(3)
                    if mode == "paced":
                        # Measure collector scheduling against the fixture's 30 ms delay.
                        # The separate fragmentation test exercises TCP buffering.
                        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    while True:
                        first = conn.recv(1)
                        if not first:
                            break
                        length, ctrl = struct.unpack(">IH", first + exact(conn, 5))
                        self.assertEqual(ctrl, 1)
                        req = exact(conn, length)
                        requests.append(req)
                        self.assertEqual(req[0], 0xF4)
                        if req == bytes.fromhex("f4120105"):
                            reply = "12f441057a" if mode == "s55_coolant" else "12f4410550" if mode in ("unknown_sgbd", "no_row_sgbd") else "12f47f0112"
                            conn.sendall(frame(1, bytes.fromhex(reply)))
                            continue
                        if req == bytes.fromhex("f4120140"):
                            self.assertIn(mode, ("fuel_supported","fuel_unsupported"))
                            mask = "00000004" if mode == "fuel_supported" else "00000000"
                            conn.sendall(frame(1, bytes.fromhex("12f44140"+mask)))
                            continue
                        if req == bytes.fromhex("f412015e"):
                            self.assertEqual(mode,"fuel_supported")
                            conn.sendall(frame(1, bytes.fromhex("12f4415e0064")))
                            continue
                        if req == bytes.fromhex("f4120123"):
                            reply = "12f4412303e8" if mode in ("rail_fallback", "unknown_sgbd", "no_row_sgbd") else "12f47f0112"
                            conn.sendall(frame(1, bytes.fromhex(reply)))
                            continue
                        if length == 4 and req[2] == 0x01:
                            if mode in ("unknown_sgbd", "no_row_sgbd"):
                                pid = req[3]
                                data = {0x05: "50", 0x0F: "50", 0x0B: "96", 0x33: "65", 0x23: "03e8"}.get(pid)
                                if data:
                                    conn.sendall(frame(1, bytes.fromhex("12f441%02x%s" % (pid, data))))
                                    continue
                            # A Mode 01 fallback PID this fixture has no value for: not supported.
                            conn.sendall(frame(1, bytes([req[1], 0xF4, 0x7F, 0x01, 0x12])))
                            continue
                        self.assertEqual(length, 5)
                        self.assertEqual(req[2], 0x22)  # read only
                        ecu, did = req[1], int.from_bytes(req[3:], "big")
                        # F150 (SGBD identification) is sent on both the DME and the EGS; 0x6000
                        # is gszfbce's own transmission-temperature DID, read in DA12's place.
                        if did == 0xF150:
                            self.assertIn(ecu, (0x12, 0x18))
                        else:
                            self.assertEqual(ecu, 0x18 if did in (0xDA12, 0x6000) else 0x12)
                        if mode == "timeout":
                            time.sleep(.8)
                            return
                        if mode == "oversized":
                            conn.sendall(struct.pack(">IH", 500000, 1))
                            return
                        if mode == "heartbeat":
                            conn.sendall(frame(0x12, bytes([0, 0xF4])))
                            # Read the full reply, including its declared body.
                            alive_header = exact(conn, 6)
                            alive_length, alive_ctrl = struct.unpack(">IH", alive_header)
                            self.assertEqual(alive_ctrl, 0x13)
                            self.assertEqual(alive_length, 2)
                            self.assertEqual(exact(conn, alive_length), bytes([0, 0xF4]))
                        conn.sendall(frame(2, req))  # ack
                        # Wrong source with a valid-looking payload must be ignored.
                        conn.sendall(frame(1, bytes([0x99, 0xF4, 0x62]) + req[3:] + b"\0\0"))
                        conn.sendall(frame(1, bytes([ecu, 0xF4, 0x7F, 0x22, 0x78])))
                        if ((mode == "rail_fallback" and did == 0x56D7) or
                                (mode == "no_trans" and did == 0xDA12) or
                                (mode == "no_afr" and did == 0x5889)):
                            result = bytes([ecu, 0xF4, 0x7F, 0x22, 0x31])
                        else:
                            values = CAR_VALUES if mode == "car_snapshot" else \
                                     DME9X2_VALUES if mode == "dme9x2" else \
                                     EGS_OVERRIDE_VALUES if mode == "gszfbce" else \
                                     UNKNOWN_SGBD_VALUES if mode.startswith("unknown_sgbd") else \
                                     DIESEL_VALUES if mode == "diesel_sgbd" else \
                                     NO_ROW_VALUES if mode == "no_row_sgbd" else VALUES
                            payload = values[did]
                            if did == 0xF150 and isinstance(payload, dict):
                                payload = payload[ecu]  # F150 goes to both the DME and the EGS
                            if mode == "extra_pressure" and did == 0x4205:
                                payload += b"\xaa"
                            if mode == "extra_iat" and did == 0x580F:
                                payload += b"\xaa"
                            if mode == "s55_coolant" and did == 0x4300:
                                payload = b"\xae"
                            result = bytes([ecu, 0xF4, 0x62]) + req[3:] + payload
                        if mode == "paced":
                            started = time.monotonic()
                            time.sleep(.030)
                            paced_delays.append((time.monotonic() - started) * 1000.0)
                        raw = frame(1, result)
                        # Fragment both header and body to catch partial-read bugs.
                        for part in (raw[:2], raw[2:7], raw[7:]):
                            conn.sendall(part)
            except Exception as exc:
                errors.append(exc)

        t = threading.Thread(target=gateway, daemon=True)
        t.start()
        try:
            result = subprocess.run([BIN, "--host", "127.0.0.1", "--port", str(server.getsockname()[1]),
                                     "--udp-port", str(udp.getsockname()[1]), "--cycles", str(cycles), *extra],
                                    capture_output=True, text=True, timeout=10, env=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            packets = [udp.recv(512).decode().split() for _ in range(cycles)]
            packet = packets[-1]
            self.assertEqual(packet[0], "NBG8")
            self.assertGreater(int(packet[4]), 0)
            t.join(2)
            self.assertFalse(t.is_alive())
            if errors:
                raise errors[0]
            self.paced_delays = paced_delays
            return packet, requests, result.stderr
        finally:
            udp.close()
            server.close()

    def test_recorded_values_and_fragmented_transport(self):
        p, requests, _ = self.run_collector()
        self.assertEqual(len(requests), 12)  # + the 22 F150 read and the baro PID (33) for boost
        self.assertEqual(int(p[2]), 0)  # not demo
        self.assertEqual(int(p[3]), 255)
        values = [float(p[i]) for i in (5, 7, 9, 11, 13)]
        self.assertAlmostEqual(values[0], 0)
        self.assertAlmostEqual(values[1], 4174*16/65535*14.7, places=3)
        self.assertAlmostEqual(values[2], 7.8)
        self.assertAlmostEqual(values[3], 49.5)
        self.assertAlmostEqual(values[4], 94)
        self.assertAlmostEqual(float(p[15]), 89.14)
        self.assertAlmostEqual(float(p[17]), 2.497)
        self.assertAlmostEqual(float(p[19]), 33.25, places=1)  # the DME8FF_R row uses -273.14

    def test_intercooler_coolant_is_read_when_selected(self):
        p, requests, _ = self.run_collector("dme9x2", ("--signals", "4294967295"))
        self.assertIn(bytes.fromhex("f41222ec3f"), requests)
        self.assertTrue(int(p[3]) & (1 << 31))
        self.assertAlmostEqual(float(p[5 + 2 * 31]), 40.0)  # 0x1400 / 128

    def test_f150_identification_switches_to_the_cars_own_dids(self):
        """A 22 F150 reply for an SGBD in dme_signal_table.h (here, a G65 DME9X2) is read instead
        of the dme8ff_r default -- different DIDs, different scaling, same gauges."""
        p, requests, _ = self.run_collector("dme9x2")
        self.assertEqual(len(requests), 12)  # + F150 and the baro PID (33)
        self.assertEqual(int(p[3]), 251)  # timing: the DME9X2 row has no DID for it, and PID 0E is refused
        self.assertNotIn(bytes.fromhex("f412224300"), requests)  # the dme8ff_r coolant DID
        self.assertIn(bytes.fromhex("f41222ef51"), requests)     # the dme9x2 ambient DID instead
        self.assertAlmostEqual(float(p[5]), (0xea60*.001 - 0x0c80*.0125)*.145037738, places=5)
        self.assertAlmostEqual(float(p[7]), 0x0400*97656/1e8*14.7, places=5)
        self.assertNotIn(bytes.fromhex("f412224a49"), requests)  # no default DID for a signal the row lacks
        self.assertIn(bytes.fromhex("f412010e"), requests)       # its PID is tried instead
        self.assertAlmostEqual(float(p[11]), 0xaa*.75-48)
        self.assertAlmostEqual(float(p[13]), 94)    # EGS transmission temperature: no override here
        self.assertAlmostEqual(float(p[15]), 0x1388*.02)
        self.assertAlmostEqual(float(p[17]), 0x09c4*.001)
        self.assertAlmostEqual(float(p[19]), 0x2c00/128.0)

    def test_egs_f150_identification_switches_the_transmission_temperature_did(self):
        """A 22 F150 reply for an SGBD in egs_signal_table.h (here, GSZFBCE) is read instead of
        the dme8ff_r-generation default (EGS DA12) -- the DME side stays on its own default."""
        p, requests, _ = self.run_collector("gszfbce")
        self.assertEqual(len(requests), 12)  # + F150 and the baro PID (33)
        self.assertEqual(int(p[3]), 255)
        self.assertNotIn(bytes.fromhex("f41822da12"), requests)  # the DA12 default
        self.assertIn(bytes.fromhex("f418226000"), requests)     # the gszfbce transmission DID
        self.assertAlmostEqual(float(p[13]), 0x82*1.0 - 40)      # raw-40C, 1 byte
        self.assertAlmostEqual(float(p[5]), 0)                   # DME side: unaffected, same as the
        self.assertAlmostEqual(float(p[7]), 4174*16/65535*14.7, places=3)  # default test

    def test_unknown_sgbd_reads_standard_pids_instead_of_the_default_dids(self):
        """The four DIDs a diesel DDE reuses must not be read on a car the table cannot name."""
        p, requests, _ = self.run_collector("unknown_sgbd", cycles=50)
        for pid in ("05", "0f", "23", "0b", "33"):
            self.assertIn(bytes.fromhex("f41201" + pid), requests)
        for did in ("4300", "580f", "56d7", "4205"):
            self.assertNotIn(bytes.fromhex("f41222" + did), requests)
        self.assertAlmostEqual(float(p[11]), 40)    # intake air 0x50 - 40 over Mode 01
        self.assertAlmostEqual(float(p[15]), 100)   # rail 0x03e8 * .1 bar
        self.assertAlmostEqual(float(p[19]), 40)    # coolant

    def test_diesel_sgbd_reads_only_its_own_dids(self):
        p, requests, _ = self.run_collector("diesel_sgbd", cycles=50)
        dids = {r[3:].hex() for r in requests if r[2] == 0x22 and r[3:] != bytes.fromhex("f150")}
        self.assertEqual(dids, {"4cf0", "4841", "520f", "4748", "461b", "da12"})
        self.assertAlmostEqual(float(p[11]), 50)        # intake air 0x5a - 40
        self.assertAlmostEqual(float(p[19]), 0x2a30*.01 - 100)  # coolant

    def test_named_sgbd_without_a_row_keeps_the_default_dids_and_the_pids(self):
        _, requests, _ = self.run_collector("no_row_sgbd", cycles=50)
        dids = {r[3:].hex() for r in requests if r[2] == 0x22 and r[3:] != bytes.fromhex("f150")}
        # the defaults, except the four a diesel reuses (4300, 580f, 56d7, 4205): their PIDs answer
        self.assertEqual(dids, {"4201", "5889", "4a49", "586f", "da12"})
        self.assertIn(bytes.fromhex("f4120105"), requests)

    def test_unknown_sgbd_uses_the_did_once_the_pid_is_refused(self):
        _, requests, _ = self.run_collector("unknown_sgbd_refused", cycles=50)
        self.assertIn(bytes.fromhex("f4120105"), requests)
        self.assertIn(bytes.fromhex("f412224300"), requests)

    def test_unknown_petrol_car_falls_back_to_obd_for_a_refused_did(self):
        _, requests, _ = self.run_collector("no_afr")
        self.assertIn(bytes.fromhex("f4120124"), requests)         # lambda PID after the refused 5889

    def test_fast_channels_and_staggered_slow_updates(self):
        packet, requests, trace = self.run_collector("paced", ("--trace",), cycles=32)
        self.assertEqual(int(packet[3]), 255)
        self.assertEqual(len(packet), 69)  # one value and age per signal
        self.assertNotIn(bytes.fromhex("f4120152"), requests)
        # Correct for host timer coalescing in the mock gateway. Keep the same
        # collector budget at the intended delay, including the slow DID reads.
        mean_delay = sum(self.paced_delays) / len(self.paced_delays)
        scheduling_budget = 215 + 6 * max(0, mean_delay - 30)
        for did in ("4205", "5889", "4A49", "56D7", "586F"):
            samples = re.findall(rf"RAW mono_ms=(\d+) ecu=12 did={did}", trace)
            self.assertEqual(len(samples), 32)
            self.assertLess((int(samples[-1])-int(samples[1]))/30, scheduling_budget)
        for did in ("580F", "DA12", "4300"):
            samples = re.findall(rf"RAW mono_ms=(\d+) ecu=\w+ did={did}", trace)
            gaps = [int(b)-int(a) for a,b in zip(samples[1:], samples[2:])]
            self.assertTrue(gaps)
            self.assertTrue(all(1700 < gap < 2300 for gap in gaps), (did,gaps))

    def test_rejected_transmission_is_missing(self):
        p, _, _ = self.run_collector("no_trans")
        self.assertEqual(int(p[3]), 239)

    def test_refused_fast_read_backs_off_without_holding_up_other_gauges(self):
        packet, requests, _ = self.run_collector("no_afr", cycles=4)
        self.assertEqual(int(packet[3]), 253)
        self.assertEqual(requests.count(bytes.fromhex("f412225889")), 1)
        self.assertEqual(requests.count(bytes.fromhex("f412224a49")), 4)

    def test_one_byte_s55_coolant_reply_uses_standard_obd_coolant(self):
        packet, requests, _ = self.run_collector("s55_coolant", cycles=3)
        self.assertIn(bytes.fromhex("f412224300"), requests)
        self.assertEqual(requests.count(bytes.fromhex("f4120105")), 1)
        self.assertTrue(int(packet[3]) & (1 << 7))
        self.assertEqual(float(packet[19]), 82)

    def test_gasoline_rail_pressure_falls_back_to_obd_after_did_refusal(self):
        packet, requests, _ = self.run_collector("rail_fallback", cycles=4)
        self.assertEqual(requests.count(bytes.fromhex("f4120123")), 1)
        self.assertEqual(float(packet[15]), 100)
        self.assertEqual(int(packet[3]), 255)

    def test_fuel_rate_respects_advertised_support_and_keeps_boost_live(self):
        fuel_bit=1 << 19
        for mode, expected in (("fuel_supported", True), ("fuel_unsupported", False)):
            with self.subTest(mode=mode):
                packet, requests, _ = self.run_collector(mode, ("--signals",str(1|fuel_bit)),cycles=32)
                self.assertEqual(requests.count(bytes.fromhex("f4120140")),1)
                self.assertEqual(bytes.fromhex("f412015e") in requests,expected)
                self.assertTrue(int(packet[3]) & 1)
                self.assertEqual(bool(int(packet[3]) & fuel_bit),expected)

    def test_heartbeat_reply_uses_full_ediabaslib_shape(self):
        p, requests, _ = self.run_collector("heartbeat")
        self.assertEqual(len(requests), 12)  # + the 22 F150 read and the baro PID (33) for boost
        self.assertEqual(int(p[3]), 255)

    def test_extra_did_bytes_do_not_become_valid_readings(self):
        # The refused DID is retried over Mode 01: boost needs baro (33) and MAP (0B), IAT is 0F.
        for mode, expected_mask, pids in [("extra_pressure", 254, ("33", "0b")), ("extra_iat", 247, ("33", "0f"))]:
            with self.subTest(mode=mode):
                p, requests, _ = self.run_collector(mode)
                self.assertEqual(len(requests), 11 + len(pids))  # + the 22 F150 read
                for pid in pids:
                    self.assertIn(bytes.fromhex("f41201" + pid), requests)
                self.assertEqual(int(p[3]), expected_mask)

    def test_boostdeck_collects_when_hud_sharing_port_is_occupied(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as holder:
            holder.bind(("127.0.0.1", 42871))
            packet, _, trace = self.run_collector()
            self.assertEqual(int(packet[3]), 255)
            self.assertIn("without HUD sharing", trace)

    def test_timeout_is_bounded(self):
        start = time.monotonic()
        p, _, _ = self.run_collector("timeout")
        self.assertEqual(int(p[3]), 0)
        self.assertLess(time.monotonic()-start, 2)

    def test_oversized_frame_is_rejected(self):
        p, _, _ = self.run_collector("oversized")
        self.assertEqual(int(p[3]), 0)

    def test_this_cars_snapshot_and_ambiguous_timing_trace(self):
        p, requests, trace = self.run_collector("car_snapshot", ("--trace",))
        self.assertEqual(len(requests), 13)  # + F150, baro (33) and Mode 01 timing (0E)
        self.assertIn(bytes.fromhex("f412010e"), requests)  # the ambiguous DID falls back to the PID
        self.assertEqual(int(p[3]), 251)  # timing alone remains unavailable
        self.assertAlmostEqual(float(p[5]), .027761, places=5)
        self.assertAlmostEqual(float(p[7]), 14.775593, places=3)
        self.assertEqual(float(p[11]), 57)
        self.assertEqual(float(p[13]), 42)
        self.assertIn("did=4A49 payload=FFFF unavailable reason=ambiguous-FFFF", trace)
        self.assertEqual(trace.count("RAW mono_ms="), 9)

    def test_no_trans_flag_never_queries_egs(self):
        p, requests, _ = self.run_collector(extra=("--no-trans",))
        self.assertEqual(int(p[3]), 239)
        self.assertEqual(len(requests), 10)  # + F150 and the baro PID (33)
        self.assertTrue(all(r[1] == 0x12 for r in requests))

    def run_most(self, args, fake="up", cycles=50):
        """Run with the in-process fake DSI; return every packet, the exit status and stderr."""
        env = dict(os.environ)
        env.pop("ID6_FAKE_MOST", None)
        if fake:
            env["ID6_FAKE_MOST"] = fake
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(.5)
            r = subprocess.run([BIN, *args, "--udp-port", str(udp.getsockname()[1]), "--cycles", str(cycles)],
                               capture_output=True, text=True, timeout=20, env=env)
            packets = []
            try:
                while True:
                    packets.append(udp.recv(512).decode().split())
            except socket.timeout:
                pass
        return packets, r.returncode, r.stderr

    @staticmethod
    def closed_port():
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]

    def assert_most_values(self, p):
        self.assertEqual(int(p[3]), 239)  # every gauge but the EGS transmission temperature
        self.assertAlmostEqual(float(p[7]), 4174*16/65535*14.7, places=5)
        self.assertAlmostEqual(float(p[9]), 7.8)
        self.assertAlmostEqual(float(p[11]), 49.5)
        self.assertAlmostEqual(float(p[15]), 89.14)
        self.assertAlmostEqual(float(p[19]), 33.25)

    def test_unreachable_gateway_switches_to_most(self):
        packets, rc, err = self.run_most(["--host", "127.0.0.1", "--port", str(self.closed_port())])
        self.assertEqual(rc, 0, err)
        self.assertEqual([int(p[3]) for p in packets[:4]], [0]*4)  # the fifth failed connect opens MOST
        self.assertIn("transport: MOST", err)
        self.assert_most_values(packets[-1])
        self.assertIn("FAKE-MOST exit unregister_target=1 unregister_client=1 free=1", err)

    def test_failed_most_open_is_closed_and_hsfz_retried(self):
        start = time.monotonic()
        packets, rc, err = self.run_most(["--host", "127.0.0.1", "--port", str(self.closed_port())],
                                         fake="no-server", cycles=50)
        self.assertEqual(rc, 0, err)
        self.assertLess(time.monotonic()-start, 8)
        self.assertIn("MOST open failed: no server", err)
        self.assertEqual(err.count("MOST open failed"), 1)  # backed off, not retried every tick
        self.assertIn("free=1", err)  # the failed open's DSI client was freed
        self.assertTrue(all(int(p[3]) == 0 for p in packets))

    def test_missing_gateway_file_uses_most(self):
        packets, rc, err = self.run_most(["--trace"], cycles=3)
        self.assertEqual(rc, 0, err)
        self.assert_most_values(packets[-1])
        self.assertNotIn("did=DA12", err)
        self.assertIn("ecu=12 did=4205 payload=31E5", err)

    def test_boostdeck_skips_egs_on_most(self):
        packets, rc, err = self.run_most(["--transport", "most", "--trace"], cycles=4)
        self.assertEqual(rc, 0, err)
        self.assertNotIn("did=DA12", err)
        self.assertNotIn("ecu=18", err)
        self.assertTrue(all(int(p[3]) & (1 << 4) == 0 for p in packets))  # transmission temperature invalid
        self.assertEqual(int(packets[-1][3]), 239)

    def test_unmatched_most_read_does_not_end_the_gauge_scan(self):
        packets, rc, err = self.run_most(["--transport", "most", "--trace"],
                                         fake="gauge-mismatch", cycles=3)  # the PID is refused first, then the DID
        self.assertEqual(rc, 0, err)
        self.assertTrue(int(packets[-1][3]) & (1 << 7))
        self.assertFalse(int(packets[-1][3]) & (1 << 1))
        self.assertIn("ecu=12 did=4300 payload=0BF8", err)

    def test_working_gateway_never_touches_most(self):
        env = dict(os.environ, ID6_FAKE_MOST="up")
        p, requests, err = self.run_collector(cycles=3, env=env)
        self.assertEqual(int(p[3]), 255)
        self.assertNotIn("FAKE-MOST", err)
        _, rc, err = self.run_most(["--transport", "hsfz", "--host", "127.0.0.1",
                                    "--port", str(self.closed_port())], cycles=10)
        self.assertEqual(rc, 0, err)
        self.assertNotIn("FAKE-MOST", err)

    def test_missing_dsi_library_keeps_emitting_no_data(self):
        packets, rc, err = self.run_most(["--transport", "most"], fake=None, cycles=4)
        self.assertEqual(rc, 0, err)
        self.assertEqual(len(packets), 4)
        self.assertTrue(all(int(p[3]) == 0 for p in packets))
        self.assertIn("MOST open failed: no library", err)
        self.assertEqual(err.count("FAKE-MOST bind"), 1)  # not retried every tick

    def test_lost_most_link_exits_for_a_fresh_worker(self):
        packets, rc, err = self.run_most(["--transport", "most"], fake="drop", cycles=50)
        self.assertEqual(rc, 1, err)
        self.assertLess(len(packets), 50)

    def test_busy_tunnel_keeps_fresh_values(self):
        """A read skipped for a busy lease leaves a still-fresh gauge shown."""
        env = dict(os.environ, ID6_FAKE_MOST="up")
        env.pop("ID6_FAKE_MOST_DELAY_MS", None)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(1)
            proc = subprocess.Popen([BIN, "--transport", "most", "--udp-port", str(udp.getsockname()[1]),
                                     "--cycles", "30"], stderr=subprocess.DEVNULL, env=env)
            masks, held = [], False
            try:
                while proc.poll() is None or len(masks) < 30:
                    try:
                        mask = int(udp.recv(512).decode().split()[3])
                    except socket.timeout:
                        break
                    masks.append(mask)
                    if mask == 239 and not held:
                        held = True
                        lock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        lock.bind(("127.0.0.1", 42861))
                        threading.Timer(1.0, lock.close).start()
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.wait()
        self.assertTrue(held, masks)
        shown = masks[masks.index(239):]
        # Boost needs a fresh ambient read too, so only the other MOST gauges are checked.
        self.assertTrue(all(m & 238 == 238 for m in shown), shown)

    def test_hsfz_needs_host(self):
        r = subprocess.run([BIN, "--transport", "hsfz"], capture_output=True)
        self.assertEqual(r.returncode, 2)

    def test_sigterm_closes_most_session(self):
        env = dict(os.environ, ID6_FAKE_MOST="up")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(3)
            proc = subprocess.Popen([BIN, "--session", "--udp-port", str(udp.getsockname()[1])],
                                    stderr=subprocess.PIPE, text=True, env=env)
            try:
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    if int(udp.recv(512).decode().split()[3]) == 239:
                        break
                else:
                    self.fail("no MOST values")
                start = time.monotonic()
                proc.send_signal(signal.SIGTERM)
                self.assertEqual(proc.wait(5), 0)
                self.assertLess(time.monotonic()-start, 3)
            finally:
                if proc.poll() is None:
                    proc.kill()
                _, err = proc.communicate()
        self.assertIn("FAKE-MOST exit unregister_target=1 unregister_client=1 free=1", err)

    def test_demo_never_uses_vehicle(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(2)
            r = subprocess.run([BIN, "--demo", "--cycles", "1", "--udp-port", str(udp.getsockname()[1])],
                               capture_output=True, timeout=2)
            self.assertEqual(r.returncode, 0)
            self.assertEqual(udp.recv(512).decode().split()[2:4], ["1", str((1 << 32) - 1)])  # demo marks all 32 signals valid
        r = subprocess.run([BIN, "--demo", "--host", "127.0.0.1"], capture_output=True)
        self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
