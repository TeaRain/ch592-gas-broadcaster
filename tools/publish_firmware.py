#!/usr/bin/env python3
"""Publish the built OTA image to the firmware repository.

Run tools/make_ota_images.py first so that obj/Broadcaster_app.bin and its
.json sidecar exist. This script copies both into <repo>/firmware/ and writes
<repo>/firmware/manifest.json, which the ch592_ota Home Assistant integration
polls to discover new firmware versions.

Usage:
    python tools/publish_firmware.py --repo ../gaspulse-firmware \
        --notes "fix: OTA 分片" [--push]

    python tools/publish_firmware.py --repo ../gaspulse-firmware \
        --legacy-repo ../ch592-gas-broadcaster --push --remote origin

Only the explicit firmware release whitelist is committed. With --push, the
legacy mirror is pushed and verified before canonical remotes. Re-running the
same release skips an unchanged commit but retries every required push.
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
PUBLISHED_FILENAMES = (
    "ch592_ota.bin",
    "ch592_ota.bin.json",
    "Broadcaster_full.hex",
    "manifest.json",
)
COMMIT_ATTRIBUTION = "Co-Authored-By: Claude Code <noreply@anthropic.com>"


class PublishError(Exception):
    """A recoverable publish step failed."""


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


def _git(repo: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repo), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )


def _require(result: subprocess.CompletedProcess[str], action: str) -> None:
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise PublishError(f"{action}失败（退出码 {result.returncode}）: {detail}")


def published_paths(repo: Path) -> list[str]:
    """Return only existing, supported release members."""
    return [
        f"firmware/{name}"
        for name in PUBLISHED_FILENAMES
        if (repo / "firmware" / name).is_file()
    ]


def preflight_clean_index(repo: Path) -> None:
    """Refuse staged work before any release file is copied or rewritten."""
    staged = _git(repo, "diff", "--cached", "--name-only")
    _require(staged, "检查既有暂存区")
    if staged.stdout.strip():
        raise PublishError("仓库已有暂存修改；请先由用户处理，发布脚本不会纳入或清除")


def stage_and_commit(repo: Path, version: str, relative_paths: list[str]) -> bool:
    """Commit exactly the release whitelist, refusing unrelated staged work."""
    allowed = {f"firmware/{name}" for name in PUBLISHED_FILENAMES}
    if not relative_paths or any(path not in allowed for path in relative_paths):
        raise PublishError("发布暂存列表为空或包含白名单外文件")

    preflight_clean_index(repo)
    added = _git(repo, "add", "--", *relative_paths)
    _require(added, "暂存发布白名单")
    changed = _git(repo, "diff", "--cached", "--quiet", "--", *relative_paths)
    if changed.returncode == 0:
        print(f"no release changes in {repo}; commit skipped")
        return False
    if changed.returncode != 1:
        _require(changed, "检查发布差异")

    staged_names = _git(repo, "diff", "--cached", "--name-only")
    _require(staged_names, "复核暂存文件")
    actual = set(staged_names.stdout.splitlines())
    if not actual or not actual.issubset(allowed):
        raise PublishError(f"暂存文件超出发布白名单: {sorted(actual)}")
    committed = _git(
        repo,
        "commit",
        "-m",
        f"firmware: {version}",
        "-m",
        COMMIT_ATTRIBUTION,
    )
    _require(committed, "git commit")
    print(f"committed firmware {version} in {repo}")
    return True


def validate_legacy_repo(canonical_repo: Path, legacy_repo: Path) -> None:
    """Validate the separate legacy checkout before touching canonical files."""
    if canonical_repo.resolve() == legacy_repo.resolve():
        raise PublishError("旧镜像仓库不能与 canonical 仓库相同")
    inside = _git(legacy_repo, "rev-parse", "--is-inside-work-tree")
    _require(inside, "检查旧镜像 Git 工作副本")
    if inside.stdout.strip() != "true":
        raise PublishError("旧镜像路径不是 Git 工作副本")


def sync_legacy_repo(canonical_repo: Path, legacy_repo: Path, version: str) -> None:
    """Copy only release artifacts into the legacy working copy and commit."""
    canonical_repo = canonical_repo.resolve()
    legacy_repo = legacy_repo.resolve()
    validate_legacy_repo(canonical_repo, legacy_repo)
    preflight_clean_index(legacy_repo)

    paths = published_paths(canonical_repo)
    required = {
        "firmware/ch592_ota.bin",
        "firmware/ch592_ota.bin.json",
        "firmware/manifest.json",
    }
    if not required.issubset(paths):
        raise PublishError("canonical 发布目录缺少必要固件文件")
    (legacy_repo / "firmware").mkdir(parents=True, exist_ok=True)
    for relative in paths:
        shutil.copy2(canonical_repo / relative, legacy_repo / relative)
    stage_and_commit(legacy_repo, version, published_paths(legacy_repo))


def push_and_verify(repo: Path, remote: str) -> None:
    """Push master and prove the remote points at the exact local commit."""
    pushed = _git(repo, "push", remote, "HEAD:master")
    _require(pushed, f"git push {remote}")
    local = _git(repo, "rev-parse", "HEAD")
    _require(local, "读取本地 HEAD")
    remote_result = _git(repo, "ls-remote", remote, "refs/heads/master")
    _require(remote_result, f"核验远端 {remote}")
    remote_sha = remote_result.stdout.split()[0] if remote_result.stdout.split() else ""
    local_sha = local.stdout.strip()
    if remote_sha != local_sha:
        raise PublishError(f"远端 {remote} 与本地 HEAD 不一致: {remote_sha} != {local_sha}")
    print(f"verified {remote}: {local_sha}")


def _remote_names(repo: Path) -> list[str]:
    result = _git(repo, "remote")
    _require(result, "读取远端列表")
    return result.stdout.split()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, help="firmware repository working copy")
    parser.add_argument("--notes", default="", help="release notes for the manifest")
    parser.add_argument("--push", action="store_true", help="git commit and push")
    parser.add_argument("--remote", action="append", help="remote names to push (default: origin, github)")
    parser.add_argument("--legacy-repo", help="legacy Gitee release-mirror working copy")
    parser.add_argument("--legacy-remote", default="origin", help="legacy mirror remote (default: origin)")
    args = parser.parse_args()

    if args.legacy_repo and not args.push:
        fail("--legacy-repo 需要同时指定 --push")
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
    legacy_repo = (
        Path(args.legacy_repo).expanduser().resolve() if args.legacy_repo else None
    )
    try:
        preflight_clean_index(repo)
        if legacy_repo is not None:
            validate_legacy_repo(repo, legacy_repo)
            preflight_clean_index(legacy_repo)
    except PublishError as error:
        fail(str(error))
    firmware_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(APP_BIN, firmware_dir / "ch592_ota.bin")
    shutil.copy2(APP_JSON, firmware_dir / "ch592_ota.bin.json")
    full_hex = PROJECT_ROOT / "obj" / "Broadcaster_full.hex"
    if full_hex.exists():
        # JumpIAP + APP + IAP，用于 WCH-LinkE 首次/恢复烧录（只擦代码区）
        shutil.copy2(full_hex, firmware_dir / "Broadcaster_full.hex")

    notes = args.notes or f"ch592_ota {version}"
    comparable = {
        "version": version,
        "file": "ch592_ota.bin",
        "size": len(image),
        "crc32": crc32,
        "sha256": sha256_of(firmware_dir / "ch592_ota.bin"),
        "notes": notes,
    }
    built = datetime.now(UTC).replace(microsecond=0).isoformat()
    manifest_path = firmware_dir / "manifest.json"
    try:
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        previous = {}
    if previous and all(previous.get(key) == value for key, value in comparable.items()):
        built = str(previous.get("built") or built)
    manifest = {
        "version": version,
        "file": "ch592_ota.bin",
        "size": len(image),
        "crc32": crc32,
        "sha256": comparable["sha256"],
        "built": built,
        "notes": notes,
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"published {version} ({len(image)} bytes, {crc32}) -> {firmware_dir}")

    if not args.push:
        return 0

    try:
        stage_and_commit(repo, version, published_paths(repo))
        if legacy_repo is not None:
            sync_legacy_repo(repo, legacy_repo, version)
            try:
                push_and_verify(legacy_repo, args.legacy_remote)
            except PublishError as error:
                raise PublishError(f"旧镜像未对齐，canonical 推送已阻止: {error}") from error

        failures: list[str] = []
        names = _remote_names(repo)
        explicit_remotes = args.remote is not None
        for remote in args.remote or ["origin", "github"]:
            if remote not in names:
                message = f"remote {remote} is not configured"
                if explicit_remotes:
                    failures.append(message)
                    print(f"error: {message}", file=sys.stderr)
                else:
                    print(f"skip {message}")
                continue
            try:
                push_and_verify(repo, remote)
            except PublishError as error:
                failures.append(f"{remote}: {error}")
                print(f"error: {remote}: {error}", file=sys.stderr)
        if failures:
            raise PublishError("部分 canonical 远端未对齐，可重跑补推: " + " | ".join(failures))
    except PublishError as error:
        fail(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
