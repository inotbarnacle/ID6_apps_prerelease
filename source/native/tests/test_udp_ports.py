"""Force IPC-port choices without changing native workers or filtering frames."""
import errno
import socket
import unittest
from unittest.mock import patch

from udp_test_socket import telemetry_socket


class PrivateUdpTest(unittest.TestCase):
    def test_all_ipc_ports_are_skipped_and_rejected_sockets_are_closed(self):
        original = socket.socket
        forced = list(range(42860, 42880))
        sockets, assigned = [], []
        class ForcedPortsSocket(original):
            def bind(self, address):
                super().bind((address[0], forced.pop(0) if forced else 0))
                sockets.append(self)
                assigned.append(self.getsockname()[1])
        with patch.object(socket, "socket", ForcedPortsSocket), telemetry_socket() as receiver:
            self.assertEqual(assigned[:20], list(range(42860, 42880)))
            self.assertFalse(42860 <= receiver.getsockname()[1] <= 42879)
            self.assertTrue(all(s.fileno() == -1 for s in sockets[:-1]))
            self.assertGreaterEqual(receiver.fileno(), 0)
        self.assertTrue(all(s.fileno() == -1 for s in sockets))

    def test_bounded_failure_closes_every_rejected_socket(self):
        original = socket.socket
        sockets = []
        class HudPortSocket(original):
            def bind(self, address):
                if len(sockets) >= 32:
                    raise AssertionError("receiver allocation exceeded its retry bound")
                super().bind((address[0], 42870))
                sockets.append(self)
        with patch.object(socket, "socket", HudPortSocket), self.assertRaisesRegex(OSError, "outside project IPC ports"):
            telemetry_socket()
        self.assertEqual(len(sockets), 32)
        self.assertTrue(all(s.fileno() == -1 for s in sockets))

    def test_bind_error_is_propagated_and_closes_the_socket(self):
        original = socket.socket
        sockets = []
        error = OSError(errno.EACCES, "synthetic bind refusal")
        class RefusedSocket(original):
            def bind(self, address):
                sockets.append(self)
                raise error
        with patch.object(socket, "socket", RefusedSocket), self.assertRaises(OSError) as raised:
            telemetry_socket()
        self.assertIs(raised.exception, error)
        self.assertEqual(len(sockets), 1)
        self.assertEqual(sockets[0].fileno(), -1)

    def test_shift_frame_remains_valid_when_hud_port_is_the_first_choice(self):
        from test_drive_apps import DriveAppsTest
        original = socket.socket
        assigned = []
        class HudFirstSocket(original):
            def bind(self, address):
                if self.type == socket.SOCK_DGRAM and address[1] == 0:
                    super().bind((address[0], 42870 if not assigned else 0))
                    assigned.append(self.getsockname()[1])
                else:
                    super().bind(address)
        with patch.object(socket, "socket", HudFirstSocket):
            DriveAppsTest().test_shift_worker_decodes_live_rpm_and_speed()
        self.assertEqual(assigned[0], 42870)
        self.assertGreaterEqual(len(assigned), 2)
        self.assertFalse(42860 <= assigned[-1] <= 42879)


if __name__ == "__main__":
    unittest.main()
