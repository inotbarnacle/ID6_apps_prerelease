"""ena-gateway against a simulated gateway: discovery by HSFZ vehicle identification, and the read-only route check."""
from pathlib import Path
import socket
import struct
import subprocess
import threading
import unittest

from test_transport import exact, frame

TOOL = Path(__file__).resolve().parents[1] / "build" / "ena-gateway"
IDENT = b"DIAGADR10BMWMAC001122334455BMWVINWBA00000000000000"


class DiscoverTest(unittest.TestCase):
    def discover(self, replies):
        """Run discover against a UDP responder on 127.0.0.1 that answers with each of replies."""
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as responder:
            responder.bind(("127.0.0.1", 0))
            responder.settimeout(3)
            received = []

            def answer():
                data, sender = responder.recvfrom(64)
                received.append(data)
                for reply in replies:
                    responder.sendto(reply, sender)

            thread = threading.Thread(target=answer, daemon=True)
            thread.start()
            result = subprocess.run([TOOL, "discover", "--to", "127.0.0.1", "--port",
                                     str(responder.getsockname()[1]), "--wait", "400"],
                                    capture_output=True, text=True, timeout=5)
            thread.join(2)
            self.assertEqual(received, [bytes.fromhex("000000000011")])
            return result

    def test_prints_the_gateway_once(self):
        reply = struct.pack(">IH", len(IDENT), 0x11) + IDENT
        result = self.discover([reply, reply])
        self.assertEqual((result.returncode, result.stdout), (0, "127.0.0.1\n"))

    def test_asks_every_network_on_the_device(self):
        """Without --to it broadcasts on each of the device's networks: lo's is 127.0.0.0/8, so 127.255.255.255."""
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as responder:
            responder.bind(("0.0.0.0", 0))  # a broadcast reaches only sockets bound to the wildcard address
            responder.settimeout(3)
            received = []

            def answer():
                data, sender = responder.recvfrom(64)
                received.append(data)
                responder.sendto(struct.pack(">IH", len(IDENT), 0x11) + IDENT, sender)

            thread = threading.Thread(target=answer, daemon=True)
            thread.start()
            result = subprocess.run([TOOL, "discover", "--dev", "lo", "--port", str(responder.getsockname()[1]),
                                     "--wait", "400"], capture_output=True, text=True, timeout=5)
            thread.join(2)
        self.assertEqual(received, [bytes.fromhex("000000000011")])
        self.assertEqual((result.returncode, result.stdout), (0, "127.0.0.1\n"))

    def test_a_device_without_an_address(self):
        result = subprocess.run([TOOL, "discover", "--dev", "nonexistent0", "--wait", "100"],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual((result.returncode, result.stdout), (1, ""))
        self.assertIn("nonexistent0 has no IPv4 address", result.stderr)

    def test_ignores_other_controllers_and_other_messages(self):
        other = b"DIAGADR62BMWMAC001122334455"
        result = self.discover([struct.pack(">IH", len(other), 0x11) + other,
                                struct.pack(">IH", len(IDENT), 0x12) + IDENT, b"\x00\x11"])
        self.assertEqual((result.returncode, result.stdout), (1, ""))


class CheckTest(unittest.TestCase):
    def check(self, responses):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(3)
            requests = []

            def gateway():
                conn, _ = listener.accept()
                with conn:
                    conn.settimeout(3)
                    size, control = struct.unpack(">IH", exact(conn, 6))
                    requests.append((control, exact(conn, size)))
                    for response in responses:
                        conn.sendall(frame(1, bytes.fromhex("12f4" + response)))

            thread = threading.Thread(target=gateway, daemon=True)
            thread.start()
            result = subprocess.run([TOOL, "check", "--host", "127.0.0.1", "--port",
                                     str(listener.getsockname()[1])], capture_output=True, text=True, timeout=5)
            thread.join(2)
            self.assertEqual(requests, [(1, bytes.fromhex("f4120100"))])
            return result

    def test_positive_reply(self):
        result = self.check(["4100be3fa813"])
        self.assertEqual(result.returncode, 0)
        self.assertIn("DME replied", result.stdout)

    def test_pending_then_refusal_still_proves_the_route(self):
        result = self.check(["7f0178", "7f0112"])
        self.assertEqual(result.returncode, 0)
        self.assertIn("NRC 12", result.stdout)

    def test_no_gateway(self):
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            port = unused.getsockname()[1]
        result = subprocess.run([TOOL, "check", "--host", "127.0.0.1", "--port", str(port)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertIn("No connection", result.stdout)


if __name__ == "__main__":
    unittest.main()
