"""Synthetic standard OBD CAN-counted replies; no vehicle captures or commands."""
import socket
import subprocess
import unittest

import test_drive_apps as apps
import test_oneshot_most as most

MODES = (("03", "confirmed", "Confirmed"), ("07", "pending", "Pending"), ("0a", "permanent", "Permanent"))


class ObdFaultsTest(unittest.TestCase):
    run_worker = apps.DriveAppsTest.run_worker

    def test_each_category_uses_one_read_and_preserves_its_protocol(self):
        for service, flag, state in MODES:
            with self.subTest(category=flag):
                positive = f"{int(service, 16) + 0x40:02x}"
                result = self.run_worker("engine-codes", [f"--obd-{flag}"], [(service, [positive + "020133d016"])])
                self.assertEqual(result, f"OBD {state} emissions: 2 codes\nOBD{service.upper()} P0133\nOBD{service.upper()} U1016\n")

    def test_empty_category_is_only_a_result_for_that_category(self):
        for service, flag, state in MODES:
            with self.subTest(category=flag):
                response = f"{int(service, 16) + 0x40:02x}00"
                result = self.run_worker("engine-codes", [f"--obd-{flag}"], [(service, [response])])
                self.assertEqual(result, f"OBD {state} emissions: 0 codes\n")

    def test_invalid_or_lost_responses_never_become_a_clean_memory_result(self):
        for response in ([], ["4302"], ["430101"], ["43010000"], ["43000133"],
                         ["430101330001"], ["43013300000000"], ["5902af01330008"]):
            with self.subTest(response=response):
                result = self.run_worker("engine-codes", ["--obd-confirmed"], [("03", response)])
                self.assertEqual(result, "OBD Confirmed read unavailable; no valid counted CAN reply\n")

    def test_refusals_and_pending_deadlines_do_not_change_session_or_retry(self):
        for service, flag, state in MODES:
            with self.subTest(category=flag):
                rejected = self.run_worker("engine-codes", [f"--obd-{flag}"], [(service, [f"7f{service}11"])])
                self.assertEqual(rejected, f"OBD {state} read rejected (NRC 11)\n")
                pending = self.run_worker("engine-codes", [f"--obd-{flag}"], [(service, [f"7f{service}78"])])
                self.assertIn("read unavailable", pending)

    def test_zero_padding_and_large_lists_keep_count_and_pipe_bounds(self):
        padded = self.run_worker("engine-codes", ["--obd-pending"], [("07", ["470101330000"])])
        self.assertEqual(padded, "OBD Pending emissions: 1 code\nOBD07 P0133\n")
        codes = "".join(f"{value:04x}" for value in range(0x0101, 0x0200))
        result = self.run_worker("engine-codes", ["--obd-confirmed"], [("03", ["43ff" + codes])])
        self.assertTrue(result.startswith("OBD Confirmed emissions: 255 codes\n"))
        self.assertIn("OBD03 P0180\n", result)
        self.assertNotIn("OBD03 P0181", result)
        self.assertTrue(result.endswith("More codes returned; first 128 shown\n"))
        self.assertEqual(len(result.splitlines()), 130)
        self.assertLess(len(result), 4096)

    def test_most_uses_the_same_counted_parser_and_releases_resources(self):
        expected = {
            "confirmed": "OBD Confirmed emissions: 2 codes\nOBD03 P0133\nOBD03 U1016\n",
            "pending": "OBD Pending emissions: 1 code\nOBD07 P0101\n",
            "permanent": "OBD Permanent emissions: 0 codes\n",
        }
        for _, flag, _ in MODES:
            with self.subTest(category=flag):
                result = most.run("engine-codes", ["--transport", "most", f"--obd-{flag}"], "up")
                self.assertEqual(result.returncode, 0)
                self.assertEqual(result.stdout, expected[flag])
                self.assertIn("unregister_target=1 unregister_client=1 free=1", result.stderr)

    def test_incompatible_modes_are_rejected_before_a_connection(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0)); listener.listen(); listener.settimeout(.05)
            for _, flag, _ in MODES:
                for incompatible in (("--clear", "--confirm-clear"), ("--confirm-clear",),
                                     ("--codes",), ("--status-mask",), ("--trace",)):
                    with self.subTest(category=flag, incompatible=incompatible):
                        result = subprocess.run([apps.BUILD / "engine-codes", "--host", "127.0.0.1",
                                                 "--port", str(listener.getsockname()[1]),
                                                 f"--obd-{flag}", *incompatible],
                                                capture_output=True, text=True, timeout=2)
                        self.assertEqual(result.returncode, 2)
                        with self.assertRaises(socket.timeout): listener.accept()

    def test_most_result_reaches_pipe_eof_before_bounded_cleanup(self):
        most.result_before_close(self, "engine-codes", ["--transport", "most", "--obd-pending"],
                                 "OBD Pending emissions: 1 code\nOBD07 P0101\n")


if __name__ == "__main__":
    unittest.main()
