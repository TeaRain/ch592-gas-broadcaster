#!/usr/bin/env python3
"""Publish the built OTA image to the firmware repository.

Run tools/make_ota_images.py first so that obj/Broadcaster_app.bin and its
.json sidecar exist. This script copies both into <repo>/firmware/ and writes
<repo>/firmware/manifest.json, which the ch592_ota Home Assistant integration
polls to discover new firmware versions.

Usage:
    python tools/publish_firmware.py --repo ../ch592-gas-broadcaster \
        --notes "fix: OTA 分片" [--push]

--push commits the firmware directory and pushes to every configured remote
(origin = Gitee, github = GitHub by convention; missing remotes are skipped).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zlib
from datetime import UTC, datetime
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
OBJ_DIR = PROJECT_ROOT / "obj"
APP_BIN = OBJ_DIR / "Broadcaster_app.bin"
APP_JSON = OBJ_DIR / "Broadcaster_app.bin.json"
BASELINE_JSON = PROJECT_ROOT / "APP" / "include" / "broadcaster.h"


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_header_version() -> str | None:
    """Read FW_VERSION_STRING from the GBK-encoded header, for a cross-check."""
    try:
        text = BASELINE_JSON.read_bytes().decode("gbk", errors="replace")
    except OSError:
        return None
    match = re.search(r'FW_VERSION_STRING\s+"([^"]*)"', text)
    return match.group(1) if match else None


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, help="firmware repository working copy")
    parser.add_argument("--notes", default="", help="release notes for the manifest")
    parser.add_argument("--push", action="store_true", help="git commit and push")
    parser.add_argument("--remote", action="append", help="remote names to push (default: origin, github)")
    args = parser.parse_args()

    if not APP_BIN.exists() or not APP_JSON.exists():
        fail("obj/Broadcaster_app.bin(.json) 不存在，先运行 tools/make_ota_images.py")

    sidecar = json.loads(APP_JSON.read_text(encoding="utf-8"))
    version = str(sidecar.get("version") or "")
    if not version:
        fail("sidecar 里没有 version")

    header_version = read_header_version()
    if header_version and header_version != version:
        fail(
            f"版本不一致：broadcaster.h={header_version}, sidecar={version}；"
            "请重新编译并运行 make_ota_images.py"
        )

    image = APP_BIN.read_bytes()
    crc32 = f"0x{zlib.crc32(image) & 0xFFFFFFFF:08X}"
    if str(sidecar.get("crc32", "")).upper() != crc32.upper():
        fail(f"CRC32 不一致：sidecar={sidecar.get('crc32')}, 实际={crc32}")

    repo = Path(args.repo).expanduser().resolve()
    firmware_dir = repo / "firmware"
    if not repo.is_dir():
        fail(f"仓库目录不存在: {repo}")
    firmware_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(APP_BIN, firmware_dir / "ch592_ota.bin")
    shutil.copy2(APP_JSON, firmware_dir / "ch592_ota.bin.json")
    full_hex = PROJECT_ROOT / "obj" / "Broadcaster_full.hex"
    if full_hex.exists():
        # JumpIAP + APP + IAP，用于 WCH-LinkE 首次/恢复烧录（只擦代码区）
        shutil.copy2(full_hex, firmware_dir / "Broadcaster_full.hex")

    manifest = {
        "version": version,
        "file": "ch592_ota.bin",
        "size": len(image),
        "crc32": crc32,
        "sha256": sha256_of(firmware_dir / "ch592_ota.bin"),
        "built": datetime.now(UTC).replace(microsecond=0).isoformat(),
        "notes": args.notes or f"ch592_ota {version}",
    }
    manifest_path = firmware_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"published {version} ({len(image)} bytes, {crc32}) -> {firmware_dir}")

    if not args.push:
        return 0

    remotes = args.remote or ["origin", "github"]
    subprocess.run(
        ["git", "-C", str(repo), "add", "firmware"],
        check=True,
    )
    subprocess.run(
        ["git", "-C", str(repo), "commit", "-m", f"firmware: {version}"],
        check=False,  # nothing to commit is fine
    )
    for remote in remotes:
        names = subprocess.run(
            ["git", "-C", str(repo), "remote"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.split()
        if remote not in names:
            print(f"skip remote {remote} (not configured)")
            continue
        subprocess.run(
            ["git", "-C", str(repo), "push", remote, "HEAD"],
            check=False,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
