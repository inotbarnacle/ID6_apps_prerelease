"""Pinned synthetic DDE over real worker loops and the host-only MOST fixture. No vehicle addresses."""
from pathlib import Path
import errno
import math
import os
import re
import socket
import struct
import subprocess
import threading
import time
import unittest

from test_transport import exact, frame

BUILD = Path(__file__).resolve().parents[1] / "build"
INDEX = "010203"  # Synthetic identity, never a compatibility assertion.
RPM, TORQUE, POWER, REGEN_DISTANCE = 11, 37, 38, 33
WORKERS = ("boostdeck-n57", "shift-deck")


class Worker:
    def __init__(self, name, transport="most", mode="n57-up", port=None, signals=1 << RPM, delay=0,
                 speed_only=False, read_delay=0):
        self.name, self.frames, self.lines = name, [], []
        self.speed_only = speed_only
        self.closed = socket.socket()
        self.closed.bind(("127.0.0.1", 0))  # Bound but never listening: deterministic ECONNREFUSED.
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", 0))
        self.udp.settimeout(.02)
        args = [BUILD / name, "--host", "127.0.0.1", "--port", str(port or self.closed.getsockname()[1]),
                "--transport", transport, "--n57-sgbd", INDEX, "--udp-port", str(self.udp.getsockname()[1])]
        if name == "boostdeck-n57":
            args += ["--signals", str(signals)]
        elif speed_only:
            args += ["--speed-only"]
        env = dict(os.environ, ID6_FAKE_MOST=mode, ID6_FAKE_MOST_DELAY_MS=str(delay),
                   ID6_FAKE_MOST_READ_DELAY_MS=str(read_delay))
        self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, env=env)
        self.reader = threading.Thread(target=lambda: self.lines.extend(iter(self.proc.stderr.readline, "")))
        self.reader.start()

    @property
    def error(self):
        return "".join(self.lines)

    @property
    def requests(self):
        return re.findall(r"FAKE-N57 q=([0-9A-F]+) r=", self.error)

    def until(self, condition, timeout=8, observe=None):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if observe:
                observe()
            if condition():
                return True
            try:
                packet = self.udp.recv(4096)
            except socket.timeout:
                continue
            if self.name == "boostdeck-n57":
                p = packet.decode().split()
                assert p[0] == "NBGA", p
                self.frames.append({"valid": int(p[3]), "sent": int(p[2]),
                                    "values": list(map(float, p[5::2])), "ages": list(map(int, p[6::2]))})
            else:
                magic, sampled, rpm, speed = struct.unpack("=QQii", packet)
                assert magic == 0xFFFFFFFF31454444
                self.frames.append({"valid": speed >= 0 and (rpm >= 0 or self.speed_only),
                                    "rpm": rpm, "speed": speed, "sampled": sampled,
                                    "received": time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1000000})
        return bool(condition())

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()
        try:
            self.proc.wait(6)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(2)
            raise
        finally:
            self.reader.join(2)
            self.proc.stderr.close()
            self.udp.close()
            self.closed.close()


class Hsfz:
    """F150 and stateful single/pair F303, for transport preference/recovery tests."""
    def __init__(self, behavior="up", reads_per_group=2):
        self.behavior, self.reads_per_group = behavior, reads_per_group
        self.seen, self.errors, self.reads = [], [], 0
        self.done = threading.Event()
        self.tcp = socket.socket()
        self.tcp.bind(("127.0.0.1", 0))
        self.port = self.tcp.getsockname()[1]
        self.tcp.listen()
        self.tcp.settimeout(.1)
        self.conn = None
        self.thread = threading.Thread(target=self.serve)
        self.thread.start()

    def serve(self):
        try:
            while not self.done.is_set():
                try:
                    conn, _ = self.tcp.accept()
                except socket.timeout:
                    continue
                self.conn = conn
                defined = None
                with conn:
                    conn.settimeout(.2)
                    while not self.done.is_set():
                        try:
                            size, control = struct.unpack(">IH", exact(conn, 6))
                            body = exact(conn, size)
                        except socket.timeout:
                            continue
                        except EOFError:
                            break
                        assert control == 1 and body[:2] == b"\xf4\x12", body.hex()
                        q = body[2:]
                        self.seen.append(q.hex().upper())
                        if self.behavior == "drop" and self.reads >= self.reads_per_group:
                            self.tcp.close()
                            return
                        out = bytes((0x7f, q[0], 0x31))
                        if q == bytes.fromhex("22f150"):
                            out = bytes.fromhex("62f150" + INDEX)
                        elif self.behavior == "silent":
                            continue  # TCP and identity work, but every dynamic request times out.
                        elif q == bytes.fromhex("2c03f303"):
                            defined = None
                            out = bytes.fromhex("6c03f303")
                        elif q[:4] == bytes.fromhex("2c01f303"):
                            assert len(q) in (8, 12)
                            defined = []
                            for pos in range(4, len(q), 4):
                                assert q[pos+2:pos+4] == b"\x01\x02" and q[pos:pos+2] in (b"\x59\x55", b"\x4c\x14", b"\x46\x1b")
                                defined.append(q[pos:pos+2])
                            out = bytes.fromhex("6c01f303")
                        elif q == bytes.fromhex("22f303") and defined:
                            values = {b"\x59\x55": b"\x06\x40", b"\x4c\x14": b"\0\0", b"\x46\x1b": b"\x4a\x38"}
                            value = b"".join(values[did] for did in defined)
                            out = bytes.fromhex("62f303") + value
                            self.reads += 1
                        conn.sendall(frame(1, b"\x12\xf4" + out))
        except OSError as exc:
            if not self.done.is_set():
                self.errors.append(exc)
        except Exception as exc:
            self.errors.append(exc)

    def close(self):
        self.done.set()
        self.tcp.close()
        if self.conn:
            self.conn.close()
        self.thread.join(2)
        assert not self.thread.is_alive()
        assert not self.errors, self.errors


class N57MostTest(unittest.TestCase):
    def worker(self, *args, **kwargs):
        if getattr(self, "active", None):
            self.active.close()  # A failed subtest must not leave a lease held for the next one.
        w = Worker(*args, **kwargs)
        self.active = w
        self.addCleanup(w.close)
        return w

    def stop(self, w, expected=0):
        w.close()
        self.assertEqual(w.proc.returncode, expected, w.error)
        self.assertFalse(w.reader.is_alive())
        if "transport: MOST" in w.error:
            self.assertIn("FAKE-MOST exit", w.error)
            self.assertIn("free=1", w.error)
        self.assertFalse(any(q.startswith("01") for q in w.requests), w.error)
        self.assertNotIn("22DA12", w.requests)  # No EGS through the DME-only MOST tunnel.

    def live(self, w):
        return any(f["valid"] for f in w.frames)

    def replay_drive_ui(self, w, frames):
        trace = "".join(f'{f["sampled"]} {f["received"]} {f["rpm"]} {f["speed"]}\n' for f in frames)
        apps = ("test-launch-deck",) if w.speed_only else ("test-shift-deck", "test-shift-deck-smooth")
        for app in apps:
            result = subprocess.run([BUILD / app, "--most-replay"], input=trace, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_auto_closed_hsfz_and_explicit_most_produce_dde(self):
        for name in WORKERS:
            for transport in ("auto", "most"):
                with self.subTest(worker=name, transport=transport):
                    w = self.worker(name, transport=transport)
                    self.assertTrue(w.until(lambda: self.live(w) and "transport: MOST" in w.error), w.error)
                    first = next(f for f in w.frames if f["valid"])
                    self.assertEqual(first["values"][RPM] if name == "boostdeck-n57" else first["rpm"], 800)
                    if name == "shift-deck":
                        self.assertEqual(first["speed"], 0)
                    q = w.requests
                    definition = "2C01F30359550102" + ("4C140102" if name == "shift-deck" else "")
                    self.assertEqual(q[:4], ["22F150", "2C03F303", definition, "22F303"])
                    self.stop(w)

    def test_explicit_hsfz_never_opens_most(self):
        for name in WORKERS:
            with self.subTest(worker=name):
                w = self.worker(name, transport="hsfz")
                w.until(lambda: False, timeout=2)  # Longer than the five-failure automatic fallback.
                self.assertFalse(self.live(w))
                self.stop(w)
                self.assertNotIn("FAKE-MOST bind", w.error)

    def test_working_hsfz_has_priority(self):
        for name in WORKERS:
            with self.subTest(worker=name):
                server = Hsfz(reads_per_group=1)
                try:
                    w = self.worker(name, transport="auto", port=server.port)
                    self.assertTrue(w.until(lambda: self.live(w), timeout=4), w.error)
                    self.stop(w)
                    self.assertNotIn("FAKE-MOST bind", w.error)
                    self.assertTrue(server.seen)
                    self.assertTrue(all(q.startswith(("22", "2C")) for q in server.seen))
                finally:
                    server.close()

    def test_hsfz_disconnect_and_dynamic_timeout_recover_to_most(self):
        for behavior in ("drop", "silent"):
            with self.subTest(behavior=behavior):
                server = Hsfz(behavior)
                try:
                    # Coolant's 2s cadence leaves identity-only polls between failures: those polls
                    # must not reset the counter and prevent a functioning MOST fallback forever.
                    w = self.worker("shift-deck" if behavior == "drop" else "boostdeck-n57",
                                    transport="auto", port=server.port, signals=1 << 7)
                    if behavior == "drop":
                        self.assertTrue(w.until(lambda: self.live(w), timeout=4), w.error)
                    after_most = []

                    def recovered():
                        if "transport: MOST" not in w.error:
                            return False
                        if not after_most:
                            after_most.append(len(w.frames))
                        return any(f["valid"] for f in w.frames[after_most[0]:])

                    self.assertTrue(w.until(recovered, timeout=16), w.error)
                    self.assertGreaterEqual(server.seen.count("22F150"), 1)
                    self.assertIn("2C03F303", server.seen)
                    self.stop(w)
                finally:
                    server.close()

    def test_bad_pin_and_failed_dynamic_reads_never_fall_back_to_mode01(self):
        for name in WORKERS:
            for mode in ("n57-wrong-pin", "n57-bad-pin", "n57-refuse-clear", "n57-refuse-define",
                         "n57-short-read", "n57-invalid-rpm"):
                with self.subTest(worker=name, mode=mode):
                    w = self.worker(name, mode=mode)
                    self.assertTrue(w.until(lambda: bool(w.frames) and bool(w.requests)), w.error)
                    self.assertFalse(self.live(w), w.error)
                    self.stop(w)
                    if mode in ("n57-wrong-pin", "n57-bad-pin"):
                        self.assertTrue(all(q == "22F150" for q in w.requests), w.error)

    def test_four_byte_definition(self):
        w = self.worker("boostdeck-n57", signals=1 << REGEN_DISTANCE)
        self.assertTrue(w.until(lambda: self.live(w)), w.error)
        self.assertEqual(next(f for f in w.frames if f["valid"])["values"][REGEN_DISTANCE], 1500000)
        self.assertIn("2C01F30344BF0104", w.requests)
        self.stop(w)

    def test_relative_torque_most_requires_pin_and_complete_dynamic_read(self):
        mask = 1 << 40
        w = self.worker("boostdeck-n57", signals=mask, delay=80)
        self.assertTrue(w.until(lambda: self.live(w)), w.error)
        first = next(f for f in w.frames if f["valid"])
        self.assertEqual(first["valid"], mask)
        self.assertAlmostEqual(first["values"][40], 50.003968, places=6)
        self.assertGreaterEqual(first["ages"][40], 200)
        self.assertEqual(w.requests[:4], ["22F150", "2C03F303", "2C01F303464C0102", "22F303"])
        self.stop(w)
        for mode in ("n57-wrong-pin", "n57-refuse-clear", "n57-refuse-define", "n57-short-read"):
            with self.subTest(mode=mode):
                w = self.worker("boostdeck-n57", signals=mask, mode=mode)
                self.assertTrue(w.until(lambda: bool(w.frames) and bool(w.requests)), w.error)
                self.assertFalse(self.live(w), w.error)
                self.stop(w)
                if mode == "n57-wrong-pin":
                    self.assertTrue(all(q == "22F150" for q in w.requests))

    def test_most_skips_requested_egs_channel(self):
        w = self.worker("boostdeck-n57", signals=(1 << RPM) | (1 << 4))
        self.assertTrue(w.until(lambda: self.live(w)), w.error)
        self.assertTrue(all(not f["valid"] & (1 << 4) for f in w.frames))
        self.stop(w)

    def test_power_pair_is_coherent_and_conservative(self):
        mask = (1 << RPM) | (1 << TORQUE) | (1 << POWER)
        w = self.worker("boostdeck-n57", signals=mask, delay=120)
        self.assertTrue(w.until(lambda: any(f["valid"] == mask for f in w.frames)), w.error)
        f = next(f for f in w.frames if f["valid"] == mask)
        self.assertAlmostEqual(f["values"][POWER], f["values"][TORQUE] * f["values"][RPM] * math.tau / 60000, places=4)
        self.assertEqual(len({f["ages"][i] for i in (RPM, TORQUE, POWER)}), 1)
        self.assertGreaterEqual(f["ages"][POWER], 500)  # Seven delayed calls, not a fresh completion timestamp.
        self.assertEqual(w.requests[:7], ["22F150", "2C03F303", "2C01F30348A50102", "22F303",
                                         "2C03F303", "2C01F30359550102", "22F303"])
        self.stop(w)

    def test_identity_rpm_speed_share_one_lease_against_competitor(self):
        w = self.worker("shift-deck", delay=120)
        checked = []

        def competitor():
            for q in w.requests[len(checked):4]:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    with self.assertRaises(OSError) as caught:
                        sock.bind(("127.0.0.1", 42861))
                    self.assertEqual(caught.exception.errno, errno.EADDRINUSE)
                    sock.sendto(b"\x01", ("127.0.0.1", 42861))
                checked.append(q)

        self.assertTrue(w.until(lambda: self.live(w), observe=competitor), w.error)
        self.assertEqual(len(checked), 4)
        self.assertEqual(checked, ["22F150", "2C03F303", "2C01F303595501024C140102", "22F303"])
        gained = False
        end = time.monotonic() + .5
        while time.monotonic() < end:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                try:
                    sock.bind(("127.0.0.1", 42861))
                    gained = True
                    break
                except OSError as exc:
                    self.assertEqual(exc.errno, errno.EADDRINUSE)
                    sock.sendto(b"\x01", ("127.0.0.1", 42861))
            time.sleep(.005)
        self.assertTrue(gained, "lease was not released for the waiting worker")
        self.stop(w)

    def test_delayed_drive_packets_cover_the_next_update_within_fixed_freshness(self):
        for speed_only in (False, True):
            with self.subTest(speed_only=speed_only):
                w = self.worker("shift-deck", delay=80, speed_only=speed_only)
                self.assertTrue(w.until(lambda: sum(f["valid"] for f in w.frames) >= 4), w.error)
                frames = [f for f in w.frames if f["valid"]]
                for previous, current in zip(frames, frames[1:]):
                    # This is the real HMI's 750ms contract, including time spent awaiting the ECU.
                    self.assertLess(current["received"] - previous["sampled"], 750, frames)
                    self.assertLessEqual(current["sampled"], current["received"])
                    self.assertGreaterEqual(current["received"] - current["sampled"], 65)
                self.stop(w)
                self.replay_drive_ui(w, frames)

    def test_busy_diagnostic_lease_does_not_publish_an_invalid_measurement(self):
        w = self.worker("shift-deck")
        self.assertTrue(w.until(lambda: self.live(w)), w.error)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as competitor:
            deadline = time.monotonic() + 1
            while True:
                try:
                    competitor.bind(("127.0.0.1", 42861))
                    break
                except OSError as exc:
                    self.assertEqual(exc.errno, errno.EADDRINUSE)
                    self.assertLess(time.monotonic(), deadline, w.error)
                    competitor.sendto(b"\x01", ("127.0.0.1", 42861))
                    time.sleep(.005)
            start = len(w.frames)
            w.until(lambda: False, timeout=.5)  # Covers the next 100ms poll and its 300ms lease deadline.
            self.assertFalse(any(not f["valid"] for f in w.frames[start:]), w.frames[start:])
        self.assertTrue(w.until(lambda: len(w.frames) > start and w.frames[-1]["valid"]), w.error)
        self.stop(w)

    def test_slow_data_reply_keeps_its_age_and_cannot_become_fresh_at_completion(self):
        for speed_only in (False, True):
            with self.subTest(speed_only=speed_only):
                w = self.worker("shift-deck", speed_only=speed_only, read_delay=800)
                self.assertTrue(w.until(lambda: any(f["valid"] for f in w.frames)), w.error)
                f = next(f for f in w.frames if f["valid"])
                self.assertGreaterEqual(f["received"]-f["sampled"], 750)
                self.assertLessEqual(f["sampled"], f["received"])
                self.stop(w)
                self.replay_drive_ui(w, [f])

    def test_refused_read_invalidates_the_previous_drive_measurement(self):
        for speed_only in (False, True):
            with self.subTest(speed_only=speed_only):
                w = self.worker("shift-deck", speed_only=speed_only, mode="n57-refuse-read-once")
                self.assertTrue(w.until(lambda: len(w.frames) >= 2), w.error)
                self.assertTrue(w.frames[0]["valid"])
                self.assertEqual((w.frames[1]["rpm"], w.frames[1]["speed"]), (-1, -1))
                self.assertGreaterEqual(w.frames[1]["sampled"], w.frames[0]["sampled"])
                self.stop(w)

    def test_drive_f303_timeout_retires_the_client_without_retry(self):
        for speed_only in (False, True):
            with self.subTest(speed_only=speed_only):
                w = self.worker("shift-deck", speed_only=speed_only, read_delay=2100)
                self.assertTrue(w.until(lambda: w.proc.poll() is not None, timeout=6), w.error)
                self.assertFalse(self.live(w), w.error)
                self.assertEqual(w.requests.count("22F303"), 1)
                self.assertEqual(w.requests[-1], "22F303")
                self.stop(w, expected=1)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                    probe.bind(("127.0.0.1", 42861))

    def test_lost_most_exits_and_releases_lease(self):
        for name in WORKERS:
            with self.subTest(worker=name):
                w = self.worker(name, mode="n57-drop")
                started = time.monotonic()
                self.assertTrue(w.until(lambda: w.proc.poll() is not None, timeout=5), w.error)
                self.assertLess(time.monotonic() - started, 5)
                self.stop(w, expected=1)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                    probe.bind(("127.0.0.1", 42861))


if __name__ == "__main__":
    unittest.main()
