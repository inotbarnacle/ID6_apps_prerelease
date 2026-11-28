"""Exercise build.sh argument assembly; this is not a QNX compilation.

Temporary compiler recorders accept the script's commands without producing
binaries. Selected worker commands are then linked with the real host compiler
and the existing fake MOST implementation, without running a collector.
"""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


NATIVE = Path(__file__).resolve().parents[1]


class NativeBuildInputsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="native-build-inputs-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / "native build"
        self.root.mkdir()
        shutil.copyfile(NATIVE / "build.sh", self.root / "build.sh")
        for name in ("src", "sdk", "fileops.c"):
            (self.root / name).symlink_to(NATIVE / name)
        frameworks = self.root / "private frameworks"
        frameworks.mkdir()
        for name in ("libAppFramework.A.so", "libCoreFramework.A.so"):
            (frameworks / name).touch()  # Empty test placeholders, never linked.
        self.log = self.root / "commands.jsonl"
        host = self.root / "tool recorders"
        binaries = host / "usr/bin"
        binaries.mkdir(parents=True)
        recorder = "#!" + sys.executable + "\n" + '''import json, os, sys
with open(os.environ["BUILD_INPUTS_LOG"], "a") as log:
    log.write(json.dumps([os.path.basename(sys.argv[0])] + sys.argv[1:]) + "\\n")
if os.environ.get("BUILD_INPUTS_FAIL") and os.path.basename(sys.argv[0]) == "qcc":
    sys.exit(42)
'''
        for name in ("qcc", "ntoarm-gcc"):
            tool = binaries / name
            tool.write_text(recorder)
            tool.chmod(0o700)
        self.env = dict(os.environ, QNX_HOST=str(host),
                        NBTEVO_FRAMEWORKS_DIR=str(frameworks),
                        NBTEVO_RUNTIME_DIR=str(self.root / "absent runtime"),
                        NBTAAD_SOURCE="", BUILD_INPUTS_LOG=str(self.log),
                        BUILD_INPUTS_FAIL="")

    def run_driver(self):
        result = subprocess.run(["sh", str(self.root / "build.sh")],
                                env=self.env, capture_output=True, text=True,
                                timeout=30)
        commands = [json.loads(line) for line in self.log.read_text().splitlines()]
        return result, [args[1:] for args in commands if args[0] == "qcc"]

    def test_sources_are_unique_present_and_objects_are_built_first(self):
        result, commands = self.run_driver()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(len(commands), 10)
        outputs = set()
        for args in commands:
            output = args[args.index("-o") + 1]
            inputs = [arg for arg in args if arg.endswith((".c", ".cpp", ".o"))
                      and arg != output]
            with self.subTest(output=output):
                self.assertEqual(len(inputs), len(set(inputs)),
                                 "duplicate source/object in " + output)
                for source in inputs:
                    if source.endswith(".o"):
                        self.assertIn(source, outputs)
                    else:
                        self.assertTrue((self.root / source).is_file(), source)
            self.assertNotIn(output, outputs)
            outputs.add(output)

    def test_worker_link_inputs_with_real_host_compiler(self):
        result, commands = self.run_driver()
        self.assertEqual(result.returncode, 0, result.stderr)
        workers = ["warm_deck_worker.c"]
        if (NATIVE / "src/workers/dpf_deck_worker.c").exists():
            workers.append("dpf_deck_worker.c")
        objects = {args[args.index("-o") + 1]: args[args.index("-c") + 1]
                   for args in commands if "-c" in args}
        for worker in workers:
            with self.subTest(worker=worker):
                matches = [args for args in commands
                           if "src/workers/" + worker in args]
                self.assertEqual(len(matches), 1)
                args = matches[0]
                output_index = args.index("-o")
                host_args = []
                for index, arg in enumerate(args):
                    if index in (output_index, output_index + 1):
                        continue
                    if arg.startswith(("-V", "-Wc,")) or arg in (
                            "-lsocket", "-Lout", "-mno-unaligned-access"):
                        continue
                    host_args.append(objects.get(arg, arg))
                host_args += ["-std=c99", "-DMOST_HOST", "-Itests", "-pthread",
                              "tests/fake_most_env.c", "tests/fake_dsi.c",
                              "tests/fake_dme.c", "-lm", "-o", str(self.root / worker)]
                compiler = shlex.split(os.environ.get("CC", "cc"))
                linked = subprocess.run(compiler + host_args, cwd=NATIVE,
                                        capture_output=True, text=True, timeout=60)
                self.assertEqual(linked.returncode, 0, linked.stderr)

    def test_compiler_failure_stops_the_build(self):
        self.env["BUILD_INPUTS_FAIL"] = "1"
        result, commands = self.run_driver()
        self.assertEqual(result.returncode, 42)
        self.assertEqual(len(commands), 1)


if __name__ == "__main__":
    unittest.main()
