#!/usr/bin/env python3
"""Standalone WCH OTA client for CH592 (test tool, no Home Assistant).

Install the only dependency:
    py -m pip install bleak

Put the device into OTA mode (hold B for 3 seconds), then:
    py tools/ch592_ota_client.py --address AA:BB:CC:DD:EE:FF --file obj/Broadcaster_app.bin
"""

from __future__ import annotations

import argparse
import asyncio
import sys
from pathlib import Path

from bleak import BleakClient

OTA_CHAR = "0000fee1-0000-1000-8000-00805f9b34fb"
IMAGE_A_START = 0x1000
IMAGE_SIZE = 216 * 1024
OTA_IMAGE_MAGIC = 0x35415347
CMD_PROM, CMD_ERASE, CMD_VERIFY, CMD_END, CMD_INFO = 0x80, 0x81, 0x82, 0x83, 0x84


def le16(value: int) -> bytes:
    return bytes((value & 0xFF, (value >> 8) & 0xFF))


async def read_response(client: BleakClient, char, timeout: float) -> bytes:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while loop.time() < deadline:
        data = await client.read_gatt_char(char)
        if data:
            return bytes(data)
        await asyncio.sleep(0.05)
    raise TimeoutError("device did not respond")


async def read_status(client: BleakClient, char, timeout: float = 20) -> None:
    response = await read_response(client, char, timeout)
    if len(response) < 2:
        raise RuntimeError(f"bad response: {response.hex()}")
    if response[0] != 0:
        raise RuntimeError(f"device status 0x{response[0]:02X}")


async def upload(client: BleakClient, image: bytes, chunk: int, verify: bool) -> None:
    char = None
    for service in client.services:
        for candidate in service.characteristics:
            if candidate.uuid.lower() == OTA_CHAR:
                char = candidate
                break
    if char is None:
        raise RuntimeError("OTA characteristic 0xFEE1 not found")

    await client.write_gatt_char(char, bytes((CMD_INFO, 0)), response=True)
    info = await read_response(client, char, 20)
    if len(info) < 9:
        raise RuntimeError(f"bad info response: {info.hex()}")
    image_size = int.from_bytes(info[1:5], "little")
    block_size = int.from_bytes(info[5:7], "little")
    chip_id = int.from_bytes(info[7:9], "little")
    print(
        f"INFO: image_flag={info[0]:#04x} image_size={image_size} "
        f"block_size={block_size} chip_id={chip_id:#06x}"
    )
    if image_size != IMAGE_SIZE:
        raise RuntimeError(f"unexpected image size {image_size}, expected {IMAGE_SIZE}")
    if block_size == 0:
        raise RuntimeError("invalid block size 0 reported by device")

    blocks = (image_size + block_size - 1) // block_size
    addr16 = IMAGE_A_START >> 4
    print(f"ERASE: {blocks} blocks x {block_size} bytes")
    await client.write_gatt_char(
        char, bytes((CMD_ERASE, 0)) + le16(addr16) + le16(blocks), response=True
    )
    await read_status(client, char, timeout=60)

    total = len(image)
    for offset in range(0, total, chunk):
        data = image[offset : offset + chunk]
        if len(data) % 4:
            data += b"\xFF" * (4 - len(data) % 4)
        addr = (IMAGE_A_START + offset) >> 4
        payload = bytes((CMD_PROM, len(data))) + le16(addr) + data
        await client.write_gatt_char(char, payload, response=True)
        await read_status(client, char)
        done = min(offset + len(data), total)
        print(f"\rPROGRAM: {done}/{total} ({done * 100 // total}%)", end="", flush=True)
    print()

    if verify:
        for offset in range(0, total, chunk):
            data = image[offset : offset + chunk]
            if len(data) % 4:
                data += b"\xFF" * (4 - len(data) % 4)
            addr = (IMAGE_A_START + offset) >> 4
            payload = bytes((CMD_VERIFY, len(data))) + le16(addr) + data
            await client.write_gatt_char(char, payload, response=True)
            await read_status(client, char, timeout=60)
            done = min(offset + len(data), total)
            print(f"\rVERIFY: {done}/{total} ({done * 100 // total}%)", end="", flush=True)
        print()

    print("END: device will reboot and run the new image")
    try:
        await client.write_gatt_char(char, bytes((CMD_END, 0)), response=True)
    except Exception:  # noqa: BLE001 - reset can drop the link
        pass


async def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="BLE address, e.g. AA:BB:CC:DD:EE:FF")
    parser.add_argument("--file", required=True, help="APP image (.bin from make_ota_images.py)")
    parser.add_argument("--chunk", type=int, default=64, help="data bytes per write (default 64)")
    parser.add_argument("--no-verify", action="store_true", help="skip the VERIFY pass")
    args = parser.parse_args()

    if args.chunk < 16 or args.chunk > 240 or args.chunk % 16:
        parser.error("--chunk must be a multiple of 16 between 16 and 240")
    if args.chunk > 64:
        print(
            "warning: chunks above 64 bytes can hang the device in FLASH_ROM_WRITE; "
            "use --chunk 64 (or 32/16) unless you know what you are doing",
            file=sys.stderr,
        )

    image = Path(args.file).read_bytes()
    if not image or len(image) > IMAGE_SIZE - 16:
        print(f"invalid image size: {len(image)}", file=sys.stderr)
        return 2
    if len(image) < 4 or (image[0] & 0x7F) != 0x6F:
        print("file does not look like a CH592 APP image (first word is not JAL)", file=sys.stderr)
        return 2

    # Fill the unused tail of Image B and append the marker checked by IAP.
    padded = bytearray(image)
    padded.extend(b"\xFF" * (IMAGE_SIZE - 16 - len(padded)))
    padded.extend(OTA_IMAGE_MAGIC.to_bytes(4, "little"))
    padded.extend(b"\xFF" * 12)
    image = bytes(padded)

    print(f"Connecting to {args.address} ...")
    async with BleakClient(args.address, timeout=30) as client:
        try:
            mtu = client.mtu_size
        except Exception:  # noqa: BLE001 - some backends do not expose it
            mtu = "?"
        print(f"Connected, MTU={mtu}")
        await upload(client, image, args.chunk, not args.no_verify)
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
