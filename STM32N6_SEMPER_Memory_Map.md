# STM32N6 + S28HS512T External Flash Memory Map

This document defines the intended external-flash layout for the custom STM32N657 board using an Infineon S28HS512T (512 Mbit / 64 MiB) connected to **XSPI1** through **XSPIM Port 2**.

The purpose of this layout is to support:

- STM32N6 BootROM boot from the second FSBL location
- use of the S28HS512T hybrid 4 KiB parameter sectors for persistent configuration
- a stable FSBL
- two application slots for future A/B firmware updates (for example FoE)
- additional space for models, logs, assets, or future features

---

## 1. Address conventions

The S28HS512T has a capacity of:

```text
64 MiB = 0x04000000 bytes
```

On STM32N657, the XSPI1 memory-mapped bank starts at:

```text
XSPI1_BASE = 0x90000000
```

Therefore:

```text
Flash offset 0x00000000 -> CPU address 0x90000000
Flash offset 0x00100000 -> CPU address 0x90100000
...
Flash offset 0x03FFFFFF -> CPU address 0x93FFFFFF
```

The ExtMemLoader should advertise the complete device as:

```c
#define STM32EXTLOADER_DEVICE_ADDR  0x90000000U
#define STM32EXTLOADER_DEVICE_SIZE  0x04000000U
```

CubeProgrammer addresses are absolute addresses in this range.  
The SEMPER driver itself should normally operate on flash-relative offsets.

A safe address conversion is therefore:

```c
if ((Address < STM32EXTLOADER_DEVICE_ADDR) ||
    (Address >= (STM32EXTLOADER_DEVICE_ADDR + STM32EXTLOADER_DEVICE_SIZE)))
{
    return MEM_FAIL;
}

uint32_t flash_offset = Address - STM32EXTLOADER_DEVICE_ADDR;
```

Do **not** use only:

```c
Address & 0x03FFFFFFU
```

because invalid addresses would silently wrap into the flash.

---

## 2. Physical erase geometry

The intended device configuration keeps the factory-style **bottom hybrid sector architecture**.

```text
Flash offset
0x00000000 ┌─────────────────────────────────────────────┐
           │ 32 × 4 KiB parameter sectors               │
0x00020000 ├─────────────────────────────────────────────┤
           │ 1 × 128 KiB transition region              │
0x00040000 ├─────────────────────────────────────────────┤
           │ 255 × 256 KiB main sectors                 │
           │                                             │
0x04000000 └─────────────────────────────────────────────┘
```

Geometry:

| Region | Offset range | Count | Logical size |
|---|---:|---:|---:|
| Parameter sectors | `0x00000000` – `0x0001FFFF` | 32 | 4 KiB |
| Transition region | `0x00020000` – `0x0003FFFF` | 1 | 128 KiB |
| Main sectors | `0x00040000` – `0x03FFFFFF` | 255 | 256 KiB |

Total:

```text
32 × 4 KiB
+ 1 × 128 KiB
+ 255 × 256 KiB
= 64 MiB
```

The ExtMemLoader `StorageInfo` sector description must match this geometry.

---

## 3. Proposed logical flash layout

```text
Flash offset       CPU address       Purpose
──────────────────────────────────────────────────────────────────

0x00000000         0x90000000
    │
    │  4 KiB       GUARD / INVALID FSBL1
    │
0x00001000         0x90001000
    │
    │  124 KiB     PARAMETER STORAGE
    │               - factory identity
    │               - serial number
    │               - MAC address
    │               - calibration
    │               - IP/network configuration
    │               - boot/update state
    │
0x00020000         0x90020000
    │
    │  128 KiB     RESERVED TRANSITION REGION
    │
0x00040000         0x90040000
    │
    │  512 KiB     FSBL2 SLOT
    │
0x000C0000         0x900C0000
    │
    │  256 KiB     RESERVED
    │
0x00100000         0x90100000
    │
    │  8 MiB       APPLICATION SLOT A
    │
0x00900000         0x90900000
    │
    │  8 MiB       APPLICATION SLOT B
    │
0x01100000         0x91100000
    │
    │  47 MiB      FUTURE / DATA / MODELS / LOGGING
    │
0x04000000         0x94000000 (exclusive)
```

---

## 4. Guard sector / FSBL1

The STM32N6 BootROM searches serial NOR for:

```text
FSBL1 at flash offset 0x00000000
FSBL2 at flash offset 0x00040000
```

This design intentionally uses **FSBL2 as the primary FSBL** so that the hybrid parameter-sector region can be used for device data.

The first 4 KiB sector is therefore reserved:

```text
0x00000000 – 0x00000FFF
```

Recommended invariant:

> This sector must never contain a valid STM32 FSBL image header.

The simplest policy is to leave it erased (`0xFF`) and never use it for configuration data.

This ensures that the BootROM rejects FSBL1 and proceeds to the valid FSBL stored at `0x00040000`.

---

## 5. Parameter sector allocation

The remaining 31 small sectors provide:

```text
31 × 4 KiB = 124 KiB
```

A suggested initial allocation is:

| Offset | Size | Purpose |
|---:|---:|---|
| `0x00001000` | 4 KiB | Factory information A |
| `0x00002000` | 4 KiB | Factory information B / backup |
| `0x00003000` | 4 KiB | Device configuration A |
| `0x00004000` | 4 KiB | Device configuration B |
| `0x00005000` | 4 KiB | Firmware/update state A |
| `0x00006000` | 4 KiB | Firmware/update state B |
| `0x00007000` – `0x0001FFFF` | 100 KiB | Reserved / future parameters |

### Factory information

Typical contents:

- board serial number
- hardware revision
- production information
- MAC address
- calibration constants
- manufacturing/test data

Factory information should normally be treated as read-only by the application.

### Device configuration

Typical contents:

- DHCP/static-IP selection
- static IP address
- subnet mask
- gateway
- hostname
- EtherCAT/Ethernet settings
- sensor settings
- user-configurable behavior

Use two sectors (A/B) with:

- format/version field
- generation counter
- payload length
- CRC

Update the inactive copy first, verify it, then make it current. This provides power-loss-safe configuration updates.

### Firmware/update state

Reserve another A/B pair for future firmware-update metadata:

- confirmed application slot
- pending application slot
- update counter
- image version
- image hash
- boot-attempt state
- rollback state
- CRC

This can later support FoE-based A/B updates.

---

## 6. FSBL region

The real FSBL is stored at the BootROM FSBL2 location:

```text
Flash offset : 0x00040000
CPU address  : 0x90040000
```

Reserve 512 KiB:

```text
0x00040000 – 0x000BFFFF
```

This matches the STM32N6 BootROM maximum FSBL-image size and consumes exactly two 256 KiB main sectors.

The following sector is kept reserved:

```text
0x000C0000 – 0x000FFFFF
```

This maintains a clean 1 MiB boundary before Application Slot A.

The FSBL should be considered infrastructure and updated only when necessary.

---

## 7. Application slots

### Application A

```text
Flash offset : 0x00100000
CPU address  : 0x90100000
Size         : 0x00800000 (8 MiB)
End          : 0x008FFFFF
```

### Application B

```text
Flash offset : 0x00900000
CPU address  : 0x90900000
Size         : 0x00800000 (8 MiB)
End          : 0x010FFFFF
```

The large slots are intentional. An LRUN application must fit into available SRAM when executed, so the flash slots are considerably larger than is currently required. The additional space allows future image packaging, metadata, AI/model data, and firmware growth without changing the partition table.

For the initial firmware, the FSBL can continue to load Slot A from:

```text
LRUN source offset = 0x00100000
```

A future A/B-aware FSBL can choose dynamically between:

```text
Slot A = 0x00100000
Slot B = 0x00900000
```

based on the update-state parameter sectors.

---

## 8. Remaining flash

After the two 8 MiB application slots:

```text
0x01100000 – 0x03FFFFFF
```

remains available.

Size:

```text
47 MiB
```

Possible future uses:

- neural-network models
- lookup/calibration datasets
- event or diagnostic logging
- crash dumps
- web-interface assets
- sensor recordings
- additional firmware images
- factory test data
- reserved expansion space

Do not assign this region permanently until there is a concrete need.

---

## 9. ExtMemLoader configuration

The ExtMemLoader does **not** enforce the semantic partition table above. It only exposes the physical external memory to STM32CubeProgrammer.

For the custom board using XSPI1, configure:

```text
External loader start address : 0x90000000
External loader end address   : 0x93FFFFFF
Device size                   : 0x04000000
Page size                     : 0x00000100
Erase value                   : 0xFF
```

In CubeMX these correspond to the ExtMemLoader parameters, and the generated configuration should contain:

```c
#define STM32EXTLOADER_DEVICE_ADDR                0x90000000U
#define STM32EXTLOADER_DEVICE_SIZE                0x04000000U

#define STM32EXTLOADER_DEVICE_PAGE_SIZE           0x00000100U
#define STM32EXTLOADER_DEVICE_INITIAL_CONTENT     0xFFU

#define STM32EXTLOADER_DEVICE_4K_SECTOR_NUMBERS   0x20U
#define STM32EXTLOADER_DEVICE_4K_SECTOR_SIZE      0x1000U

#define STM32EXTLOADER_DEVICE_128K_SECTOR_NUMBERS 0x1U
#define STM32EXTLOADER_DEVICE_128K_SECTOR_SIZE    0x20000U

#define STM32EXTLOADER_DEVICE_256K_SECTOR_NUMBERS 0xFFU
#define STM32EXTLOADER_DEVICE_256K_SECTOR_SIZE    0x40000U
```

The `StorageInfo` structure in the ExtMemLoader is what STM32CubeProgrammer reads to discover:

- memory start address
- total size
- page size
- erase value
- erase-region geometry

The current STM32N6570-DK example advertises `0x70000000` because it uses XSPI2.  
For this custom board, SEMPER is on XSPI1, therefore the advertised base must be `0x90000000`.

---

## 10. ExtMemLoader address translation

STM32CubeProgrammer calls the loader using absolute external-memory addresses such as:

```text
0x90040000
0x90100000
0x90900000
```

The low-level SEMPER driver should receive flash-relative offsets:

```text
0x00040000
0x00100000
0x00900000
```

Therefore the memory wrapper should translate:

```c
static bool ExtMemLoader_AddressToOffset(uint32_t address,
                                         uint32_t size,
                                         uint32_t *offset)
{
    const uint32_t base = STM32EXTLOADER_DEVICE_ADDR;
    const uint32_t flash_size = STM32EXTLOADER_DEVICE_SIZE;

    if (offset == NULL)
    {
        return false;
    }

    if (address < base)
    {
        return false;
    }

    uint32_t local = address - base;

    if (local >= flash_size)
    {
        return false;
    }

    if (size > (flash_size - local))
    {
        return false;
    }

    *offset = local;
    return true;
}
```

This is preferred over masking the address.

---

## 11. Important erase/update rules

1. Never mass-erase the flash during a normal firmware update.
2. Never erase the guard sector at `0x00000000` as part of an application update.
3. Do not erase factory-information sectors during normal operation.
4. Update device configuration using A/B sectors.
5. For future FoE updates, erase/program only the inactive application slot.
6. Verify an application image before marking it pending.
7. Keep the currently confirmed application until the new application has successfully booted and confirmed itself.
8. Treat the FSBL as separately updateable infrastructure.
9. Any FSBL update procedure needs its own power-loss/recovery strategy because this design intentionally does not use FSBL1 as a redundant FSBL copy.

---

## 12. Summary

```text
0x00000000  Guard / invalid FSBL1             4 KiB
0x00001000  Parameter storage                 124 KiB
0x00020000  Reserved transition region        128 KiB
0x00040000  FSBL2                             512 KiB
0x000C0000  Reserved                          256 KiB
0x00100000  Application A                     8 MiB
0x00900000  Application B                     8 MiB
0x01100000  Future/data/models/logs           47 MiB
0x04000000  End of flash
```

Memory-mapped through XSPI1:

```text
0x90000000 – 0x93FFFFFF
```
