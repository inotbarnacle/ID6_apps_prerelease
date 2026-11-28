import argparse
from contextlib import redirect_stdout
from io import StringIO
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from capture_headunit import (FRAMEWORKS, JACINTO_FRAMEWORK_DIRS, JACINTO_STORAGE, MAX_STORAGE_KIB,
                              STORAGE, STORAGE_PATH, capture, enet_ip, main, ssh_read, storage_snapshot)

LIBRARY = b"\x7fELF\x01\x01" + bytes(10) + struct.pack("<HH", 3, 40) + b"example"
DF_HEADER = b"Filesystem 1024-blocks Used Available Capacity Mounted on\n"
DF_ROW = b"/dev/private-device 4096 1024 2048 34% /fs/private-mount\n"


def mock_head(firmware=b"NBTevo_A20000I", valid_frameworks=True):
    def read(command, optional=False):
        if command == "cat /opt/sys/etc/nbt_version.txt":
            return firmware
        if command == "uname -a":
            return b"QNX private-hostname 6.5.0 build board armle"
        if command.endswith("get screen 0"):
            return b"- resolution: x=1280, y=480\n"
        if command in ("cat /opt/hmi/ID5/lib/" + name for name in FRAMEWORKS):
            return LIBRARY if valid_frameworks else b"invalid"
        raise AssertionError(command)
    return read


def mock_jacinto(framework_dir=JACINTO_FRAMEWORK_DIRS[0]):
    """Split-node unit: SSH on the Jacinto, HMI files only over Qnet."""
    def read(command, optional=False):
        if command == "cat /etc/nbt_version.txt":
            return b"NBTevo_P18362I"
        if command == "uname -a":
            return b"QNX hu-jacinto 6.5.0 2015/12/01 J5_Evo armle"
        if command.startswith("on -f hu-omap") and command.endswith('"'):
            return b"- resolution: x=1280, y=480\n"
        if command == "on -f hu-omap /sbin/ifconfig":
            return b"lo0: inet 127.0.0.1\ndwc0: inet 169.254.199.119 netmask 0xffff0000\n"
        if command in ("cat " + framework_dir + "/" + name for name in FRAMEWORKS):
            return LIBRARY
        if optional:  # a wrong Qnet mount point reads empty
            return b""
        raise AssertionError(command)
    return read


class CaptureTest(unittest.TestCase):
    def test_accepts_an_unlisted_firmware_and_omits_hostname(self):
        report, files = capture(mock_head(), None)
        self.assertEqual(report["firmware"], "NBTevo_A20000I")
        self.assertEqual(report["firmware_source"], "ssh")
        self.assertEqual(report["display"], {"width": 1280, "height": 480})
        self.assertNotIn("private-hostname", str(report))
        self.assertEqual(set(files), set(FRAMEWORKS))
        self.assertTrue(all(len(f["sha256"]) == 64 for f in report["frameworks"].values()))
        self.assertNotIn("storage", report)  # legacy default has no new read or field

    def test_reports_direct_node_without_leaking_hostname(self):
        report, _ = capture(mock_head(), None)
        self.assertEqual(report["ssh_node"], "unknown")
        self.assertNotIn("private-hostname", str(report))

    def test_split_node_reads_hmi_over_qnet(self):
        report, files = capture(mock_jacinto(), None)
        self.assertEqual(report["firmware"], "NBTevo_P18362I")
        self.assertEqual(report["ssh_node"], "hu-jacinto")
        self.assertEqual(report["framework_dir"], JACINTO_FRAMEWORK_DIRS[0])
        self.assertEqual(report["hmi_node_ip"], "169.254.199.119")
        self.assertEqual(report["display"], {"width": 1280, "height": 480})
        self.assertEqual(set(files), set(FRAMEWORKS))

    def test_split_node_falls_back_to_other_qnet_mount(self):
        report, _ = capture(mock_jacinto(framework_dir=JACINTO_FRAMEWORK_DIRS[1]), None)
        self.assertEqual(report["framework_dir"], JACINTO_FRAMEWORK_DIRS[1])

    def test_manual_version_and_invalid_framework(self):
        report, _ = capture(mock_head(b""), "NBTevo_A20000I")
        self.assertEqual(report["firmware_source"], "manual")
        with self.assertRaisesRegex(ValueError, "differs"):
            capture(mock_head(), "NBTevo_A20001I")
        with self.assertRaisesRegex(ValueError, "ELF32"):
            capture(mock_head(valid_frameworks=False), None)
        with self.assertRaises(argparse.ArgumentTypeError):
            enet_ip("192.168.1.2")

    @patch("capture_headunit.subprocess.run")
    def test_keeps_strict_host_key_checking_in_legacy_mode(self, run):
        run.return_value.returncode = 0
        run.return_value.stdout = b"example"
        self.assertEqual(ssh_read("169.254.10.3", True, "uname -a"), b"example")
        args = run.call_args.args[0]
        self.assertIn("StrictHostKeyChecking=yes", args)
        self.assertIn("HostKeyAlgorithms=+ssh-rsa", args)
        self.assertEqual(args[-2:], ["root@169.254.10.3", "uname -a"])

    def test_storage_parser_retains_counts_and_valid_zero_without_names(self):
        report = storage_snapshot(DF_HEADER + DF_ROW)
        self.assertEqual(report, {"path": STORAGE_PATH, "status": "reported",
                                  "total_kib": 4096, "used_kib": 1024, "available_kib": 2048})
        self.assertNotIn("private", json.dumps(report))
        for total, used, available, percent in ((4096, 4096, 0, 100), (4096, 0, 4096, 0),
                                                 (MAX_STORAGE_KIB, 1, MAX_STORAGE_KIB - 1, 1)):
            row = f"/dev/fs {total} {used} {available} {percent}% /\n".encode()
            result = storage_snapshot(DF_HEADER + row)
            self.assertEqual(result["status"], "reported")
            self.assertEqual(result["available_kib"], available)

    def test_storage_parser_rejects_ambiguous_units_bounds_and_bad_rows(self):
        invalid = [b"", None, b"unavailable: private-address", DF_HEADER, DF_HEADER + DF_ROW * 2,
                   (DF_HEADER + DF_ROW).replace(b"1024-blocks", b"512-blocks"),
                   (DF_HEADER + DF_ROW).replace(b"1024-blocks", b"Size"),
                   DF_HEADER + b"/dev/fs\n 4096 1024 2048 34% /\n",
                   DF_HEADER + b"/dev/fs 4096 1024 2048 34%\n",
                   DF_HEADER + DF_ROW + b"x" * 4096,
                   DF_HEADER + DF_ROW.replace(b"private-device", b"private\x00device"),
                   DF_HEADER + DF_ROW.replace(b"34%", b"101%"),
                   DF_HEADER + DF_ROW.replace(b"34%", b"0.3%"),
                   DF_HEADER + DF_ROW.replace(b"/fs/private-mount", b"relative")]
        for field in (b"-1", b"+1", b"1.2", b"1e3", b"1,024", str(MAX_STORAGE_KIB + 1).encode()):
            invalid.append(DF_HEADER + DF_ROW.replace(b"4096", field))
        for row in (b"/dev/fs 0 0 0 0% /\n", b"/dev/fs 4096 4097 0 100% /\n",
                    b"/dev/fs 4096 0 4097 0% /\n", b"/dev/fs 4096 1024 4096 20% /\n"):
            invalid.append(DF_HEADER + row)
        for raw in invalid:
            with self.subTest(raw=raw if raw is None or len(raw) < 100 else b"oversized"):
                result = storage_snapshot(raw)
                self.assertEqual(result["status"], "unavailable")
                self.assertNotIn("available_kib", result)
                self.assertNotIn("private", json.dumps(result))

    def storage_reader(self, split=False, storage=DF_HEADER + DF_ROW):
        calls = []
        base = mock_jacinto() if split else mock_head()

        def read(command, optional=False):
            calls.append(command)
            if not split and command == "uname -a":
                return b"QNX hu-omap 6.5.0 build board armle"
            if command == (JACINTO_STORAGE if split else STORAGE):
                self.assertTrue(optional)
                if isinstance(storage, Exception):
                    raise storage
                return storage
            return base(command, optional)

        return read, calls

    def test_direct_storage_is_opt_in_and_does_not_change_legacy_fields(self):
        read, calls = self.storage_reader()
        legacy, old_files = capture(read, None)
        self.assertNotIn(STORAGE, calls)
        report, files = capture(read, None, include_storage=True)
        self.assertEqual(calls.count(STORAGE), 1)
        self.assertEqual(calls[-1], STORAGE)
        self.assertEqual({k: v for k, v in report.items() if k != "storage"}, legacy)
        self.assertEqual(files, old_files)
        self.assertEqual(report["storage"]["available_kib"], 2048)

    def test_split_storage_runs_on_omap_without_jacinto_df_fallback(self):
        read, calls = self.storage_reader(split=True)
        report, _ = capture(read, None, include_storage=True)
        self.assertEqual(report["storage"]["available_kib"], 2048)
        self.assertEqual(calls[-1], JACINTO_STORAGE)
        self.assertNotIn(STORAGE, calls)

    def test_unknown_node_or_storage_failure_preserves_other_evidence(self):
        report, _ = capture(mock_head(), None, include_storage=True)
        self.assertEqual(report["storage"]["reason"], "hmi_node_unconfirmed")
        for failure in (b"", b"private error", OSError("private error"),
                        ValueError("private error"), subprocess.TimeoutExpired("private command", 60)):
            read, calls = self.storage_reader(storage=failure)
            result, files = capture(read, None, include_storage=True)
            self.assertEqual(result["storage"]["status"], "unavailable")
            self.assertEqual(result["firmware"], "NBTevo_A20000I")
            self.assertEqual(set(files), set(FRAMEWORKS))
            self.assertNotIn("private", json.dumps(result["storage"]))
            self.assertEqual(calls.count(STORAGE), 1)

    def test_cli_storage_flag_writes_metadata_without_framework_binaries(self):
        read, calls = self.storage_reader()
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "capture"
            args = ["capture_headunit.py", "--head", "169.254.10.3", "--out", str(out), "--storage"]

            def mocked_ssh(head, legacy, command, optional=False):
                self.assertEqual(head, "169.254.10.3")
                self.assertFalse(legacy)
                return read(command, optional)

            with patch("sys.argv", args), patch("capture_headunit.ssh_read", side_effect=mocked_ssh), \
                    redirect_stdout(StringIO()):
                main()
            self.assertEqual([path.name for path in out.iterdir()], ["compatibility.json"])
            result = json.loads((out / "compatibility.json").read_text())
            self.assertEqual(result["storage"]["available_kib"], 2048)
            self.assertEqual(calls[-1], STORAGE)


if __name__ == "__main__":
    unittest.main()
