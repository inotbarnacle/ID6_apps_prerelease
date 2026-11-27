"""Actual N57 workers, synthetic DSI/AAD states, and real loopback ownership/telemetry sockets."""
from pathlib import Path
import errno
import os
import signal
import socket
import struct
import subprocess
import threading
import time
import unittest

BUILD = Path(__file__).resolve().parents[1] / "build"
MAGIC = 0x31504D534455484E
RPM, BOOST, N57 = 1, 2, 4
# App IPC is deliberately unchanged. Coordination occupies its own range.
APPS = {
    "boostdeck": ("boostdeck-n57", 42860, 42971, 42975, (1 << 11) | 1),
    "shiftdeck": ("shift-deck", 42863, 42972, 42976, None),
    "launchdeck": ("shift-deck", 42871, 42973, 42977, None),
    "powerdeck": ("boostdeck-n57", 42872, 42980, 42981, (1 << 11) | (1 << 37) | (1 << 38)),
    "basediagnostik": ("boostdeck-n57", 42873, 42982, 42983, (1 << 7) | (1 << 9) | (1 << 10) | (1 << 39)),
}


def occupied(port):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        try:
            probe.bind(("127.0.0.1", port))
            return False
        except OSError as exc:
            if exc.errno != errno.EADDRINUSE:
                raise
            return True


class App:
    def __init__(self, name, transport="most", mode="n57-up", delay=0, flags=None, mask=None,
                 stop_on_request=None):
        binary, ipc, self.active, self.most, selected = APPS[name]
        self.name, self.lines, self.frames = name, [], []
        self.stop_sent = threading.Event()
        self.closed = socket.socket()
        self.closed.bind(("127.0.0.1", 0))  # HSFZ cannot connect; no address outside loopback exists here.
        self.data = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.data.bind(("127.0.0.1", ipc))
        self.data.settimeout(.01)
        args = [BUILD / binary, "--host", "127.0.0.1", "--port", str(self.closed.getsockname()[1]),
                "--transport", transport, "--n57-sgbd", "010203", "--udp-port", str(ipc)]
        if selected is not None:
            args += ["--signals", str(selected if mask is None else mask)]
        if name == "launchdeck":
            args += ["--speed-only"]
        env = dict(os.environ, ID6_FAKE_MOST=mode, ID6_FAKE_MOST_DELAY_MS=str(delay))
        for key in ("ID6_FAKE_AAD_INSTALLED", "ID6_FAKE_AAD_SHOWN", "ID6_FAKE_CARPLAY_HUD"):
            env.pop(key, None)
        env.update(flags or {})
        self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, env=env)
        def capture():
            for line in iter(self.proc.stderr.readline, ""):
                self.lines.append(line)
                if stop_on_request and not self.stop_sent.is_set() and line.startswith(
                        "FAKE-N57 q=" + stop_on_request + " r="):
                    self.proc.send_signal(signal.SIGTERM)
                    self.stop_sent.set()
        self.reader = threading.Thread(target=capture)
        self.reader.start()

    @property
    def error(self):
        return "".join(self.lines)

    def poll(self):
        try:
            data = self.data.recv(4096)
        except socket.timeout:
            return
        if data.startswith(b"NBGA "):
            self.frames.append(int(data.split()[3]) != 0)
        else:
            magic, _, rpm, speed = struct.unpack("=QQii", data)
            assert magic == 0xFFFFFFFF31454444
            self.frames.append(speed >= 0 and (rpm >= 0 or self.name == "launchdeck"))

    def close(self, killed=False):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGKILL if killed else signal.SIGTERM)
        try:
            self.proc.wait(6)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(2)
            raise
        finally:
            self.reader.join(2)
            self.proc.stderr.close()
            self.data.close()
            self.closed.close()


class CoexistenceTest(unittest.TestCase):
    def setUp(self):
        self.apps, self.samples = [], []
        self.hud = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.hud.bind(("127.0.0.1", 42970))
        self.hud.settimeout(.01)

    def tearDown(self):
        for app in reversed(self.apps):
            app.close()
        self.hud.close()

    def app(self, name, **kwargs):
        for old in self.apps:
            if old.name == name:
                old.close()  # Failed subtests must not retain the same app's fixed IPC socket.
        app = App(name, **kwargs)
        self.apps.append(app)
        return app

    def until(self, condition, timeout=8):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for app in self.apps:
                if app.proc.poll() is None:
                    app.poll()
            try:
                data, peer = self.hud.recvfrom(128)
            except socket.timeout:
                pass
            else:
                self.assertEqual(len(data), 40)
                magic, rpm_ms, boost_ms, source, valid, rpm, boost = struct.unpack("=QQQIIIi", data)
                self.assertEqual(magic, MAGIC)
                self.assertEqual(peer, ("127.0.0.1", source))
                self.assertIn(source, [values[2] for values in APPS.values()])
                self.assertEqual(valid & ~(RPM | BOOST | N57), 0)
                self.samples.append(dict(rpm_ms=rpm_ms, boost_ms=boost_ms, source=source,
                                         valid=valid, rpm=rpm, boost=boost,
                                         received=time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1000000))
            if condition():
                return True
        return bool(condition())

    def stop(self, app, killed=False):
        app.close(killed)
        self.assertEqual(app.proc.returncode, -signal.SIGKILL if killed else 0, app.error)
        self.assertFalse(app.reader.is_alive())
        self.assertFalse(occupied(app.active))
        self.assertFalse(occupied(app.most))
        if not killed:
            self.assertIn("FAKE-MOST client=" + app.name, app.error)
            self.assertIn("free=1", app.error)

    def sample(self, app, valid=None):
        return next((s for s in reversed(self.samples) if s["source"] == app.active and
                     (valid is None or s["valid"] & valid == valid)), None)

    def test_aad_installed_and_active_hud_do_not_disable_five_engine_sources(self):
        for name in APPS:
            with self.subTest(source=name):
                app = self.app(name, transport="auto", flags={"ID6_FAKE_AAD_INSTALLED": "1",
                                                              "ID6_FAKE_AAD_SHOWN": "1"})
                expected = N57 if name in ("launchdeck", "basediagnostik") else N57 | RPM
                if name == "boostdeck":
                    expected |= BOOST
                self.assertTrue(self.until(lambda: any(app.frames) and self.sample(app, expected)), app.error)
                self.assertIsNone(app.proc.poll())
                self.assertTrue(occupied(app.active) and occupied(app.most))
                value = self.sample(app, expected)
                if expected & RPM:
                    self.assertEqual(value["rpm"], 800)
                    self.assertGreater(value["rpm_ms"], 0)
                    self.assertLessEqual(value["rpm_ms"], value["received"])
                else:
                    self.assertEqual((value["rpm_ms"], value["rpm"]), (0, 0))
                    self.assertFalse(value["valid"] & RPM)
                if expected & BOOST:
                    self.assertAlmostEqual(value["boost"], 188, delta=1)
                    self.assertGreater(value["boost_ms"], 0)
                self.stop(app)

    def test_power_base_launch_can_run_together_without_ipc_or_marker_collisions(self):
        apps = [self.app(name, flags={"ID6_FAKE_AAD_INSTALLED": "1"})
                for name in ("powerdeck", "basediagnostik", "launchdeck")]
        self.assertTrue(self.until(lambda: all(any(a.frames) and self.sample(a, N57) for a in apps)),
                        "\n".join(a.error for a in apps))
        for app in apps:
            self.assertIsNone(app.proc.poll())
            self.assertTrue(occupied(app.active) and occupied(app.most))
        for app in apps:
            self.stop(app)

    def test_hud_owner_yields_before_most_open_and_crash_allows_restart(self):
        owner = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        owner.bind(("127.0.0.1", 42974))
        owner.settimeout(3)
        try:
            app = self.app("boostdeck", flags={"ID6_FAKE_AAD_INSTALLED": "1"})
            self.assertEqual(owner.recv(8), b"\x01")
            self.assertNotIn("FAKE-MOST bind", app.error)
            self.assertTrue(occupied(app.active))
        finally:
            owner.close()  # Models a HUD renderer clearing and releasing its DSI ownership.
        self.assertTrue(self.until(lambda: any(app.frames) and self.sample(app, RPM | N57)), app.error)
        self.stop(app, killed=True)
        restarted = self.app("boostdeck")
        self.assertTrue(self.until(lambda: any(restarted.frames) and "transport: MOST" in restarted.error), restarted.error)
        self.stop(restarted)

    def test_published_timestamps_are_original_observations_not_send_times(self):
        # RPM's observation starts after identity, before its three F303 calls.
        app = self.app("boostdeck", mask=1 << 11, delay=400)
        self.assertTrue(self.until(lambda: self.sample(app, RPM | N57)), app.error)
        sample = self.sample(app, RPM | N57)
        self.assertGreaterEqual(sample["received"] - sample["rpm_ms"], 1100)
        self.stop(app)

    def test_sigterm_during_f303_read_is_clean_shutdown(self):
        for name in ("shiftdeck", "powerdeck"):
            with self.subTest(source=name):
                # The fixture logs the chosen request before delaying its reply; cancel on that event.
                app = self.app(name, delay=1000, stop_on_request="22F303")
                self.assertTrue(app.stop_sent.wait(8), app.error)
                app.proc.wait(6)
                self.stop(app)
                self.assertIn("FAKE-MOST exit unregister_target=1 unregister_client=1 free=1", app.error)
                self.assertFalse(occupied(42861))
                requests = [line.split(" q=", 1)[1].split(" ", 1)[0]
                            for line in app.lines if line.startswith("FAKE-N57 q=")]
                self.assertEqual(requests.count("22F303"), 1, app.error)
                self.assertEqual(requests[-1], "22F303", app.error)

    def test_invalid_measurements_are_not_fabricated_into_hud_samples(self):
        for mode in ("n57-wrong-pin", "n57-short-read", "n57-invalid-rpm"):
            with self.subTest(mode=mode):
                app = self.app("boostdeck", mode=mode, mask=1 << 11)
                self.assertTrue(self.until(lambda: self.sample(app, N57) and "FAKE-N57 q=" in app.error), app.error)
                sample = self.sample(app, N57)
                self.assertEqual((sample["valid"], sample["rpm_ms"], sample["boost_ms"], sample["rpm"]), (N57, 0, 0, 0))
                self.stop(app)


if __name__ == "__main__":
    unittest.main()
