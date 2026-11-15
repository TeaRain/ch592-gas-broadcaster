#!/usr/bin/env python3
"""Merge JumpIAP + app + IAP into a full USB-flash hex and app OTA bin.

Memory map (CH592, 448K code flash):
  0x000000 - 0x000FFF  JumpIAP (4K)   -> jumps to 0x6D000
  0x001000 - 0x036FFF  Image A (216K) -> application (this build)
  0x037000 - 0x06CFFF  Image B (216K) -> OTA download slot
  0x06D000 - 0x06FFFF  IAP (12K)      -> copies B to A
"""
from __future__ import annotations

import argparse
import json
import re
import zlib
from pathlib import Path

JUMP_START, JUMP_END = 0x000000, 0x001000
APP_START, APP_END = 0x001000, 0x037000
IAP_START, IAP_END = 0x06D000, 0x070000


def read_ihex(path: Path) -> dict[int, int]:
    mem: dict[int, int] = {}
    upper = 0
    for lineno, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ValueError(f"{path}:{lineno}: not an Intel HEX record")
        raw = bytes.fromhex(line[1:])
        count, addr, rtype = raw[0], (raw[1] << 8) | raw[2], raw[3]
        data = raw[4 : 4 + count]
        checksum = raw[4 + count]
        if (sum(raw[:-1]) + checksum) & 0xFF:
            raise ValueError(f"{path}:{lineno}: bad checksum")
        if rtype == 0x00:
            for i, value in enumerate(data):
                mem[upper + addr + i] = value
        elif rtype == 0x01:
            break
        elif rtype == 0x02:
            upper = ((data[0] << 8) | data[1]) << 4
        elif rtype == 0x04:
            upper = ((data[0] << 8) | data[1]) << 16
        elif rtype in (0x03, 0x05):
            continue
        else:
            raise ValueError(f"{path}:{lineno}: unsupported record type {rtype}")
    return mem


def _record(addr: int, rtype: int, data: bytes) -> str:
    body = bytes([len(data), (addr >> 8) & 0xFF, addr & 0xFF, rtype]) + data
    return ":" + body.hex().upper() + f"{(-sum(body)) & 0xFF:02X}"


def write_ihex(mem: dict[int, int], path: Path) -> None:
    lines: list[str] = []
    addresses = sorted(mem)
    upper = None
    i = 0
    while i < len(addresses):
        addr = addresses[i]
        chunk = [mem[addr]]
        j = i + 1
        while (
            j < len(addresses)
            and addresses[j] == addr + len(chunk)
            and len(chunk) < 16
            and (addr % 16) + len(chunk) < 16
        ):
            chunk.append(mem[addresses[j]])
            j += 1
        hi = addr >> 16
        if hi != upper:
            upper = hi
            lines.append(_record(0, 0x04, bytes([hi >> 8, hi & 0xFF])))
        lines.append(_record(addr & 0xFFFF, 0x00, bytes(chunk)))
        i = j
    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def range_of(mem: dict[int, int]) -> tuple[int, int]:
    return min(mem), max(mem)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app-hex", default="obj/Broadcaster.hex")
    parser.add_argument("--jump-hex", default="OTA_Image/JumpIAP/jumpiap.hex")
    parser.add_argument("--iap-hex", default="OTA_Image/IAP/iap.hex")
    parser.add_argument("--full-hex", default="obj/Broadcaster_full.hex")
    parser.add_argument("--app-bin", default="obj/Broadcaster_app.bin")
    parser.add_argument("--version-header", default="APP/include/broadcaster.h")
    args = parser.parse_args()

    jump = read_ihex(Path(args.jump_hex))
    app = read_ihex(Path(args.app_hex))
    iap = read_ihex(Path(args.iap_hex))

    for name, mem, start, end in (
        ("JumpIAP", jump, JUMP_START, JUMP_END),
        ("App", app, APP_START, APP_END),
        ("IAP", iap, IAP_START, IAP_END),
    ):
        lo, hi = range_of(mem)
        if lo < start or hi >= end:
            raise SystemExit(f"{name} out of range: 0x{lo:05X}-0x{hi:05X}, expected 0x{start:05X}-0x{end - 1:05X}")

    full: dict[int, int] = {}
    full.update(jump)
    full.update(app)
    full.update(iap)
    write_ihex(full, Path(args.full_hex))

    app_lo, app_hi = range_of(app)
    bin_bytes = bytes(app.get(app_lo + i, 0xFF) for i in range(app_hi - app_lo + 1))
    app_bin = Path(args.app_bin)
    app_bin.write_bytes(bin_bytes)

    version = ""
    try:
        header = Path(args.version_header).read_bytes().decode("gbk", errors="replace")
        match = re.search(r'#define\s+FW_VERSION_STRING\s+"([^"]+)"', header)
        if match:
            version = match.group(1)
    except OSError:
        pass

    meta = {
        "version": version,
        "size": len(bin_bytes),
        "base": f"0x{app_lo:05X}",
        "crc32": f"0x{zlib.crc32(bin_bytes) & 0xFFFFFFFF:08X}",
    }
    app_bin.with_name(app_bin.name + ".json").write_text(
        json.dumps(meta, indent=2) + "\n", encoding="utf-8"
    )

    print(f"JumpIAP : 0x{min(jump):05X}-0x{max(jump):05X}  {len(jump)} bytes")
    print(f"App     : 0x{app_lo:05X}-0x{app_hi:05X}  {len(app)} bytes")
    print(f"IAP     : 0x{min(iap):05X}-0x{max(iap):05X}  {len(iap)} bytes")
    print(f"Full HEX: {args.full_hex}  {len(full)} bytes")
    print(f"App BIN : {args.app_bin}  {len(bin_bytes)} bytes  base=0x{app_lo:05X}  crc32={meta['crc32']}  version={version or '?'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
