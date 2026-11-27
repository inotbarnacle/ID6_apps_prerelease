"""Verify app workers on a simulated HSFZ gateway."""
from contextlib import ExitStack
from pathlib import Path
import os
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

from test_transport import exact, frame

BUILD = Path(__file__).resolve().parents[1] / "build"
BM3_CLEAR = ("2c03f301", ["6c03f301"])


class DriveAppsTest(unittest.TestCase):
    def run_worker(self, worker, args, transactions, env=None, stream="stdout", targets=None,
                   reconnect=False):
        errors = []
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(3)

            def gateway():
                try:
                    with ExitStack() as connections:
                        conn = connections.enter_context(listener.accept()[0])
                        conn.settimeout(3)
                        for index, (request, responses) in enumerate(transactions):
                            ecu = targets[index] if targets else "12"
                            try:
                                header = exact(conn, 6)
                            except EOFError:
                                if not reconnect:
                                    raise
                                conn = connections.enter_context(listener.accept()[0])
                                conn.settimeout(3)
                                header = exact(conn, 6)
                            size, control = struct.unpack(">IH", header)
                            self.assertEqual(control, 1)
                            self.assertEqual(exact(conn, size), bytes.fromhex("f4"+ecu+request))
                            for response in responses:
                                conn.sendall(frame(1, bytes.fromhex(ecu+"f4"+response)))
                        if transactions and not transactions[-1][1]:
                            conn.shutdown(socket.SHUT_WR)
                        self.assertEqual(conn.recv(1), b"", "Unexpected repeat or extra command")
                except Exception as error:
                    errors.append(error)

            thread = threading.Thread(target=gateway)
            thread.start()
            result = subprocess.run(
                [BUILD/worker, "--host", "127.0.0.1", *args, "--port", str(listener.getsockname()[1])],
                capture_output=True, text=True, check=True, timeout=6, env=env)
            thread.join(4)
            self.assertFalse(thread.is_alive())
            if errors:
                raise errors[0]
            return getattr(result, stream)

    def test_map_two_has_only_one_request_and_requires_matching_echo(self):
        result = self.run_worker("mhd-maps", ["--map", "2"],
                                 [("2f0101", ["7f2f78", "6f0100", "6f0101"])])
        self.assertEqual(result, "Accepted by DME: Map 2\n")

    def test_wrong_echo_does_not_confirm_and_refusal_is_reported(self):
        result = self.run_worker("mhd-maps", ["--map", "1"], [("2f0100", ["6f0101"])])
        self.assertIn("Map result unknown", result)
        result = self.run_worker("mhd-maps", ["--map", "1"], [("2f0100", ["7f2f31"])])
        self.assertIn("refused by DME (NRC 31)", result)

    def test_maps_three_and_four_use_their_own_slot_bytes(self):
        for slot, index in ((1, "00"), (3, "02"), (4, "03")):
            with self.subTest(slot=slot):
                result = self.run_worker("mhd-maps", ["--map", str(slot)],
                                         [("2f01"+index, ["6f01"+index])])
                self.assertEqual(result, f"Accepted by DME: Map {slot}\n")

    def test_read_fault_codes_can_trace_raw_response(self):
        response=[("1902af", ["5902ff0000022300000123"])]
        trace=self.run_worker("engine-codes", ["--codes","--trace"], response, stream="stderr")
        self.assertIn("DME 12 19 02 AF reply: 59 02 FF 00 00 02 23 00 00 01 23",trace)
        result=self.run_worker("engine-codes", ["--codes","--trace"], response)
        self.assertEqual(result,"Engine: 2 codes\n000002  status 23\n000001  status 23\n")

    def test_clear_fault_memory_rechecks_before_restoring_session(self):
        result=self.run_worker("engine-codes", ["--clear","--confirm-clear"], [
            ("14ffffff", ["7f147f"]), ("1003", ["5003003201f4"]),
            ("14ffffff", ["7f1478", "54"]),
            ("1902af", ["5902ff1234562f"]), ("1001", ["5001003201f4"]),
        ])
        self.assertEqual(result,"Clear accepted; 1 engine code remains\n123456  status 2F\n")

    def test_clear_fault_memory_rechecks_after_busy_without_duplicate_write(self):
        result=self.run_worker("engine-codes", ["--clear","--confirm-clear"], [
            ("14ffffff", ["7f1421"]), ("14ffffff", ["54"]),
            ("1902af", ["7f1921"]), ("1902af", ["5902ff"]),
        ])
        self.assertEqual(result,"Clear accepted; 0 engine codes remain\n")

    def test_clear_fault_memory_does_not_retry_an_unconfirmed_write(self):
        result=self.run_worker("engine-codes", ["--clear","--confirm-clear"], [("14ffffff", [])])
        self.assertEqual(result,"Clear unconfirmed. Read engine codes before retrying.\n")

    def test_fault_reads_default_to_read_only_and_preserve_supported_status(self):
        result=self.run_worker("engine-codes", ["--status-mask"], [
            ("1902af", ["59020802ff120900000101"]),
        ])
        self.assertEqual(result,"Engine: 1 code\n02FF12  status 09 mask 08\n")

    def test_fault_status_unavailable_is_not_reported_as_no_codes(self):
        for mask in ("00","10","40","50"):
            with self.subTest(mask=mask):
                result=self.run_worker("engine-codes", ["--codes","--status-mask"], [
                    ("1902af", ["5902"+mask]),
                ])
                self.assertEqual(result,"Requested fault-status flags unavailable\n")

    def test_fault_metadata_rejects_incomplete_records_and_retains_leading_zero_codes(self):
        result=self.run_worker("engine-codes", ["--codes","--status-mask"], [
            ("1902af", ["5902af0000012000000204"]),
        ])
        self.assertEqual(result,"Engine: 2 codes\n000001  status 20 mask AF\n000002  status 04 mask AF\n")
        result=self.run_worker("engine-codes", ["--codes","--status-mask"], [
            ("1902af", ["5902af0000012000"]),
        ])
        self.assertEqual(result,"No valid code response\n")

    def test_fault_read_refusal_and_lost_reply_never_attempt_clear_or_session_changes(self):
        for reply,expected in ((["7f1912"],"Read rejected (NRC 12)\n"),
                               (["7f1933"],"Read rejected (NRC 33)\n"),
                               ([],"No valid code response\n")):
            with self.subTest(reply=reply):
                result=self.run_worker("engine-codes", ["--codes","--status-mask"], [("1902af",reply)])
                self.assertEqual(result,expected)

    def test_clear_without_confirmation_never_opens_the_gateway(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1",0))
            listener.listen()
            listener.settimeout(.05)
            for flags in (("--clear",),("--confirm-clear",),("--codes","--confirm-clear")):
                with self.subTest(flags=flags):
                    result=subprocess.run([BUILD/"engine-codes","--host","127.0.0.1",
                                           "--port",str(listener.getsockname()[1]),*flags],
                                          capture_output=True,text=True,timeout=2)
                    self.assertEqual(result.returncode,2)
                    self.assertIn("--clear --confirm-clear",result.stderr)
                    with self.assertRaises(socket.timeout):
                        listener.accept()

    def test_warmdeck_reads_each_temperature_independently(self):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
            listener.bind(("127.0.0.1",42862))
            listener.settimeout(2)
            self.run_worker("warmdeck-collector", ["--cycles","1"], [
                ("015c", ["415c7d"]), ("0105", ["7f0112"]), ("22da12", ["62da120058"]),
            ], targets=["12","12","18"])
            oil, coolant, trans, oil_c, _, trans_c=struct.unpack("=QQQddd", listener.recv(64))
            self.assertGreater(oil,0)
            self.assertEqual(coolant,0)
            self.assertGreaterEqual(trans,oil)
            self.assertEqual((oil_c,trans_c),(85.0,40.0))

    def test_shift_worker_decodes_live_rpm_and_speed(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.settimeout(2)
            self.run_worker("shift-deck", ["--cycles", "1", "--udp-port", str(listener.getsockname()[1])],
                            [("010c", ["410c61a8"]), ("010d", ["410d64"])])
            sampled_ms, rpm, kph = struct.unpack("=Qii", listener.recv(64))
            self.assertGreater(sampled_ms, 0)
            self.assertEqual((rpm,kph), (6250,100))


    def test_shift_worker_shares_rpm_with_hud(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as hud, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as display:
            hud.bind(("127.0.0.1", 42870))
            hud.settimeout(2)
            display.bind(("127.0.0.1", 0))
            self.run_worker("shift-deck", ["--cycles", "1", "--udp-port", str(display.getsockname()[1])],
                            [("010c", ["410c61a8"]), ("010d", ["410d64"])])
            data, peer = hud.recvfrom(64)
            timestamp, source, flags, rpm, boost = struct.unpack("=QIIIi", data)
            self.assertGreater(timestamp, 0)
            self.assertEqual(peer, ("127.0.0.1", 42872))
            self.assertEqual((source, flags, rpm, boost), (42872, 1, 6250, 0))

    def test_dashboard_requests_most_from_hud_before_fallback(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as hud, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as display:
            hud.bind(("127.0.0.1", 42874))
            hud.settimeout(2)
            display.bind(("127.0.0.1", 0))
            display.settimeout(0.25)
            proc = self.start_most("shift-deck", display.getsockname()[1])
            try:
                self.assertEqual(hud.recv(8), b"\x01")
                with self.assertRaises(socket.timeout):
                    display.recv(64)
                hud.close()
                display.settimeout(6)
                self.assertEqual(struct.unpack("=Qii", display.recv(64))[1], 1726)
            finally:
                codes, errors = self.stop_all([proc])
            self.assertEqual(codes, [0], errors)

    def test_launch_worker_samples_speed_without_an_rpm_query(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.settimeout(2)
            self.run_worker("shift-deck",
                ["--speed-only", "--cycles", "1", "--udp-port", str(listener.getsockname()[1])],
                [("010d", ["410d61"])])
            sampled_ms, rpm, speed = struct.unpack("=Qii", listener.recv(64))
            self.assertGreater(sampled_ms, 0)
            self.assertEqual((rpm, speed), (-1, 97))

    def test_bm3_current_map_uses_direct_read_when_dynamic_did_is_unsupported(self):
        marker=bytearray(255)
        marker[:4]=b"BM3_"
        tag=b"~|~___#F4C9L8G05ABCDE\x00"
        marker[84:84+len(tag)]=tag
        result=self.run_worker("bootmod3",["--current-map","0"],[
            BM3_CLEAR,
            ("2c02f30124807ffe5000ff",["7f2c31"]),
            ("2314807ffe50ff",["63"+marker.hex()]),
            ("23145003800601",["6302"]),
        ])
        self.assertEqual(result,"Current map: 3\n")

    def test_bm3_reads_actual_map_after_write_acceptance_or_refusal(self):
        marker=b"BM3_R #011000".ljust(255,b"\x00")
        identify=[BM3_CLEAR,("2c02f30124807ffe5000ff",["6c02f301"]),("22f301",["62f301"+marker.hex()])]
        for write_reply, map_byte, expected in (
            ("7d", "01", "Map 2 verified by DME readback.\n"),
            ("7d", "00", "Map 2 change unconfirmed. Current map: 1.\n"),
            ("7f3d31", "00", "Map 2 change unconfirmed. Current map: 1.\n"),
        ):
            with self.subTest(write_reply=write_reply,map_byte=map_byte):
                result=self.run_worker("bootmod3",["--map","2"],identify+[
                    ("3d14d001d006020101",[write_reply]),("2314d001d00601",["63"+map_byte])
                ])
                self.assertEqual(result,expected)

    def test_bm3_security_refusal_stops_identification(self):
        result=self.run_worker("bootmod3",["--current-map","0"],[
            BM3_CLEAR,
            ("2c02f30124807ffe5000ff",["7f2c33"]),
        ])
        self.assertIn("identification refused (NRC 33)",result)

    def test_bm3_s58_binary_marker_and_invalid_slot(self):
        marker=bytearray(255)
        marker[:4]=b"BM3_"
        tag=b"#F4C9L8R02GGGJM_G87_RWD_USA_\x00"
        marker[101:101+len(tag)]=tag
        result=self.run_worker("bootmod3",["--current-map","0"],[
            BM3_CLEAR,
            ("2c02f30124807ffe5000ff",["6c02f301"]),
            ("22f301",["62f301"+marker.hex()]),
            ("23146003800601",["6304"]),
        ])
        self.assertEqual(result,"Current map unavailable.\n")

    def test_bm3_clears_accumulated_marker_before_trying_the_next_location(self):
        marker=b"BM3_R #011000".ljust(255,b"\x00")
        result=self.run_worker("bootmod3",["--current-map","0"],[
            BM3_CLEAR, ("2c02f30124807ffe5000ff",["6c02f301"]),
            ("22f301",["62f301"+b"BM3_R #UNKNOWN".ljust(255,b"\x00").hex()]),
            BM3_CLEAR, ("2c02f301240977fe4c00ff",["6c02f301"]),
            ("22f301",["62f301"+marker.hex()]),
            ("2314d001d00601",["6301"]),
        ])
        self.assertEqual(result,"Current map: 2\n")

    def test_bm3_fixed_commands_skip_marker_discovery(self):
        for mode,value,request,reply in (
            ("--sound","1","2e5ffd00","6e5ffd"),
            ("--flap","2","2f60fd032328","6f60fd03"),
        ):
            result=self.run_worker("bootmod3",[mode,value],[(request,[reply])])
            self.assertIn("accepted by DME",result)

    def test_bm3_burble_preset_uses_automatically_identified_rom(self):
        marker=b"BM3_R #011000".ljust(255,b"\x00")
        timing="f0"*24
        result=self.run_worker("bootmod3",["--burbles","2"],[
            BM3_CLEAR,
            ("2c02f30124807ffe5000ff",["7f2c7e"]),
            ("2314807ffe50ff",["63"+marker.hex()]),
            ("3d14d001d11618"+timing,["7d"]),
            ("3d14d001d0990101",["7d"]),
            ("2314d001d11618",["63"+timing]),
            ("2314d001d09901",["6301"]),
        ])
        self.assertEqual(result,"Burble preset 8 verified by DME readback.\n")

    def test_mgflasher_sends_all_three_commands_without_tune_or_profile_check(self):
        with tempfile.TemporaryDirectory() as directory:
            missing=Path(directory)/"no-profile.txt"
            for mode, value, did, frame, display in (
                ("--map","2","6945","02b0","Map slot 2"),
                ("--burbles","5","6946","05b4","Burble slot 5"),
                ("--cooling","1","6947","01b1","Max Cooling On"),
            ):
                with self.subTest(mode=mode):
                    result=self.run_worker("mgflasher",["--profile",str(missing),mode,value],[
                        ("2e"+did+frame,["6e"+did]),
                    ])
                    self.assertEqual(result,"Acknowledged by DME: "+display+"\n")
            result=self.run_worker("mgflasher",["--profile",str(missing),"--map","2"],[
                ("2e694502b0",["7f2e31"]),
            ])
            self.assertIn("refused (NRC 31)",result)

    def test_mgflasher_optional_locators_verify_echo_and_active_slot(self):
        with tempfile.TemporaryDirectory() as directory:
            profile=Path(directory)/"readback.txt"
            for config in (
                "MGF1 2 LE 1000 aabbccdd\n1010 1 1012 1f\n0 0 0 0\n0 0 0 0\n",
                "MGF2 2 LE\n1010 1 1012\n0 0 0\n0 0 0\n",
            ):
                profile.write_text(config)
                args=["--profile",str(profile),"--map","2"]
                write=("2e694502b0",["6e6945"])
                echo=("2324000010120002",["634569"])
                active=("2324000010100001",["6302"])
                self.assertEqual(self.run_worker("mgflasher",args,[write,echo,active]),
                                 "Verified: Map slot 2\n")
                self.assertIn("unconfirmed",self.run_worker("mgflasher",args,[
                    write,echo,("2324000010100001",["6301"]),
                ]))

    def test_hybriddeck_sme_obd_and_battery_health_values(self):
        for health, expected_health in (
            ([("226335",["6263355501005e"])],94),
            ([("226335",["7f2231"]),("22dd7b",["62dd7b61"])],97),
            ([("226335",["626335550100ff"]),("22dd7b",["62dd7bff"])],None),
        ):
            with self.subTest(health=health), socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
                listener.bind(("127.0.0.1",42865))
                listener.settimeout(2)
                self.run_worker("hybriddeck-collector",["--cycles","7"],[
                    ("22ddbc",["62ddbc0280"]),
                    ("22ddb4",["7f2231"]), ("22dd68",["62dd688f20"]),
                    ("22dd69",["62dd69fffff63c"]),
                    ("22dd6c",["62dd6c09c4"]),
                    ("0105",["41057a"]), ("0142",["414236b0"]),
                ]+health,targets=["07","07","07","07","07","12","12"]+["07"]*len(health),
                    reconnect=True)
                for _ in range(7):
                    data=listener.recv(128)
                samples=struct.unpack("=7Q7d",data)
                self.assertTrue(all(samples[:6]))
                for actual, expected in zip(samples[7:13],(64,366.4,-25,25,82,14)):
                    self.assertAlmostEqual(actual,expected)
                if expected_health is None:
                    self.assertEqual(samples[6],0)
                else:
                    self.assertGreater(samples[6],0)
                    self.assertEqual(samples[13],expected_health)

    def test_xdrive_status_identifies_unknown_modules_read_only(self):
        result=self.run_worker("xdrive-control",["--status","0"],[
            ("22f150",["62f1500f2500"]), ("22f150",["62f1500f2180"]),
            ("22f150",["62f1500f2520"])],targets=["29","19","39"])
        self.assertEqual(result,"Unsupported DSC 0F2500. Transfer case 0F2180. VIP 0F2520.\n")

    def test_dsc25_switch_requires_park_and_verified_lmv(self):
        ident = [("22f150", ["62f1500f1860"]), ("22dbe1", ["62dbe101"])]
        result = self.run_worker("xdrive-control", ["--drive-mode", "0"],
            ident + [("22f150", ["62f1500f1400"])], targets=["29", "29", "19"])
        self.assertIn("requires a verified LMV", result)

        current = ident + [("22f150", ["62f1500f1480"]), ("224002", ["62400209c4200f"])]
        targets = ["29", "29", "19", "19", "18"]
        # Measured on a DSC_25 car: DA2E byte 2 is 01 in Park with the engine running, 0a in R/N/D,
        # and 06 only in Park with the engine off.
        for gear in ("0005", "000a", "0006"):
            result = self.run_worker("xdrive-control", ["--drive-mode", "0"],
                current + [("22da2e", ["62da2e" + gear])], targets=targets)
            self.assertIn("confirmed Park", result)

        result = self.run_worker("xdrive-control", ["--drive-mode", "0"],
            current + [("22da2e", ["62da2e0001"]), ("010c", ["410c0c80"]), ("010d", ["410d00"]),
                       ("2edbe100", ["6edbe1"]), ("224002", ["62400209c4280f"])],
            targets=targets + ["12", "12", "29", "19"])
        self.assertEqual(result, "Transfer case confirms RWD.\n")

    def test_shift_worker_waits_for_a_busy_gateway(self):
        with socket.socket() as gateway, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as lock:
            gateway.bind(("127.0.0.1", 0))
            gateway.listen()
            gateway.settimeout(0.2)
            lock.bind(("127.0.0.1", 42861))
            with subprocess.Popen(
                [BUILD/"shift-deck", "--host", "127.0.0.1",
                 "--port", str(gateway.getsockname()[1]), "--cycles", "60"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            ) as worker:
                try:
                    with self.assertRaises(TimeoutError):
                        gateway.accept()
                    self.assertIsNone(worker.poll())
                    lock.close()
                    gateway.settimeout(2)
                    conn, _ = gateway.accept()
                    with conn:
                        conn.settimeout(2)
                        length, ctrl = struct.unpack(">IH", exact(conn, 6))
                        self.assertEqual(ctrl, 1)
                        self.assertEqual(exact(conn, length), bytes.fromhex("f412010c"))
                finally:
                    if worker.poll() is None:
                        worker.terminate()

    @staticmethod
    def closed_port():
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]

    @staticmethod
    def most_env():
        return dict(os.environ, ID6_FAKE_MOST="up")

    def test_shiftdeck_most_fallback(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(5)
            r = subprocess.run(
                [BUILD/"shift-deck", "--host", "127.0.0.1", "--port", str(self.closed_port()),
                 "--udp-port", str(udp.getsockname()[1]), "--cycles", "120"],
                capture_output=True, text=True, timeout=20, env=self.most_env())
            _, rpm, kph = struct.unpack("=Qii", udp.recv(64))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual((rpm, kph), ((0x1A*256+0xF8)//4, 60))
        self.assertIn("transport: MOST", r.stderr)

    def test_shiftdeck_hsfz_never_binds(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.settimeout(2)
            err = self.run_worker("shift-deck", ["--cycles", "1", "--udp-port", str(listener.getsockname()[1])],
                                  [("010c", ["410c61a8"]), ("010d", ["410d64"])], env=self.most_env(), stream="stderr")
        self.assertNotIn("FAKE-MOST bind", err)

    def test_mhd_hsfz_never_binds(self):
        err = self.run_worker("mhd-maps", ["--map", "2"], [("2f0101", ["6f0101"])],
                              env=self.most_env(), stream="stderr")
        self.assertNotIn("FAKE-MOST bind", err)

    def start_most(self, worker, udp_port, extra=(), env=None):
        return subprocess.Popen([BUILD/worker, *extra, "--transport", "most", "--udp-port", str(udp_port)],
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                                env=env or self.most_env())

    @staticmethod
    def stop_all(procs):
        for p in procs:
            if p.poll() is None:
                p.send_signal(signal.SIGTERM)
        codes = []
        for p in procs:
            try:
                codes.append(p.wait(5))
            except subprocess.TimeoutExpired:
                p.kill()
                codes.append("hung")
        return codes, [p.communicate()[1] for p in procs]

    def shift_frames(self, udp, seconds):
        count, end = 0, time.monotonic()+seconds
        while time.monotonic() < end:
            try:
                udp.recv(64)
                count += 1
            except socket.timeout:
                pass
        return count

    def test_shiftdeck_keeps_a_fair_share_of_a_slow_tunnel(self):
        env = dict(self.most_env(), ID6_FAKE_MOST_DELAY_MS="80")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as us, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as ub:
            for u in (us, ub):
                u.bind(("127.0.0.1", 0))
                u.settimeout(0.2)
            shift = self.start_most("shift-deck", us.getsockname()[1], env=env)
            procs = [shift]
            try:
                self.shift_frames(us, 1)
                alone = self.shift_frames(us, 5)
                procs.append(self.start_most("boost-deck", ub.getsockname()[1], ["--session"], env=env))
                self.shift_frames(us, 1)
                shared = self.shift_frames(us, 5)
                self.assertIsNone(procs[1].poll())
            finally:
                codes, errs = self.stop_all(procs)
        self.assertEqual(codes, [0]*len(procs), errs)
        self.assertGreater(alone, 15)
        self.assertGreaterEqual(shared, 0.4*alone, (alone, shared))

    def test_shiftdeck_sigterm_on_most(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            udp.settimeout(3)
            proc = self.start_most("shift-deck", udp.getsockname()[1])
            try:
                udp.recv(64)
                start = time.monotonic()
                proc.send_signal(signal.SIGTERM)
                self.assertEqual(proc.wait(5), 0)
                self.assertLess(time.monotonic()-start, 3)
            finally:
                if proc.poll() is None:
                    proc.kill()
                err = proc.communicate()[1]
        self.assertIn("FAKE-MOST exit unregister_target=1 unregister_client=1 free=1", err)

    def test_shiftdeck_and_boostdeck_share_most(self):
        busy = []
        stop = threading.Event()

        def probe():
            while not stop.is_set():
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                    try:
                        s.bind(("127.0.0.1", 42861))
                    except OSError:
                        busy.append(1)
                time.sleep(0.001)

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as us, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as ub:
            for u in (us, ub):
                u.bind(("127.0.0.1", 0))
                u.settimeout(0.5)
            shift = self.start_most("shift-deck", us.getsockname()[1])
            boost = self.start_most("boost-deck", ub.getsockname()[1], ["--session"])
            prober = threading.Thread(target=probe)
            prober.start()
            shift_ok = boost_ok = 0
            try:
                end = time.monotonic()+5
                while time.monotonic() < end:
                    for u, name in ((us, "s"), (ub, "b")):
                        try:
                            data = u.recv(512)
                        except socket.timeout:
                            continue
                        if name == "s":
                            shift_ok += struct.unpack("=Qii", data[:16])[1] == 1726
                        else:
                            boost_ok += int(data.decode().split()[3]) != 0
                    self.assertIsNone(shift.poll())
                    self.assertIsNone(boost.poll())
            finally:
                stop.set()
                prober.join()
                for p in (shift, boost):
                    if p.poll() is None:
                        p.send_signal(signal.SIGTERM)
                codes = []
                for p in (shift, boost):
                    try:
                        codes.append(p.wait(3))
                    except subprocess.TimeoutExpired:
                        p.kill()
                        codes.append("hung")
                errs = [p.communicate()[1] for p in (shift, boost)]
            self.assertEqual(codes, [0, 0], errs)
            self.assertGreater(shift_ok, 3)
            self.assertGreater(boost_ok, 3)
            self.assertTrue(busy, "the lease was never seen held")
            for err in errs:
                self.assertNotIn("deadlock", err.lower())


if __name__ == "__main__":
    unittest.main()
