from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


class FastCrc32Tests(unittest.TestCase):
    def test_fast_crc_matches_bitwise_reference(self):
        compiler = os.environ.get("HOST_CC") or shutil.which("cc") or (
            "/tmp/ch592-toolchain-20261122/root/usr/bin/tcc"
        )
        compiler_includes = []
        if Path(compiler).name == "tcc":
            compiler_includes = [
                "-B/tmp/ch592-toolchain-20261122/root/usr/lib/x86_64-linux-gnu/tcc"
            ]
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "test_crc32_fast"
            subprocess.run(
                [
                    compiler,
                    "-std=c99",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *compiler_includes,
                    "-I",
                    str(REPO / "APP/include"),
                    str(REPO / "tests/test_crc32_fast.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [str(binary)], check=True, capture_output=True, text=True
            )
            self.assertIn("matches the bitwise reference", result.stdout)


if __name__ == "__main__":
    unittest.main()
