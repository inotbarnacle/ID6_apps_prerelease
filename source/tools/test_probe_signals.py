import argparse
import socket
import struct
import threading
import unittest
from unittest.mock import patch

from probe_signals import exchange, main, read_request


def frame(control, data):
    return struct.pack(">IH", len(data), control) + data


class SlowHeartbeat:
    """Synthetic socket whose heartbeat arrival consumes the receive budget."""
    def __init__(self, arrival):
        self.now = 0.0
        self.arrival = arrival
        self.timeout = None
        self.sent = []

    def settimeout(self, value):
        self.timeout = value

    def sendall(self, value):
        self.sent.append(value)
        if len(self.sent) > 1:
            self.now += self.timeout  # congested acknowledgement consumes its socket timeout
            raise TimeoutError("synthetic acknowledgement send timeout")

    def recv(self, length):
        self.assert_header_length(length)
        self.now = self.arrival
        return frame(0x12, b"")

    @staticmethod
    def assert_header_length(length):
        assert length == 6


class ProbeTest(unittest.TestCase):
    def test_reply_filters_and_heartbeat(self):
        client, gateway = socket.socketpair()
        with client, gateway:
            gateway.settimeout(2)
            def respond():
                self.assertEqual(gateway.recv(11), frame(1, bytes.fromhex("f412224300")))
                gateway.sendall(frame(0x12, b""))
                self.assertEqual(gateway.recv(8), frame(0x13, bytes.fromhex("00f4")))
                for value in ("18f46243000000", "12f47f2278", "12f46243000bf8"):
                    gateway.sendall(frame(1, bytes.fromhex(value)))
            thread = threading.Thread(target=respond)
            thread.start()
            result = exchange(client, 0x12, read_request("224300"))
            thread.join(3)
            self.assertFalse(thread.is_alive())
            self.assertEqual(result, {"reply": "62 43 00 0b f8", "data": "0b f8"})

    def test_refuses_actuation_and_decodes_negative_reply(self):
        for text in ("2f0100", "2e430001", "14ffffff", "2243", "01050c", "210b"):
            with self.assertRaises(argparse.ArgumentTypeError):
                read_request(text)
        client, gateway = socket.socketpair()
        with client, gateway:
            gateway.sendall(frame(1, bytes.fromhex("12f47f2231")))
            self.assertEqual(exchange(client, 0x12, read_request("22586f"))["nrc"], "31")

    def test_heartbeat_acknowledgement_send_cannot_exceed_receive_deadline(self):
        connection = SlowHeartbeat(1.8)
        with patch("probe_signals.time.monotonic", side_effect=lambda: connection.now), self.assertRaises(TimeoutError):
            exchange(connection, 0x12, read_request("0105"))
        self.assertEqual(len(connection.sent), 2)
        self.assertLessEqual(connection.now, 2)

    def test_expired_receive_budget_does_not_start_heartbeat_acknowledgement(self):
        connection = SlowHeartbeat(2)
        with patch("probe_signals.time.monotonic", side_effect=lambda: connection.now), self.assertRaises(TimeoutError):
            exchange(connection, 0x12, read_request("0105"))
        self.assertEqual(len(connection.sent), 1)

    def test_legacy_fluid_temperature_is_egs_only(self):
        client, gateway = socket.socketpair()
        with client, gateway:
            gateway.settimeout(2)
            gateway.sendall(frame(1, bytes.fromhex("18f4610a53")))
            self.assertEqual(exchange(client, 0x18, read_request("210A")), {"reply": "61 0a 53", "data": "53"})
            self.assertEqual(gateway.recv(10), frame(1, bytes.fromhex("f418210a")))
        with patch("sys.argv", ["probe_signals.py", "--local", "169.254.1.2",
                               "--gateway", "169.254.1.3", "--ecu", "12", "210a"]), \
             patch("probe_signals.socket.create_connection") as connect, \
             patch("sys.stderr"):
            with self.assertRaises(SystemExit) as error:
                main()
            self.assertEqual(error.exception.code, 2)
            connect.assert_not_called()


if __name__ == "__main__":
    unittest.main()
