"""Keep pinned N57 xDrive controls inside the verified DSC/LMV profile. Host only."""
import os
import subprocess
import time
import unittest

import test_drive_apps


VIN = "TEST0000000000001"
INDEX = "010203"  # Synthetic identity; not a compatibility assertion.


class N57XDriveGateTest(unittest.TestCase):
    def run_control(self, transactions, targets, value=0, pin=INDEX, status=False, owner=VIN):
        args = ["--status" if status else "--drive-mode", str(value)]
        if pin is not None:
            args += ["--n57-sgbd", pin]
        return test_drive_apps.DriveAppsTest.run_worker(
            self, "xdrive-control", args, transactions, targets=targets,
            env=dict(os.environ, NBTEVO_OWN_VIN=owner))

    @staticmethod
    def gseries(dsc, current=1):
        mode = "62db32" + "00" * 7 + ("20" if current else "c0")
        return [("22f150", ["62f150" + dsc]), ("22dbe1", ["62dbe101"]),
                ("22db32", [mode])], ["29", "29", "19"]

    @staticmethod
    def fseries(lmv="0f1480"):
        tx = [("22f150", ["62f1500f1860"]), ("22dbe1", ["62dbe101"]),
              ("22f150", ["62f150" + lmv])]
        targets = ["29", "29", "19"]
        if lmv == "0f1480":
            tx += [("224002", ["62400209c4200f"])]
            targets += ["19"]
        return tx, targets

    def test_pinned_n57_rejects_both_gseries_profiles_and_modes(self):
        for dsc in ("0f2510", "0f2850"):
            for value in (0, 1):
                with self.subTest(dsc=dsc, value=value):
                    tx, targets = self.gseries(dsc)
                    # The helper rejects every extra request, including Mode 01,
                    # legacy DA2E, DDE reads and the DSC write 2E DBE1.
                    output = self.run_control(tx, targets, value=value)
                    self.assertIn("N57 mode changes require DSC_25", output)

    def test_pinned_n57_keeps_gseries_status_read_only(self):
        for dsc in ("0f2510", "0f2850"):
            with self.subTest(dsc=dsc):
                tx, targets = self.gseries(dsc)
                self.assertEqual(self.run_control(tx, targets, status=True),
                                 "Transfer case reports AWD.\n")

    def test_pinned_n57_rejects_unverified_lmv(self):
        for value in (0, 1):
            with self.subTest(value=value):
                tx, targets = self.fseries(lmv="040506")
                self.assertIn("requires a verified LMV 0F1480",
                              self.run_control(tx, targets, value=value))

    def test_verified_drivetrain_still_requires_matching_dde_pin(self):
        tx, targets = self.fseries()
        tx += [("22f190", ["62f190" + VIN.encode().hex()]),
               ("22f150", ["62f1500f14b0"]), ("224306", ["62430608"]),
               ("22da27", ["62da27000100000000"]), ("22f150", ["62f150040506"])]
        targets += ["12", "18", "18", "18", "12"]
        for value in (0, 1):
            with self.subTest(value=value):
                self.assertIn("Needs engine running, stationary, and confirmed Park.",
                              self.run_control(tx, targets, value=value))

    def test_pinned_n57_write_requires_a_fresh_atomic_drive_snapshot(self):
        class Delayed(list):
            def __iter__(self):
                time.sleep(.45)
                return super().__iter__()

        for stale in (False, True):
            for value in (0, 1):
                with self.subTest(stale=stale, value=value):
                    tx, targets = self.fseries()
                    tx += [("22f190", ["62f190" + VIN.encode().hex()]),
                           ("22f150", ["62f1500f14b0"]), ("224306", ["62430608"]),
                           ("22da27", ["62da27000100000000"]), ("22f150", ["62f150" + INDEX]),
                           ("2c03f303", Delayed(["6c03f303"]) if stale else ["6c03f303"]),
                           ("2c01f303595501024c140102", Delayed(["6c01f303"]) if stale else ["6c01f303"]),
                           ("22f303", ["62f30305220000"])]
                    targets += ["12", "18", "18", "18", "12", "12", "12", "12"]
                    if not stale:
                        tx += [("2edbe1%02x" % value, ["6edbe1"]),
                               ("224002", ["62400209c4" + ("20" if value else "28") + "0f"])]
                        targets += ["29", "19"]
                    result = self.run_control(tx, targets, value=value)
                    if stale:
                        # Each reply meets its own timeout; the combined Park snapshot is too old.
                        self.assertIn("Needs engine running, stationary, and confirmed Park.", result)
                    else:
                        self.assertEqual(result, "Transfer case confirms %s.\n" % ("AWD" if value else "RWD"))

    def test_invalid_dde_pin_never_opens_diagnostics(self):
        for pin in ("000000", "0x1234", "010203x"):
            with self.subTest(pin=pin):
                result = subprocess.run(
                    [test_drive_apps.BUILD / "xdrive-control", "--host", "127.0.0.1", "--drive-mode", "0",
                     "--n57-sgbd", pin, "--port", "1"],
                    capture_output=True, timeout=2)
                self.assertEqual(result.returncode, 2)

    def test_preserves_owner_vin_gate(self):
        for pin, (tx, targets) in ((INDEX, self.fseries()), (None, self.gseries("0f2850"))):
            self.assertIn("Owner VIN missing, unreadable or mismatched.",
                          self.run_control(tx, targets, owner="", pin=pin))

    def test_unpinned_gseries_preserves_legacy_controls(self):
        for dsc in ("0f2510", "0f2850"):
            for value in (0, 1):
                with self.subTest(dsc=dsc, value=value):
                    tx, targets = self.gseries(dsc)
                    if dsc == "0f2510" and value:
                        self.assertIn("Cycle the ignition to restore AWD",
                                      self.run_control(tx, targets, value=value, pin=None))
                        continue
                    tx += [("22f190", ["62f190" + VIN.encode().hex()]),
                           ("22da2e", ["62da2e0006"]), ("010c", ["410c0000"]),
                           ("010d", ["410d00"]), ("2edbe1%02x" % value, ["6edbe1"]),
                           ("22db32", ["62db32" + "00" * 7 + ("20" if value else "c0")])]
                    targets += ["12", "18", "12", "12", "29", "19"]
                    self.assertEqual(self.run_control(tx, targets, value=value, pin=None),
                                     "Transfer case confirms %s.\n" % ("AWD" if value else "RWD"))


if __name__ == "__main__":
    unittest.main()
