# Magnecko Sense

Firmware repository: **`magnecko-sense-fw`**. PCB identifier: **MGS**.
Board versions are **`MGS-1.0-`**, **`MGS-1.0A`**, **`MGS-1.0B`**, and so on;
revision 0 is written as `-`.

STM32N657 firmware and desktop tools for the VL53L9 depth sensor and LSM6DSV16X IMU,
with USB CDC and Ethernet streaming, device configuration, CPU profiling, and A/B
firmware updates.

Start with the **[project wiki](wiki/README.md)** or browse the
**[full contents](wiki/SUMMARY.md)**.

- [Build, flash, and debug](wiki/getting-started/build-flash-debug.md)
- [Device Monitor](wiki/tools/device-monitor.md)
- [Firmware updates and recovery](wiki/firmware/firmware-updates.md)
- [Wire protocol](wiki/protocols/wire-protocol.md)
- [Board bring-up and testing](wiki/hardware/bring-up-and-testing.md)
- [Board datasheet (LaTeX source and PDF build instructions)](datasheet/README.md)

## Hardware revision standard

Board identifiers use **`MGS-<MAJOR>.<MINOR><REVISION>`**. Revision 0 is always
written as `-`, followed by `A`, `B`, and so on. Hardware identifiers are separate
from firmware software versions.

| Change | When to use it | Examples |
| --- | --- | --- |
| Revision: `MGS-1.0-` → `MGS-1.0A` | Fix or refine the existing design without intentionally adding functionality or breaking compatibility. The same firmware should ideally work across these revisions; document any required workaround. | Correct traces or resistor values, improve signal integrity or decoupling, adjust placement, or fix power behavior. |
| Minor: `MGS-1.0B` → `MGS-1.1-` | Add capabilities while preserving existing external interfaces and essential functionality. Existing firmware should generally retain its existing functions; using new hardware may require updated firmware. | Add a temperature sensor, synchronization input, trigger output, compatible expansion connector, or compatible flash capacity increase. |
| Major: `MGS-1.1A` → `MGS-2.0-` | Break an established interface or change the architecture incompatibly. Existing firmware and connected hardware cannot be assumed compatible. | Change connector pin assignments or supply requirements, replace the MCU family, or adopt a substantially different sensor interface. |

Classify changes by their purpose and compatibility impact, rather than their
size or component count. For example, adding a companion IC to fix the already
specified USB PD dead-battery behavior, with the same firmware and external
behavior, is a revision change: **`MGS-1.0-` → `MGS-1.0A`**. It restores intended
functionality. A single incompatible connector pin change can require a major
version increment.

Apply these release rules:

1. Give every released PCB layout modification a new identifier. Unreleased
   development edits do not each require a revision increment.
2. Reset the revision to `-` whenever the major or minor number changes:
   `MGS-1.0C` → `MGS-1.1-`, or `MGS-1.1B` → `MGS-2.0-`.
3. Never reuse an assigned identifier, even for an abandoned design.
4. Track assembly variants and BOM revisions separately from PCB versions.
   Different flash populations or electrically equivalent component substitutions
   on the same layout do not by themselves create a new PCB revision. If a variant
   requires a changed layout, assign the appropriate new board identifier.
5. Maintain a hardware compatibility table recording revision-specific quirks,
   available capabilities, assembly variants, and firmware requirements. Revisions
   can differ in actual behavior even when a change is classified as a bug fix.
   Detect optional features using capabilities or the assembly variant, rather
   than assuming a minor version alone guarantees their presence.

Keep the firmware identity in [`common/board/board_identity.h`](common/board/board_identity.h),
the CubeMX USB product string, and the stored hardware fields consistent when
releasing a board. See [device parameters](wiki/firmware/parameters.md#editable-fields)
for the identity fields and how to update an existing device.
