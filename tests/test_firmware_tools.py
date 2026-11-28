from __future__ import annotations

import asyncio
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import time
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

            added = False
            record = []

            def fake_run(command, **kwargs):
                nonlocal added
                operation = command[3] if len(command) > 3 else ""
                record.append(command[3:])
                if operation == "add":
                    added = True
                if operation == "diff" and "--quiet" in command:
                    returncode = 1
                else:
                    returncode = 1 if operation == failing_command else 0
                if operation == "remote":
                    stdout = "origin\n"
                elif operation == "diff" and "--name-only" in command and added:
                    stdout = "firmware/ch592_ota.bin\n"
                else:
                    stdout = ""
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
            resets = [command for command in record if command[0] == "reset"]
            self.assertEqual(len(resets), 1)
            paths = resets[0][3:]
            self.assertTrue(paths)
            production = load_module(
                "publish_firmware_constants", REPO / "tools/publish_firmware.py"
            )
            self.assertTrue(
                all(
                    path.startswith("firmware/")
                    and any(path.endswith(name) for name in production.PUBLISHED_FILENAMES)
                    for path in paths
                ),
                resets,
            )

    def test_publisher_reports_commit_failure_and_rolls_back_staging(self):
        self._run_publisher_with_git_failure("commit")

    def test_publisher_reports_push_failure(self):
        self._run_publisher_with_git_failure("push")


def git(repo, *arguments, check=True):
    return subprocess.run(
        ["git", "-C", str(repo), *arguments],
        check=check,
        capture_output=True,
        text=True,
    )


def init_work_repo(path):
    path.mkdir(parents=True)
    subprocess.run(
        ["git", "init", "-b", "master", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    git(path, "config", "user.name", "GasPulse Tests")
    git(path, "config", "user.email", "gaspulse@example.invalid")
    (path / "README.md").write_text("test repository\n", encoding="utf-8")
    git(path, "add", "--", "README.md")
    git(path, "commit", "-m", "initial")
    return git(path, "rev-parse", "HEAD").stdout.strip()


def init_bare_repo(path):
    subprocess.run(
        ["git", "init", "--bare", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return path


def remote_head(path):
    result = subprocess.run(
        ["git", "--git-dir", str(path), "rev-parse", "refs/heads/master"],
        check=False,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else None


class PublisherGitTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        self.initial = init_work_repo(self.repo)
        self.origin = init_bare_repo(self.root / "origin.git")
        self.github = init_bare_repo(self.root / "github.git")
        git(self.repo, "remote", "add", "origin", str(self.origin))
        git(self.repo, "remote", "add", "github", str(self.github))

        image = b"\x6f\x00\x00\x00"
        self.binary = self.root / "app.bin"
        self.binary.write_bytes(image)
        self.sidecar = self.root / "app.bin.json"
        self.sidecar.write_text(
            json.dumps(
                {
                    "version": "1.4.4",
                    "size": len(image),
                    "crc32": f"0x{zlib.crc32(image) & 0xFFFFFFFF:08X}",
                }
            ),
            encoding="utf-8",
        )
        self.header = self.root / "broadcaster.h"
        self.header.write_bytes(b'#define FW_VERSION_STRING "1.4.4"\n')
        (self.root / "obj").mkdir()
        (self.root / "obj" / "Broadcaster_full.hex").write_text(
            ":00000001FF\n", encoding="ascii"
        )
        self.module = load_module(
            f"publish_firmware_git_{id(self)}", REPO / "tools/publish_firmware.py"
        )

    def tearDown(self):
        self.temporary.cleanup()

    def run_publisher(self, *extra):
        argv = [
            "publish_firmware.py",
            "--repo",
            str(self.repo),
            "--push",
            "--remote",
            "origin",
            "--remote",
            "github",
            *extra,
        ]
        with (
            patch.multiple(
                self.module,
                APP_BIN=self.binary,
                APP_JSON=self.sidecar,
                BASELINE_JSON=self.header,
                PROJECT_ROOT=self.root,
            ),
            patch.object(sys, "argv", argv),
        ):
            return self.module.main()

    def make_legacy(self, name="legacy"):
        legacy = self.root / name
        init_work_repo(legacy)
        remote = init_bare_repo(self.root / f"{name}.git")
        git(legacy, "remote", "add", "origin", str(remote))
        return legacy, remote

    def test_publisher_stages_only_release_whitelist(self):
        recovery = self.repo / "firmware" / "Broadcaster_full_recovery_1.4.3.hex"
        recovery.parent.mkdir()
        recovery.write_text("keep me untracked\n", encoding="ascii")
        self.assertEqual(self.run_publisher(), 0)
        tracked = git(self.repo, "ls-tree", "-r", "--name-only", "HEAD").stdout
        self.assertNotIn("Broadcaster_full_recovery_1.4.3.hex", tracked)
        self.assertEqual(
            git(self.repo, "status", "--porcelain").stdout.strip(),
            "?? firmware/Broadcaster_full_recovery_1.4.3.hex",
        )

    def test_publisher_refuses_preexisting_staged_changes(self):
        (self.repo / "README.md").write_text("user staged edit\n", encoding="utf-8")
        git(self.repo, "add", "--", "README.md")
        with self.assertRaises(SystemExit) as raised:
            self.run_publisher()
        self.assertNotEqual(raised.exception.code, 0)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD").stdout.strip(), self.initial)
        self.assertEqual(
            git(self.repo, "diff", "--cached", "--name-only").stdout.split(),
            ["README.md"],
        )
        self.assertEqual(
            git(self.repo, "show", ":README.md").stdout, "user staged edit\n"
        )

    def make_staged_manifest(self, repo):
        manifest = repo / "firmware/manifest.json"
        manifest.parent.mkdir(exist_ok=True)
        manifest.write_text('{"state": "head"}\n', encoding="utf-8")
        git(repo, "add", "--", "firmware/manifest.json")
        git(repo, "commit", "-m", "baseline manifest")
        head = git(repo, "rev-parse", "HEAD").stdout.strip()
        manifest.write_text('{"state": "staged"}\n', encoding="utf-8")
        git(repo, "add", "--", "firmware/manifest.json")
        manifest.write_text('{"state": "worktree"}\n', encoding="utf-8")
        return manifest, head

    def assert_staged_manifest_preserved(self, repo, manifest, head):
        self.assertEqual(manifest.read_text(encoding="utf-8"), '{"state": "worktree"}\n')
        self.assertEqual(
            git(repo, "show", ":firmware/manifest.json").stdout,
            '{"state": "staged"}\n',
        )
        self.assertEqual(git(repo, "rev-parse", "HEAD").stdout.strip(), head)

    def test_publisher_preflight_preserves_staged_firmware_and_worktree(self):
        manifest, head = self.make_staged_manifest(self.repo)
        with self.assertRaises(SystemExit) as raised:
            self.run_publisher()
        self.assertNotEqual(raised.exception.code, 0)
        self.assert_staged_manifest_preserved(self.repo, manifest, head)

    def test_legacy_preflight_happens_before_canonical_writes(self):
        legacy, _ = self.make_legacy()
        manifest, legacy_head = self.make_staged_manifest(legacy)
        with self.assertRaises(SystemExit) as raised:
            self.run_publisher("--legacy-repo", str(legacy))
        self.assertNotEqual(raised.exception.code, 0)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD").stdout.strip(), self.initial)
        self.assert_staged_manifest_preserved(legacy, manifest, legacy_head)

    def test_explicit_missing_remote_fails_but_other_remote_pushes_and_recovers(self):
        git(self.repo, "remote", "remove", "origin")
        with self.assertRaises(SystemExit) as raised:
            self.run_publisher()
        self.assertNotEqual(raised.exception.code, 0)
        head = git(self.repo, "rev-parse", "HEAD").stdout.strip()
        self.assertEqual(remote_head(self.github), head)

        git(self.repo, "remote", "add", "origin", str(self.origin))
        time.sleep(1.1)
        self.assertEqual(self.run_publisher(), 0)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD").stdout.strip(), head)
        self.assertEqual(remote_head(self.origin), head)
        self.assertEqual(remote_head(self.github), head)

    def test_publisher_second_run_skips_commit_and_retries_push(self):
        self.run_publisher()
        head = git(self.repo, "rev-parse", "HEAD").stdout.strip()
        subprocess.run(
            ["git", "--git-dir", str(self.origin), "update-ref", "refs/heads/master", self.initial],
            check=True,
        )
        time.sleep(1.1)
        self.assertEqual(self.run_publisher(), 0)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD").stdout.strip(), head)
        self.assertEqual(remote_head(self.origin), head)

    def test_legacy_failure_blocks_new_remotes(self):
        legacy, _ = self.make_legacy()
        git(legacy, "remote", "set-url", "origin", str(self.root / "missing-legacy.git"))
        with self.assertRaises(SystemExit) as raised:
            self.run_publisher("--legacy-repo", str(legacy))
        self.assertNotEqual(raised.exception.code, 0)
        self.assertNotEqual(git(self.repo, "rev-parse", "HEAD").stdout.strip(), self.initial)
        self.assertIsNone(remote_head(self.origin))
        self.assertIsNone(remote_head(self.github))

    def test_canonical_remote_failure_is_recoverable(self):
        legacy, legacy_remote = self.make_legacy()
        git(self.repo, "remote", "set-url", "origin", str(self.root / "missing-origin.git"))
        with self.assertRaises(SystemExit) as raised:
            self.run_publisher("--legacy-repo", str(legacy))
        self.assertNotEqual(raised.exception.code, 0)
        head = git(self.repo, "rev-parse", "HEAD").stdout.strip()
        legacy_head = git(legacy, "rev-parse", "HEAD").stdout.strip()
        self.assertEqual(remote_head(legacy_remote), legacy_head)
        self.assertIsNone(remote_head(self.origin))
        self.assertEqual(remote_head(self.github), head)

        git(self.repo, "remote", "set-url", "origin", str(self.origin))
        time.sleep(1.1)
        self.assertEqual(self.run_publisher("--legacy-repo", str(legacy)), 0)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD").stdout.strip(), head)
        self.assertEqual(remote_head(self.origin), head)
        self.assertEqual(remote_head(self.github), head)
        self.assertEqual(remote_head(legacy_remote), legacy_head)

    def test_legacy_mirror_contains_only_published_files(self):
        legacy, _ = self.make_legacy()
        recovery = self.repo / "firmware" / "Broadcaster_full_recovery_1.4.3.hex"
        recovery.parent.mkdir()
        recovery.write_text("not a release\n", encoding="ascii")
        self.assertEqual(self.run_publisher("--legacy-repo", str(legacy)), 0)
        expected = [
            "firmware/Broadcaster_full.hex",
            "firmware/ch592_ota.bin",
            "firmware/ch592_ota.bin.json",
            "firmware/manifest.json",
        ]
        self.assertEqual(
            git(legacy, "ls-tree", "-r", "--name-only", "HEAD", "firmware").stdout.splitlines(),
            expected,
        )
        for relative in expected:
            self.assertEqual((legacy / relative).read_bytes(), (self.repo / relative).read_bytes())
        self.assertFalse((legacy / "firmware" / recovery.name).exists())


def types_module(name, **attributes):
    module = __import__("types").ModuleType(name)
    for key, value in attributes.items():
        setattr(module, key, value)
    return module


if __name__ == "__main__":
    unittest.main()
