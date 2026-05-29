#!/usr/bin/env python3
"""Patch DDM845R/DDL84R application binary image header."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


FW_IMAGE_MAGIC = 0x44504444
FW_HEADER_VERSION = 1
FW_IMAGE_HEADER_OFFSET = 0x180
# Fields: magic, header_version, image_size, image_crc32, build_version, flags, device_id
HEADER_FMT = "<IIIIIII"


def patch_image(path: Path, build_version: int, device_id: int) -> None:
    image = bytearray(path.read_bytes())
    header_len = struct.calcsize(HEADER_FMT)
    need_len = FW_IMAGE_HEADER_OFFSET + header_len
    if len(image) < need_len:
        image.extend(b"\xff" * (need_len - len(image)))

    image_size = len(image)
    header_without_crc = struct.pack(
        HEADER_FMT,
        FW_IMAGE_MAGIC,
        FW_HEADER_VERSION,
        image_size,
        0,
        build_version & 0xFFFFFFFF,
        0,
        device_id,
    )
    image[FW_IMAGE_HEADER_OFFSET : FW_IMAGE_HEADER_OFFSET + header_len] = header_without_crc
    crc = zlib.crc32(image) & 0xFFFFFFFF
    header = struct.pack(
        HEADER_FMT,
        FW_IMAGE_MAGIC,
        FW_HEADER_VERSION,
        image_size,
        crc,
        build_version & 0xFFFFFFFF,
        0,
        device_id,
    )
    image[FW_IMAGE_HEADER_OFFSET : FW_IMAGE_HEADER_OFFSET + header_len] = header
    path.write_bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-version", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--device-id", type=int, default=1)
    parser.add_argument("image")
    args = parser.parse_args()
    patch_image(Path(args.image), args.build_version, args.device_id)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
