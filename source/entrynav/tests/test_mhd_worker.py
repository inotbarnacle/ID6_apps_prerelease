"""The mhd-maps worker against a simulated DME that answers MHD's 2F requests with an echo, like the MHD case of
cauliflowermossgrape's test_live_adjustments.js. Checks each request's bytes, that a press counts only on the DME's echo,
and that Max Cooling goes on only with the engine running."""
from pathlib import Path
import socket
import struct
import subprocess
import threading
import unittest

from test_transport import exact, frame

WORKER = Path(__file__).resolve().parents[1] / "build" / "mhd-maps"


class FakeDme:
    """Answers 2F with its echo, as an MHD+ tune does, and OBD's engine speed (01 0C). replies: the answers to give a
    request, by its hex, instead. rpm: engine speed, or None for no answer."""

    def __init__(self, rpm=800, replies=None):
        self.rpm, self.replies, self.requests = rpm, replies or {}, []

    def answer(self, uds):
        self.requests.append(uds.hex())
        if uds.hex() in self.replies:
            return [bytes.fromhex(reply) for reply in self.replies[uds.hex()]]
        if uds[0] == 0x2F:
            return [bytes([0x6F]) + uds[1:]]
        if uds.hex() == "010c":
            return [] if self.rpm is None else [bytes.fromhex("410c") + (self.rpm * 4).to_bytes(2, "big")]
        raise AssertionError(f"unexpected request {uds.hex()}")


class MhdWorkerTest(unittest.TestCase):
    def run_worker(self, dme, *args):
        """Runs mhd-maps against dme behind a gateway on 127.0.0.1, which serves each connection in turn."""
        errors, done = [], threading.Event()
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(0.2)

            def gateway():
                try:
                    while not done.is_set():
                        try:
                            conn, _ = listener.accept()
                        except socket.timeout:
                            continue
                        with conn:
                            conn.settimeout(8)
                            while True:
                                header = conn.recv(6, socket.MSG_WAITALL)
                                if not header:
                                    break
                                size, control = struct.unpack(">IH", header)
                                body = exact(conn, size)
                                self.assertEqual((control, body[:2]), (1, b"\xf4\x12"))
                                for reply in dme.answer(body[2:]):
                                    conn.sendall(frame(1, b"\x12\xf4" + reply))
                except Exception as error:  # noqa: BLE001
                    errors.append(error)

            thread = threading.Thread(target=gateway)
            thread.start()
            try:
                result = subprocess.run([WORKER, "--host", "127.0.0.1", "--port", str(listener.getsockname()[1]), *args],
                                        capture_output=True, text=True, timeout=30)
            finally:
                done.set()
                thread.join(10)
            if errors:
                raise errors[0]
            return result.returncode, result.stdout.strip()

    def test_each_map_is_its_slot_byte_and_counts_on_the_echo(self):
        for slot in range(1, 5):
            with self.subTest(slot=slot):
                dme = FakeDme()
                self.assertEqual(self.run_worker(dme, "--map", str(slot)), (0, f"Map {slot} accepted by the DME"))
                self.assertEqual(dme.requests, [f"2f010{slot - 1}"])  # one request, and nothing read first

    def test_a_pending_answer_then_the_echo_is_accepted(self):
        dme = FakeDme(replies={"2f0101": ["7f2f78", "6f0101"]})
        self.assertEqual(self.run_worker(dme, "--map", "2"), (0, "Map 2 accepted by the DME"))

    def test_another_map_echoed_is_not_accepted(self):
        dme = FakeDme(replies={"2f0101": ["6f0100"]})
        self.assertEqual(self.run_worker(dme, "--map", "2"), (1, "No answer from the DME for map 2"))

    def test_a_dme_without_mhd_refuses(self):
        dme = FakeDme(replies={"2f0100": ["7f2f31"]})
        self.assertEqual(self.run_worker(dme, "--map", "1"),
                         (1, "The DME refused map 1 (NRC 31). Is an MHD+ tune flashed?"))
        dme = FakeDme(replies={"2f0100": ["7f2f22"]})
        self.assertEqual(self.run_worker(dme, "--map", "1"), (1, "The DME refused map 1 (NRC 22)"))

    def test_max_cooling_on_reads_the_engine_speed_first(self):
        dme = FakeDme(rpm=750)
        self.assertEqual(self.run_worker(dme, "--cooling", "1"), (0, "Max Cooling on, accepted by the DME"))
        self.assertEqual(dme.requests, ["010c", "2f0301"])

    def test_max_cooling_on_needs_the_engine_running(self):
        for rpm in (0, 500, None):
            with self.subTest(rpm=rpm):
                dme = FakeDme(rpm=rpm)
                self.assertEqual(self.run_worker(dme, "--cooling", "1"),
                                 (1, "Start the engine first. Max Cooling needs it running."))
                self.assertEqual(dme.requests, ["010c"])  # no 2F

    def test_max_cooling_off_is_always_sent(self):
        dme = FakeDme(rpm=0)
        self.assertEqual(self.run_worker(dme, "--cooling", "0"), (0, "Max Cooling off, accepted by the DME"))
        self.assertEqual(dme.requests, ["2f0300"])

    def test_bad_arguments_send_nothing(self):
        for args in ((), ("--map", "5"), ("--map", "0"), ("--cooling", "2"), ("--map", "1", "--cooling", "0")):
            with self.subTest(args=args):
                dme = FakeDme()
                self.assertEqual(self.run_worker(dme, *args)[0], 2)
                self.assertEqual(dme.requests, [])


if __name__ == "__main__":
    unittest.main()
