from __future__ import annotations

import asyncio
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
import zlib
import xml.etree.ElementTree as ET
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ihex_record(address, record_type, data):
    body = bytes(
        [len(data), (address >> 8) & 0xFF, address & 0xFF, record_type]
    ) + data
    return ":" + body.hex().upper() + f"{(-sum(body)) & 0xFF:02X}"


def app_hex(address):
    return "\n".join(
        [
            ihex_record(0, 4, address.to_bytes(4, "big")[:2]),
            ihex_record(address & 0xFFFF, 0, b"\x6f\x00\x00\x00"),
            ":00000001FF",
        ]
    ) + "\n"


class FirmwareToolTests(unittest.TestCase):
    def test_mounriver_excludes_host_tests(self):
        tree = ET.parse(REPO / ".cproject")
        exclusions = [
            entry.attrib.get("excluding", "")
            for entry in tree.findall(".//sourceEntries/entry")
        ]
        self.assertTrue(
            any("tests" in value.split("|") for value in exclusions),
            "MounRiver would compile host-only tests into the firmware",
        )

    def test_make_images_rejects_app_above_required_base(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "jump.hex").write_text(app_hex(0), encoding="ascii")
            (root / "app.hex").write_text(app_hex(0x1010), encoding="ascii")
            (root / "iap.hex").write_text(app_hex(0x6D000), encoding="ascii")
            result = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    str(REPO / "tools/make_ota_images.py"),
                    "--jump-hex",
                    str(root / "jump.hex"),
                    "--app-hex",
                    str(root / "app.hex"),
                    "--iap-hex",
                    str(root / "iap.hex"),
                    "--full-hex",
                    str(root / "full.hex"),
                    "--app-bin",
                    str(root / "app.bin"),
                    "--version-header",
                    str(root / "missing.h"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("0x01000", (result.stderr + result.stdout).lower())

    def test_rejected_app_does_not_overwrite_full_hex(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "jump.hex").write_text(app_hex(0), encoding="ascii")
            (root / "app.hex").write_text(app_hex(0x1010), encoding="ascii")
            (root / "iap.hex").write_text(app_hex(0x6D000), encoding="ascii")
            (root / "full.hex").write_text("original\n", encoding="ascii")
            result = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    str(REPO / "tools/make_ota_images.py"),
                    "--jump-hex",
                    str(root / "jump.hex"),
                    "--app-hex",
                    str(root / "app.hex"),
                    "--iap-hex",
                    str(root / "iap.hex"),
                    "--full-hex",
                    str(root / "full.hex"),
                    "--app-bin",
                    str(root / "app.bin"),
                    "--version-header",
                    str(root / "missing.h"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((root / "full.hex").read_text("ascii"), "original\n")

    def test_make_images_accepts_required_base(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "jump.hex").write_text(app_hex(0), encoding="ascii")
            (root / "app.hex").write_text(app_hex(0x1000), encoding="ascii")
            (root / "iap.hex").write_text(app_hex(0x6D000), encoding="ascii")
            result = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    str(REPO / "tools/make_ota_images.py"),
                    "--jump-hex",
                    str(root / "jump.hex"),
                    "--app-hex",
                    str(root / "app.hex"),
                    "--iap-hex",
                    str(root / "iap.hex"),
                    "--full-hex",
                    str(root / "full.hex"),
                    "--app-bin",
                    str(root / "app.bin"),
                    "--version-header",
                    str(root / "missing.h"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / "app.bin").read_bytes(), b"\x6f\x00\x00\x00")

    def test_standalone_client_rejects_zero_block_size(self):
        bleak = types_module("bleak", BleakClient=object)
        with patch.dict(sys.modules, {"bleak": bleak}):
            client_module = load_module(
                "ch592_ota_client_under_test", REPO / "tools/ch592_ota_client.py"
            )
        info = (
            b"\x00"
            + client_module.IMAGE_SIZE.to_bytes(4, "little")
            + b"\x00\x00"
            + b"\x92\x05"
        )

        class FakeClient:
            services = [
                SimpleNamespace(
                    characteristics=[SimpleNamespace(uuid=client_module.OTA_CHAR)]
                )
            ]

            async def write_gatt_char(self, *args, **kwargs):
                return None

            async def read_gatt_char(self, *args, **kwargs):
                return info

        try:
            asyncio.run(
                client_module.upload(FakeClient(), b"\x6f\x00\x00\x00", 16, False)
            )
        except Exception as err:
            self.assertIsInstance(err, RuntimeError)
            self.assertIn("block size", str(err))
        else:
            self.fail("zero block size was accepted")

    def _run_publisher_with_git_failure(self, failing_command):
        module = load_module(
            "publish_firmware_under_test", REPO / "tools/publish_firmware.py"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = b"\x6f\x00\x00\x00"
            binary = root / "app.bin"
            binary.write_bytes(image)
            sidecar = root / "app.bin.json"
            sidecar.write_text(
                json.dumps(
                    {
                        "version": "1.4.4",
                        "size": len(image),
                        "crc32": f"0x{zlib.crc32(image) & 0xFFFFFFFF:08X}",
                    }
                ),
                encoding="utf-8",
            )
            header = root / "broadcaster.h"
            header.write_bytes(b'#define FW_VERSION_STRING "1.4.4"\n')
            repo = root / "repo"
            repo.mkdir()

            def fake_run(command, **kwargs):
                operation = command[3] if len(command) > 3 else ""
                returncode = 1 if operation == failing_command else 0
                stdout = "origin\n" if operation == "remote" else ""
                return subprocess.CompletedProcess(command, returncode, stdout=stdout)

            argv = [
                "publish_firmware.py",
                "--repo",
                str(repo),
                "--push",
                "--remote",
                "origin",
            ]
            with (
                patch.multiple(
                    module,
                    APP_BIN=binary,
                    APP_JSON=sidecar,
                    BASELINE_JSON=header,
                    PROJECT_ROOT=root,
                ),
                patch.object(module.subprocess, "run", side_effect=fake_run),
                patch.object(sys, "argv", argv),
            ):
                with self.assertRaises(SystemExit) as raised:
                    module.main()
            self.assertNotEqual(raised.exception.code, 0)

    def test_publisher_reports_commit_failure(self):
        self._run_publisher_with_git_failure("commit")

    def test_publisher_reports_push_failure(self):
        self._run_publisher_with_git_failure("push")


def types_module(name, **attributes):
    module = __import__("types").ModuleType(name)
    for key, value in attributes.items():
        setattr(module, key, value)
    return module


if __name__ == "__main__":
    unittest.main()
