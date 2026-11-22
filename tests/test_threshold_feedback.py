from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


class ThresholdFeedbackTests(unittest.TestCase):
    def test_bthome_layout_and_uint32_encoding(self):
        compiler = os.environ.get("HOST_CC") or shutil.which("cc") or (
            "/tmp/ch592-toolchain-20261122/root/usr/bin/tcc"
        )
        options = []
        if Path(compiler).name == "tcc":
            options.append(
                "-B/tmp/ch592-toolchain-20261122/root/usr/lib/x86_64-linux-gnu/tcc"
            )
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "test_threshold_feedback"
            subprocess.run(
                [
                    compiler,
                    "-std=c99",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *options,
                    "-I",
                    str(REPO / "APP/include"),
                    str(REPO / "tests/test_threshold_feedback.c"),
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
            self.assertIn("layout and uint32 encoding", result.stdout)


if __name__ == "__main__":
    unittest.main()
