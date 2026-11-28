"""The bm3-maps worker against a simulated DME with bootmod3's map slot, like cauliflowermossgrape's test_bm3_protocol.js
fixture. Checks that nothing is written unless the marker names a known ROM, and that a map change is read back."""
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import unittest

from test_transport import exact, frame

WORKER = Path(__file__).resolve().parents[1] / "build" / "bm3-maps"
MARKER = 0x807FFE50
MEVD17_MARKER = 0x0977FE4C  # where an F20 M140i keeps it: bootmod3's second address
SLOT = 0x4005E006  # R1C900000's map slot (bm3_profiles.h)
BURBLES = 0x4005E099  # its burble switch
TIMING = 0x4005E100  # and its 24-byte burble timing table, factor 2


def legacy_marker(rom):
    return (b"BM3_R#" + rom.encode() + b"#").ljust(255, b"\0")


def mevd17_marker(token):
    """The F-series layout read on that car, with synthetic bytes for its 64-byte block."""
    block = bytes((i * 37 + 11) & 0xFF for i in range(64)).replace(b"#", b"$")
    body = b"#DSTBM3_" + block + b"~|~____#EPST:R1C9J8B3B______#" + token.encode() + b"\0"
    return body.ljust(255, b"\xff")


class FakeDme:
    """Answers 2C, 22, 23, 3E/3D and 2E like a bootmod3 DME. refuse: SIDs answered 7F xx 31. silent: SIDs not
    answered."""

    def __init__(self, marker, slot=0, refuse=(), silent=(), stuck=False, at=MARKER):
        self.memory = {at: marker, SLOT: bytes([slot, 1]), BURBLES: b"\0", TIMING: bytes(24)}
        self.refuse, self.silent, self.stuck = set(refuse), set(silent), stuck
        self.requests, self.entries, self.defined = [], 0, None

    def answer(self, uds):
        sid = uds[0]
        self.requests.append(uds.hex())
        if sid in self.silent:
            return []
        if sid in self.refuse:
            return [bytes([0x7F, sid, 0x31])]
        if sid == 0x2C:
            if uds[1] == 2:
                self.defined = int.from_bytes(uds[5:9], "big")
                if self.defined not in self.memory:  # as the car refused bootmod3's other addresses
                    return [bytes([0x7F, sid, 0x33])]
            self.entries = 0 if uds[1] == 3 else self.entries + 1
            return [bytes([0x6C]) + uds[1:4]]
        if sid == 0x22:
            return [bytes.fromhex("62f301") + self.memory[self.defined] * self.entries]
        if sid == 0x23:
            address, length = int.from_bytes(uds[2:6], "big"), uds[6]
            if address not in self.memory:
                return [bytes([0x7F, sid, 0x72])]
            return [bytes([0x63]) + self.memory[address][:length]]
        if sid in (0x3D, 0x3E):
            address, length = int.from_bytes(uds[2:6], "big"), uds[6]
            if len(uds) != 7 + length:
                raise AssertionError(f"write length {length} with {len(uds) - 7} bytes")
            if not self.stuck:
                self.memory[address] = uds[7:]
            # "pending" first, as a DME busy writing would answer, then the positive reply
            return [bytes([0x7F, sid, 0x78]), bytes([0x7E, 0x3D]) if sid == 0x3E else bytes([0x7D])]
        if sid == 0x2E:  # Active Sound
            return [bytes([0x6E]) + uds[1:3]]
        raise AssertionError(f"unexpected service {sid:02x}")


class Bm3WorkerTest(unittest.TestCase):
    def run_worker(self, dme, *args, marker=MARKER, host="127.0.0.1"):
        """Runs bm3-maps against dme behind a gateway on 127.0.0.1, which serves each connection in turn."""
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
            chosen = ["--marker", f"{marker:08X}"] if marker is not None else []
            try:
                result = subprocess.run([WORKER, "--host", host, "--port", str(listener.getsockname()[1]),
                                         *chosen, *args], capture_output=True, text=True, timeout=30)
            finally:
                done.set()
                thread.join(10)
            if errors:
                raise errors[0]
            return result.returncode, result.stdout.strip()

    def writes(self, dme):
        return [r for r in dme.requests if r[:2] in ("3d", "3e")]

    def test_status_reads_the_rom_and_the_slot(self):
        dme = FakeDme(legacy_marker("R1C900000"), slot=1)
        self.assertEqual(self.run_worker(dme, "--status"), (0, "Map 2 active"))
        self.assertEqual(dme.requests, ["2c03f301", "2c02f30124807ffe5000ff", "22f301", "23144005e00601"])

    def test_map_change_writes_the_slot_and_reads_it_back(self):
        dme = FakeDme(legacy_marker("R1C900000"), slot=0)
        self.assertEqual(self.run_worker(dme, "--map", "3"), (0, "Map 3 active"))
        self.assertEqual(self.writes(dme), ["3e144005e006020201"])
        self.assertEqual(dme.memory[SLOT], bytes([2, 1]))

    def test_unknown_rom_writes_nothing(self):
        dme = FakeDme(legacy_marker("ZZZZZZZZZ"))
        code, line = self.run_worker(dme, "--map", "2")
        self.assertEqual(code, 1)
        self.assertIn("No known bootmod3 ROM", line)
        self.assertEqual(self.writes(dme), [])

    def test_stock_dme_writes_nothing(self):
        dme = FakeDme(b"\0" * 255, refuse={0x2C, 0x23})
        code, line = self.run_worker(dme, "--map", "1")
        self.assertEqual((code, line), (1, "The DME refused to read its memory (NRC 31)"))
        self.assertEqual(self.writes(dme), [])

    def test_refused_dynamic_did_reads_the_marker_directly(self):
        dme = FakeDme(legacy_marker("R1C900000"), slot=3, refuse={0x2C})
        self.assertEqual(self.run_worker(dme, "--status"), (0, "Map 4 active"))
        self.assertEqual(dme.requests[:2], ["2c03f301", "2314807ffe50ff"])

    def test_accumulated_marker_is_refused(self):
        dme = FakeDme(legacy_marker("R1C900000"))
        original = dme.answer

        def no_clear(uds):  # a DME that keeps earlier definitions: the DID returns two markers
            replies = original(uds)
            if uds[0] == 0x2C and uds[1] == 2:
                dme.entries = 2
            return replies
        dme.answer = no_clear
        code, line = self.run_worker(dme, "--map", "2")
        self.assertEqual((code, line), (1, "No known bootmod3 ROM: marker length"))
        self.assertEqual(self.writes(dme), [])

    def test_finds_the_mevd17_marker_at_bootmod3s_second_address(self):
        dme = FakeDme(mevd17_marker("R1C900000CEKXA"), slot=2, at=MEVD17_MARKER)
        self.assertEqual(self.run_worker(dme, "--status", marker=None), (0, "Map 3 active"))
        self.assertEqual(dme.requests, ["2c03f301", "2c02f30124807ffe5000ff", "2314807ffe50ff",
                                        "2c03f301", "2c02f301240977fe4c00ff", "22f301", "23144005e00601"])

    def test_no_marker_at_any_address_writes_nothing(self):
        dme = FakeDme(mevd17_marker("R1C900000CEKXA"), at=0x12345678)
        code, line = self.run_worker(dme, "--map", "2", marker=None)
        self.assertEqual((code, line), (1, "The DME refused to read its memory (NRC 72)"))
        defined = [r[10:18] for r in dme.requests if r.startswith("2c02")]
        self.assertEqual(defined, ["807ffe50", "0977fe4c", "801ffdac", "8005fe1e"])
        self.assertEqual(self.writes(dme), [])

    def test_a_marker_read_beats_a_refusal_in_the_message(self):
        dme = FakeDme(mevd17_marker("ZZZZZZZZZCEKXA"), at=MEVD17_MARKER)
        code, line = self.run_worker(dme, "--status", marker=None)
        self.assertEqual((code, line), (1, "No known bootmod3 ROM: ROM not uniquely matched in the binary marker"))

    def test_finds_a_gateway_that_moved_and_saves_it(self):
        """The saved address takes no connection: the worker asks lo for the gateway, as it asks vbr0 on the car."""
        identification = b"DIAGADR10BMWMAC001122334455"
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as responder, tempfile.TemporaryDirectory() as data:
            responder.bind(("0.0.0.0", 0))  # a broadcast reaches only sockets bound to the wildcard address
            responder.settimeout(5)

            def answer():
                request, sender = responder.recvfrom(64)
                if request == bytes.fromhex("000000000011"):
                    responder.sendto(struct.pack(">IH", len(identification), 0x11) + identification, sender)

            thread = threading.Thread(target=answer, daemon=True)
            thread.start()
            saved = Path(data) / "gateway-ip.txt"
            dme = FakeDme(legacy_marker("R1C900000"), slot=1)
            result = self.run_worker(dme, "--status", "--search-dev", "lo", "--search-port",
                                     str(responder.getsockname()[1]), "--gateway-file", str(saved),
                                     host="127.0.0.2")  # nothing listens there
            thread.join(5)
            self.assertEqual(result, (0, "Map 2 active"))
            self.assertEqual(saved.read_text(), "127.0.0.1\n")

    def test_map_change_not_taken_is_reported(self):
        dme = FakeDme(legacy_marker("R1C900000"), slot=0, stuck=True)
        self.assertEqual(self.run_worker(dme, "--map", "2"),
                         (1, "Map change not confirmed: the DME reports map 1"))

    def test_refused_write(self):
        dme = FakeDme(legacy_marker("R1C900000"), refuse={0x3E})
        self.assertEqual(self.run_worker(dme, "--map", "2"), (1, "The DME refused the map change (NRC 31)"))

    def identified(self, dme):
        """The requests after the ROM's identification."""
        return dme.requests[3:]

    def test_active_sound(self):
        dme = FakeDme(legacy_marker("R1C900000"))
        self.assertEqual(self.run_worker(dme, "--sound", "0"), (0, "The DME accepted: Active Sound off"))
        self.assertEqual(self.identified(dme), ["2e5ffd01"])
        dme = FakeDme(legacy_marker("R1C900000"))
        self.assertEqual(self.run_worker(dme, "--sound", "1"), (0, "The DME accepted: Active Sound on"))
        self.assertEqual(self.identified(dme), ["2e5ffd00"])

    def test_refused_active_sound(self):
        dme = FakeDme(legacy_marker("R1C900000"), refuse={0x2E})
        self.assertEqual(self.run_worker(dme, "--sound", "1"), (1, "The DME refused Active Sound on (NRC 31)"))

    def test_sound_and_burbles_need_a_known_rom(self):
        for args in (("--sound", "1"), ("--burbles", "2")):
            dme = FakeDme(legacy_marker("ZZZZZZZZZ"))
            code, line = self.run_worker(dme, *args)
            self.assertEqual(code, 1)
            self.assertIn("No known bootmod3 ROM", line)
            self.assertFalse([r for r in dme.requests if r[:2] in ("2e", "3d", "3e")])

    def test_burbles_fill_the_table_before_turning_on(self):
        dme = FakeDme(legacy_marker("R1C900000"))
        self.assertEqual(self.run_worker(dme, "--burbles", "2"), (0, "Burbles at aggression 8, read back from the DME"))
        self.assertEqual(self.writes(dme), ["3e144005e10018" + "f0" * 24, "3e144005e0990101"])
        self.assertEqual(self.identified(dme)[2:], ["23144005e10018", "23144005e09901"])
        self.assertEqual((dme.memory[TIMING], dme.memory[BURBLES]), (b"\xf0" * 24, b"\x01"))

    def test_burbles_off_turns_off_before_clearing(self):
        dme = FakeDme(legacy_marker("R1C900000"))
        dme.memory[BURBLES], dme.memory[TIMING] = b"\x01", b"\xe8" * 24
        self.assertEqual(self.run_worker(dme, "--burbles", "0"), (0, "Burbles off, read back from the DME"))
        self.assertEqual(self.writes(dme), ["3e144005e0990100", "3e144005e10018" + "00" * 24])
        self.assertEqual((dme.memory[TIMING], dme.memory[BURBLES]), (bytes(24), b"\0"))

    def test_burbles_not_taken_are_reported(self):
        dme = FakeDme(legacy_marker("R1C900000"), stuck=True)
        self.assertEqual(self.run_worker(dme, "--burbles", "3"),
                         (1, "Burble change not confirmed. Try again before driving."))

    def test_refused_burbles_stop_at_the_first_write(self):
        dme = FakeDme(legacy_marker("R1C900000"), refuse={0x3E})
        self.assertEqual(self.run_worker(dme, "--burbles", "1"), (1, "The DME refused the burble change (NRC 31)"))
        self.assertEqual(self.writes(dme), ["3e144005e10018" + "f8" * 24])

    def test_one_control_per_run(self):
        dme = FakeDme(legacy_marker("R1C900000"))
        self.assertEqual(self.run_worker(dme, "--burbles", "1", "--sound", "1")[0], 2)
        self.assertEqual(self.run_worker(dme, "--flap", "1")[0], 2)  # the flap isn't a control here
        self.assertEqual(self.run_worker(dme, "--burbles", "4")[0], 2)
        self.assertEqual(dme.requests, [])


if __name__ == "__main__":
    unittest.main()
