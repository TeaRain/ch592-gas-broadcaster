from __future__ import annotations

import ast
import hashlib
import json
import unittest
import zipfile
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DIST = REPO.parent
HA_REPO = DIST / "gitee"


def version_tuple(value):
    return tuple(int(part) for part in str(value).split("."))


def remote_minimum():
    path = HA_REPO / "custom_components/ch592_ota/const.py"
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "REMOTE_OTA_MIN_VERSION"
            for target in node.targets
        ):
            return ast.literal_eval(node.value)
    raise AssertionError("REMOTE_OTA_MIN_VERSION is missing")


class ReleaseArtifactTests(unittest.TestCase):
    def test_firmware_release_supports_remote_wake_and_matches_hashes(self):
        firmware = REPO / "firmware"
        manifest = json.loads((firmware / "manifest.json").read_text(encoding="utf-8"))
        self.assertGreaterEqual(version_tuple(manifest["version"]), remote_minimum())

        image = (firmware / manifest["file"]).read_bytes()
        self.assertEqual(len(image), manifest["size"])
        self.assertEqual(f"0x{zlib.crc32(image) & 0xFFFFFFFF:08X}", manifest["crc32"])
        self.assertEqual(hashlib.sha256(image).hexdigest(), manifest["sha256"])
        self.assertTrue((firmware / "Broadcaster_full.hex").is_file())

    def test_hacs_zip_contains_the_current_integration(self):
        archive_path = DIST / "ch592_ota.zip"
        member = "custom_components/ch592_ota/manifest.json"
        with zipfile.ZipFile(archive_path) as archive:
            self.assertIsNone(archive.testzip())
            packaged = archive.read(member)
        current = (HA_REPO / member).read_bytes()
        self.assertEqual(current, packaged)


if __name__ == "__main__":
    unittest.main()
