"""The engine-codes worker against a simulated DME: reading the fault memory, clearing it, and a DME that wants the
extended session before it clears."""
from pathlib import Path
import socket
import struct
import subprocess
import threading
import unittest

from test_transport import exact, frame

WORKER = Path(__file__).resolve().parents[1] / "build" / "engine-codes"
READ = "1902af"
CLEAR = "14ffffff"


class FakeDme:
    """Answers the worker's requests from replies: request hex -> list of answers, used in turn (the last repeats).
    An answer is the reply frames' hex, separated by spaces: "7f1478 54" is "pending", then the positive reply."""

    def __init__(self, replies):
        self.replies = {request: list(answers) for request, answers in replies.items()}
        self.requests = []

    def answer(self, uds):
        request = uds.hex()
        self.requests.append(request)
        answers = self.replies.get(request)
        if not answers:
            raise AssertionError(f"unexpected request {request}")
        reply = answers.pop(0) if len(answers) > 1 else answers[0]
        return [bytes.fromhex(part) for part in reply.split()]


class EngineCodesTest(unittest.TestCase):
    def run_worker(self, dme, *args):
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
                result = subprocess.run([WORKER, "--host", "127.0.0.1", "--port", str(listener.getsockname()[1]),
                                         *args], capture_output=True, text=True, timeout=30)
            finally:
                done.set()
                thread.join(10)
            if errors:
                raise errors[0]
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout.splitlines()

    def test_reads_only_codes_with_a_fault_bit(self):
        # Availability mask FF: 0x2F reports, 0x50 (test not completed, not failed) does not.
        dme = FakeDme({READ: ["5902ff" "1a2f0f2f" "22222250" "30fe0108"]})
        self.assertEqual(self.run_worker(dme, "--codes"),
                         ["Engine: 2 codes", "1A2F0F  status 2F", "30FE01  status 08"])
        self.assertEqual(dme.requests, [READ])

    def test_no_codes(self):
        dme = FakeDme({READ: ["5902ff"]})
        self.assertEqual(self.run_worker(dme, "--codes"), ["Engine: 0 codes"])

    def test_clear_then_reads_again(self):
        dme = FakeDme({CLEAR: ["7f1478 54"], READ: ["5902ff"]})
        self.assertEqual(self.run_worker(dme, "--clear"), ["Engine: 0 codes after clear"])
        self.assertEqual(dme.requests, [CLEAR, READ])

    def test_clear_in_the_extended_session(self):
        dme = FakeDme({CLEAR: ["7f1422", "54"], "1003": ["5003001e01f4"], "1001": ["5001001e01f4"],
                       READ: ["5902ff"]})
        self.assertEqual(self.run_worker(dme, "--clear"), ["Engine: 0 codes after clear"])
        self.assertEqual(dme.requests, [CLEAR, "1003", CLEAR, "1001", READ])

    def test_clear_refused_says_what_to_try(self):
        dme = FakeDme({CLEAR: ["7f1422"], "1003": ["7f1012"]})
        self.assertEqual(self.run_worker(dme, "--clear"),
                         ["Clear refused (NRC 22). Try with the ignition on and the engine off."])
        self.assertEqual(dme.requests, [CLEAR, "1003"])  # no codes read, no second clear

    def test_clear_rejected_for_another_reason(self):
        dme = FakeDme({CLEAR: ["7f1413"]})
        self.assertEqual(self.run_worker(dme, "--clear"), ["Clear rejected (NRC 13)"])
        self.assertEqual(dme.requests, [CLEAR])

    def test_no_gateway(self):
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            port = unused.getsockname()[1]  # closed again before the worker connects
        result = subprocess.run([WORKER, "--host", "127.0.0.1", "--port", str(port), "--codes",
                                 "--search-dev", "lo", "--search-port", "1"], capture_output=True, text=True,
                                timeout=30)
        self.assertEqual((result.returncode, result.stdout), (0, "Diagnostic gateway unavailable\n"))


if __name__ == "__main__":
    unittest.main()
