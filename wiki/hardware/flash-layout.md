# Current external flash layout

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

## Contents

- [Address conventions](#address-conventions)
- [Programming regions](#programming-regions)
- [Parameters and the boot header](#parameters-and-the-boot-header)
- [A/B updates and metadata](#ab-updates-and-metadata)
- [Erase geometry](#erase-geometry)
- [Implementation references](#implementation-references)

## Address conventions

The S28HS512T is a 64 MiB NOR device connected through XSPI1 and XSPIM port 2.
Its memory-mapped base is `0x90000000`; the exclusive end is `0x94000000`.
The flash driver uses device-relative offsets, while CubeProgrammer uses the
absolute addresses shown below.

## Programming regions

| Purpose | Flash offset | CPU address | Reserved size |
| --- | --- | --- | --- |
| Parameter JSON and padding | `0x00000000` | `0x90000000` | 256 KiB |
| FSBL2 | `0x00040000` | `0x90040000` | 512 KiB |
| Reserved | `0x000C0000` | `0x900C0000` | 256 KiB |
| Application A | `0x00100000` | `0x90100000` | 8 MiB |
| Application B | `0x00900000` | `0x90900000` | 8 MiB |
| Update metadata copy A | `0x01100000` | `0x91100000` | 256 KiB |
| Update metadata copy B | `0x01140000` | `0x91140000` | 256 KiB |
| Future data | `0x01180000` | `0x91180000` | 46.5 MiB |

Application flash slots are larger than the current execution allocation.
An LRUN application image, including its STM32 header and padding, must fit in
512 KiB. FSBL copies it to `0x34000000`; the vector table starts at `0x34000400`.

## Parameters and the boot header

The current parameter packer writes JSON at offset zero, followed by a NUL
terminator and erased-value padding across the first 256 KiB. The application
reads at most the first 16 KiB. JSON is not a valid STM32 FSBL1 header, so the
board uses its FSBL2 image at offset `0x00040000`.

The [original proposal](../reference/flash-layout-proposal.md) described a separate
4 KiB guard followed by individual configuration sectors. That is design history;
the current [parameter workflow](../firmware/parameters.md) replaces the complete
first 256 KiB region. It does not preserve separate metadata sectors inside it.

## A/B updates and metadata

The application updater chooses the inactive application slot. The confirmed
image, parameters, and FSBL are excluded from its erase targets. Two separate
256 KiB sectors hold alternating committed update records, including image
hashes, confirmed/pending slots, trial state, and persistent status flags.

These metadata reservations occupy part of the former future/data region so
that parameter programming cannot erase boot state. See
[firmware updates and A/B recovery](../firmware/firmware-updates.md) for the
commit sequence, watchdog, validation rules, and direct-SWD reprovisioning steps.

## Erase geometry

| Offset range | Physical geometry |
| --- | --- |
| `0x00000000`–`0x0001FFFF` | 32 × 4 KiB parameter sectors |
| `0x00020000`–`0x0003FFFF` | One 128 KiB transition region |
| `0x00040000`–`0x03FFFFFF` | 255 × 256 KiB main sectors |

The external loader must use this hybrid geometry when replacing parameters.
The [parameter troubleshooting guide](../firmware/parameters.md#troubleshooting-parameter-programming)
explains the earlier uniform-erase failure and rebuilding the corrected loader.

## Implementation references

- [Update slot, metadata, and image limits](../../common/firmware/fw_update.h)
- [Update flash-access bounds](../../common/firmware/fw_flash.c)
- [Persistent programming addresses](../../scripts/flash.sh)
- [Parameter packing and size limits](../../scripts/pack_parameters.py)
- [Application SRAM linker layout](../../Appli/STM32N657XX_LRUN.ld)

---

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)
