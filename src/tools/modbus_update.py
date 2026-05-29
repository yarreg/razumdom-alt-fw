#!/usr/bin/env python3
"""Minimal Modbus RTU firmware updater for DDM845R / DDL84R bootloader."""

from __future__ import annotations

import argparse
import struct
import time
from pathlib import Path
from typing import Iterable, List, Optional

from modbus_client import ModbusClient


ENTER_BOOTLOADER = 0xB007
CMD_BEGIN = 1
CMD_ERASE = 2
CMD_WRITE_CHUNK = 3
CMD_VERIFY = 4
CMD_RUN_APP = 5

HR_UPDATE_COMMAND = 60000
HR_UPDATE_STATUS = 60001
HR_IMAGE_SIZE_HI = 60002
HR_IMAGE_SIZE_LO = 60003
HR_IMAGE_CRC_HI = 60004
HR_IMAGE_CRC_LO = 60005
HR_IMAGE_VERSION_HI = 60006
HR_IMAGE_VERSION_LO = 60007
HR_IMAGE_OFFSET_HI = 60010
HR_IMAGE_OFFSET_LO = 60011
HR_CHUNK_WORD_COUNT = 60012
HR_CHUNK_WORDS = 60100
MAX_CHUNK_BYTES = 240
FW_IMAGE_MAGIC = 0x44504444
FW_HEADER_VERSION = 1
FW_IMAGE_HEADER_OFFSET = 0x180
# Fields: magic, header_version, image_size, image_crc32, build_version, flags, device_id
HEADER_FMT = "<IIIIIII"

DEVICE_NAMES = {1: "DDM845R", 2: "DDL84R"}

UPDATE_STATUS_IDLE = 0
UPDATE_STATUS_READY = 1
UPDATE_STATUS_ERASING = 2
UPDATE_STATUS_WRITING = 3
UPDATE_STATUS_VERIFYING = 4
UPDATE_STATUS_VALID = 5
UPDATE_ERROR_MIN = 100


def read_image_metadata(image: bytes) -> tuple[int, int, int, int]:
    header_len = struct.calcsize(HEADER_FMT)
    if len(image) < FW_IMAGE_HEADER_OFFSET + header_len:
        raise ValueError("image is too small or has no valid image header")
    magic, header_version, image_size, image_crc32, build_version, _flags, device_id = struct.unpack_from(
        HEADER_FMT, image, FW_IMAGE_HEADER_OFFSET
    )
    if magic != FW_IMAGE_MAGIC or header_version != FW_HEADER_VERSION:
        raise ValueError("invalid image header; run make app to patch app.bin")
    if image_size != len(image):
        raise ValueError(f"image_size header mismatch: header={image_size}, file={len(image)}")
    return image_size, image_crc32, build_version, device_id


def words_from_bytes(data: bytes) -> List[int]:
    if len(data) % 2:
        data += b"\xff"
    return [int.from_bytes(data[i : i + 2], "big") for i in range(0, len(data), 2)]


def wait_mode(client: ModbusClient, mode: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    last_error: Optional[Exception] = None
    while time.monotonic() < deadline:
        try:
            regs = client.read_holding(9000, 3)
            if regs[2] == mode:
                return
        except Exception as exc:  # noqa: BLE001
            last_error = exc
        time.sleep(0.2)
    raise TimeoutError(f"device did not enter mode {mode}; last error: {last_error}")


def set_u32(client: ModbusClient, address_hi: int, value: int) -> None:
    client.write_multiple(address_hi, [(value >> 16) & 0xFFFF, value & 0xFFFF])


def command(client: ModbusClient, value: int, timeout: float = 10.0) -> int:
    client.write_single(HR_UPDATE_COMMAND, value)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = client.read_holding(HR_UPDATE_STATUS, 1)[0]
        if status >= UPDATE_ERROR_MIN:
            return status
        if value == CMD_BEGIN and status == UPDATE_STATUS_READY:
            return status
        if value == CMD_ERASE and status == UPDATE_STATUS_READY:
            return status
        if value == CMD_WRITE_CHUNK and status == UPDATE_STATUS_READY:
            return status
        if value == CMD_VERIFY and status == UPDATE_STATUS_VALID:
            return status
        if value == CMD_RUN_APP and status in (UPDATE_STATUS_IDLE, UPDATE_STATUS_VALID):
            return status
        time.sleep(0.1)
    raise TimeoutError(f"update command {value} timed out")


def write_chunk(client: ModbusClient, offset: int, data: bytes) -> None:
    set_u32(client, HR_IMAGE_OFFSET_HI, offset)
    words = words_from_bytes(data)
    client.write_single(HR_CHUNK_WORD_COUNT, len(words))
    client.write_multiple(HR_CHUNK_WORDS, words)
    status = command(client, CMD_WRITE_CHUNK, timeout=5.0)
    if status != UPDATE_STATUS_READY:
        raise RuntimeError(f"chunk write failed at offset {offset}: status {status}")


def update(args: argparse.Namespace) -> None:
    image = Path(args.image).read_bytes()
    image_size, crc, header_build_version, device_id = read_image_metadata(image)
    device_name = DEVICE_NAMES.get(device_id, f"unknown({device_id})")
    print(f"image: {len(image)} bytes, device={device_name}, build=0x{header_build_version:08x}")
    build_version = header_build_version if args.build_version is None else args.build_version
    boot_baud = args.boot_baud if args.boot_baud is not None else args.baud
    boot_parity = args.boot_parity if args.boot_parity is not None else args.parity
    boot_stop_bits = args.boot_stop_bits if args.boot_stop_bits is not None else args.stop_bits
    fallback_boot_baud = args.boot_baud if args.boot_baud is not None else 57600
    fallback_boot_parity = args.boot_parity if args.boot_parity is not None else "N"
    fallback_boot_stop_bits = args.boot_stop_bits if args.boot_stop_bits is not None else 2

    client = ModbusClient(args.port, args.slave, args.baud, args.timeout, args.parity, args.stop_bits)
    try:
        try:
            version = client.read_holding(9000, 3)
        except Exception:  # noqa: BLE001
            client.close()
            client = ModbusClient(
                args.port,
                args.slave,
                fallback_boot_baud,
                args.timeout,
                fallback_boot_parity,
                fallback_boot_stop_bits,
            )
            version = client.read_holding(9000, 3)
        print(f"device: app=0x{version[0]:04x} boot=0x{version[1]:04x} mode={version[2]}")
        if version[2] == 0:
            client.write_single(HR_UPDATE_COMMAND, ENTER_BOOTLOADER)
            client.close()
            client = ModbusClient(args.port, args.slave, boot_baud, args.timeout, boot_parity, boot_stop_bits)
            wait_mode(client, 1, args.reboot_timeout)
        elif version[2] != 1:
            raise RuntimeError(f"unknown device mode: {version[2]}")

        set_u32(client, HR_IMAGE_SIZE_HI, image_size)
        set_u32(client, HR_IMAGE_CRC_HI, crc)
        set_u32(client, HR_IMAGE_VERSION_HI, build_version)
        status = command(client, CMD_BEGIN, timeout=2.0)
        if status != UPDATE_STATUS_READY:
            raise RuntimeError(f"begin failed: status {status}")
        status = command(client, CMD_ERASE, timeout=args.erase_timeout)
        if status != UPDATE_STATUS_READY:
            raise RuntimeError(f"erase failed: status {status}")

        for offset in range(0, len(image), MAX_CHUNK_BYTES):
            chunk = image[offset : offset + MAX_CHUNK_BYTES]
            write_chunk(client, offset, chunk)
            print(f"\rwritten {min(offset + len(chunk), len(image))}/{len(image)} bytes", end="", flush=True)
        print()

        status = command(client, CMD_VERIFY, timeout=10.0)
        if status != UPDATE_STATUS_VALID:
            raise RuntimeError(f"verify failed: status {status}")
        client.write_single(HR_UPDATE_COMMAND, CMD_RUN_APP)
        wait_mode(client, 0, args.reboot_timeout)
        version = client.read_holding(9000, 3)
        print(f"done: app=0x{version[0]:04x} boot=0x{version[1]:04x} mode={version[2]}")
    finally:
        client.close()


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--slave", type=int, default=34, help="Modbus slave address used by app and passed to bootloader")
    parser.add_argument("--baud", type=int, default=57600, help="application Modbus baudrate")
    parser.add_argument("--parity", choices=["N", "E", "O"], default="N", help="application Modbus parity")
    parser.add_argument("--stop-bits", type=int, choices=[1, 2], default=2, help="application Modbus stop bits")
    parser.add_argument("--boot-baud", type=int, default=None, help="override bootloader Modbus baudrate")
    parser.add_argument("--boot-parity", choices=["N", "E", "O"], default=None, help="override bootloader Modbus parity")
    parser.add_argument("--boot-stop-bits", type=int, choices=[1, 2], default=None, help="override bootloader Modbus stop bits")
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--reboot-timeout", type=float, default=10.0)
    parser.add_argument("--erase-timeout", type=float, default=20.0)
    parser.add_argument("--build-version", type=lambda s: int(s, 0), default=None)
    parser.add_argument("image")
    args = parser.parse_args(list(argv) if argv is not None else None)
    update(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
