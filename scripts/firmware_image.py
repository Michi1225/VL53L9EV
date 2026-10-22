#!/usr/bin/env python3
"""Validate signed STM32 application images for SWD and host updates."""
import hashlib
import struct

NONE = 0xffffffff


## @brief Validate the signed STM32 image layout, vectors, and checksum, then return its SHA-256.
def validate_image(data):
    if not 0x408 <= len(data) <= 0x80000 or len(data) % 4:
        raise ValueError('Image must be an aligned signed application fitting 512 KiB SRAM')
    word = lambda offset: struct.unpack_from('<I', data, offset)[0]
    if word(0) != 0x324d5453 or word(0x68) != 0x00020300 or word(0x6c) + 0x240 != len(data):
        raise ValueError('Use magnecko-sense-fw_Appli-signed.bin (STM32 header v2.3 with --align)')
    sp, entry = struct.unpack_from('<II', data, 0x400)
    if (not 0x34080000 < sp <= 0x34200000 or sp % 8 or not entry & 1 or
            not 0x34000400 <= (entry & ~1) < 0x34000000 + len(data) or word(0x70) != entry):
        raise ValueError('Image vectors/entry point do not match the SRAM application layout')
    if (sum(data[0x240:]) & NONE) != word(0x64):
        raise ValueError('STM32 payload checksum is invalid')
    return hashlib.sha256(data).digest()


def validate_fsbl(data):
    """Reject application/raw binaries and validate the packaged FSBL SRAM layout."""
    if not 0x408 <= len(data) <= 0x40000 or len(data) % 4:
        raise ValueError('FSBL must be an aligned signed image fitting its 256 KiB code allocation')
    word = lambda offset: struct.unpack_from('<I', data, offset)[0]
    if word(0) != 0x324d5453 or word(0x68) != 0x20300 or word(0x6c) + 0x240 != len(data):
        raise ValueError('Use magnecko-sense-fw_FSBL-trusted.bin (STM32 header v2.3 with --align)')
    sp, entry = struct.unpack_from('<II', data, 0x400)
    if (not 0x341c0000 < sp <= 0x34200000 or sp % 8 or not entry & 1 or
            not 0x34180400 <= (entry & ~1) < 0x34180000 + len(data) or word(0x70) != entry):
        raise ValueError('Image vectors/entry point do not match the FSBL SRAM layout')
    if (sum(data[0x240:]) & NONE) != word(0x64):
        raise ValueError('STM32 FSBL payload checksum is invalid')
    return hashlib.sha256(data).digest()
