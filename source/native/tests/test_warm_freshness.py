"""Observe the real WarmDeck worker against a deliberately slow local gateway."""
import errno
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
import unittest

from test_transport import exact, frame

BUILD = Path(__file__).resolve().parents[1] / 'build'
WARM_PORT, ACTIVE_PORT, LEASE_PORT = 42862, 42878, 42861


class Gateway:
    def __init__(self, mode='normal'):
        self.mode = mode
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0)); self.listener.listen(4)
        self.listener.settimeout(.1)
        self.port = self.listener.getsockname()[1]
        self.stopping = threading.Event()
        self.lock = threading.Lock()
        self.counts = [0, 0, 0]
        self.observed = {}
        self.errors = []
        self.thread = threading.Thread(target=self.serve, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.stopping.set(); self.thread.join(3); self.listener.close()
        assert not self.thread.is_alive(), 'local gateway did not stop'
        assert not self.errors, self.errors

    def serve(self):
        while not self.stopping.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            with connection:
                connection.settimeout(2)
                while not self.stopping.is_set():
                    try:
                        size, control = struct.unpack('>IH', exact(connection, 6))
                        request = exact(connection, size)
                        received = time.clock_gettime_ns(time.CLOCK_MONOTONIC) // 1_000_000
                        assert control == 1
                        if request[:3] == b'\xf4\x12\x01' and len(request) == 4:
                            pid = request[3]
                            if pid in (0, 0x20, 0x40):
                                # Also exercise the capability-gated integration.
                                bits = {0: 0x08000001, 0x20: 1, 0x40: 0x10}[pid]
                                reply = bytes((0x41, pid)) + bits.to_bytes(4, 'big')
                                connection.sendall(frame(1, b'\x12\xf4' + reply))
                                continue
                            assert pid in (5, 0x5c), request
                            signal = 0 if pid == 0x5c else 1
                        else:
                            assert request == b'\xf4\x18\x22\xda\x12', request
                            signal = 2
                        index = self.counts[signal]; self.counts[signal] += 1
                        target = 0x18 if signal == 2 else 0x12
                        if self.mode == 'zero' and signal == 2:
                            reply = b'\x62\xda\x12\x58'  # positive prefix, wrong data width
                        elif self.mode != 'normal' and signal != 1:
                            reply = b'\x7f' + bytes((0x22 if signal == 2 else 1, 0x31))
                        elif self.mode == 'timeout' and index:
                            time.sleep(.65)  # exceeds the worker's 450-ms deadline
                            continue
                        else:
                            value = 0 if self.mode == 'zero' else (85, 75, 40)[signal] + index
                            reply = (b'\x62\xda\x12' + (value + 48).to_bytes(2, 'big') if signal == 2
                                     else bytes((0x41, 0x5c if signal == 0 else 5, value + 40)))
                            with self.lock:
                                self.observed[signal, value] = received
                            time.sleep(.15)
                        connection.sendall(frame(1, bytes((target, 0xf4)) + reply))
                    except (EOFError, ConnectionResetError, BrokenPipeError):
                        break
                    except Exception as error:
                        self.errors.append(error)
                        return


class WarmFreshnessTest(unittest.TestCase):
    def observe(self, gateway, accept):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
            receiver.bind(('127.0.0.1', WARM_PORT)); receiver.settimeout(.25)
            environment = dict(os.environ); environment.pop('ID6_FAKE_MOST', None)
            process = subprocess.Popen([str(BUILD / 'warmdeck-collector'), '--host', '127.0.0.1',
                                        '--port', str(gateway.port), '--cycles', '8'],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
            try:
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    self.assertIsNone(process.poll(), 'WarmDeck worker exited before the observation')
                    try:
                        data, peer = receiver.recvfrom(128)
                    except socket.timeout:
                        continue
                    self.assertEqual(peer, ('127.0.0.1', ACTIVE_PORT))
                    self.assertEqual(len(data), 48)
                    packet = struct.unpack('=3Q3d', data)
                    if accept(packet):
                        return packet
                self.fail('WarmDeck did not produce the required observation')
            finally:
                if process.poll() is None:
                    process.terminate()
                output, errors = process.communicate(timeout=3)
                self.assertEqual(process.returncode, 0, (output, errors))
    def assert_resources_released(self):
        for port in (ACTIVE_PORT, LEASE_PORT):
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                try:
                    probe.bind(('127.0.0.1', port))
                except OSError as error:
                    if error.errno == errno.EADDRINUSE:
                        self.fail('worker retained resource port ' + str(port))
                    raise

    def test_delayed_replies_keep_request_age_for_each_temperature(self):
        with Gateway() as gateway:
            packet = self.observe(gateway, lambda p: all(p[:3]))
            with gateway.lock:
                for signal in range(3):
                    request_received = gateway.observed[signal, packet[3 + signal]]
                    self.assertLessEqual(packet[signal], request_received,
                                         'reply delay must count toward sample age')
        self.assert_resources_released()

    def test_supported_zero_is_fresh_while_refused_or_malformed_fluids_stay_unavailable(self):
        with Gateway('zero') as gateway:
            packet = self.observe(gateway, lambda p: p[1] != 0)
            self.assertEqual((packet[0], packet[2], packet[4]), (0, 0, 0))
            with gateway.lock:
                self.assertLessEqual(packet[1], gateway.observed[1, 0])
        self.assert_resources_released()

    def test_timeout_invalidates_a_previous_temperature_without_refreshing_it(self):
        with Gateway('timeout') as gateway:
            seen_valid = False

            def invalidated(packet):
                nonlocal seen_valid
                if packet[1]:
                    self.assertEqual(packet[4], 75)
                    seen_valid = True
                return seen_valid and not packet[1] and packet[4] == 75

            self.observe(gateway, invalidated)
        self.assert_resources_released()


if __name__ == '__main__':
    unittest.main()
