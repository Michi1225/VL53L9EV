# Device parameters and build metadata

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

## Contents

- [Programming parameters](#programming-parameters)
- [Replacing editable settings](#replacing-editable-settings)
- [Editable fields](#editable-fields)
- [Generated metadata](#generated-metadata)
- [Timestamps and image provenance](#timestamps-and-image-provenance)
- [Storage layout](#storage-layout)
- [Inspecting stored records](#inspecting-stored-records)
- [Troubleshooting parameter programming](#troubleshooting-parameter-programming)

Editable device settings live in [`config/flash_parameters.json`](../../config/flash_parameters.json) (UTF-8 JSON). Every
`scripts/flash.sh` target now refreshes the parameter block, including individual
FSBL and application flashes and **Program Existing Images** in VS Code.

## Programming parameters

For a complete build and flash:

```sh
./scripts/flash.sh --target all --config Release --build
```

For an application-only update:

```sh
./scripts/flash.sh --target appli-a --config Release --build
```

Before programming, the script reads the connected board's first 16 KiB of
parameters. Existing identity, firmware version labels, calibration and custom
settings are preserved; new fields get defaults from the template. An erased
board starts from the template. The script updates only the selected firmware
slots' build records and verifies every selected image before writing/verifying
the parameters last. A readback or validation failure stops the operation before
any writes. A firmware write failure stops before committing the new parameters;
an interrupted operation can leave partially updated firmware, so rerun it.

## Replacing editable settings

To intentionally replace editable settings, edit the template and run:

```sh
./scripts/flash.sh --target parameters
```

Or provide a device-specific file with a firmware update:

```sh
./scripts/flash.sh --target appli-a --parameters /path/to/settings.json
```

Both commands replace editable settings with the selected file while retaining
other slots' build records from the connected board. A parameters-only update
needs the external loader but no local firmware binaries or manifests. Invalid
stored JSON requires an explicit settings replacement; a failed/short readback
always stops the operation.

## Editable fields

Editable sections:

- `device`: product, PCB identifier, board version, name, description, serial number,
  hardware/PCB/assembly revisions.
- `firmware`: manually maintained version, release number and protocol version.
- `manufacturing`: date, batch and test timestamp.
- `calibration`: confirmed calibration values, initially empty.

Use `null` for unassigned values. These fields are stored and displayed by the
PC app; firmware does not apply them as runtime configuration.

The default product is `Magnecko Sense`, PCB identifier `MGS`, hardware version
`1.0`, and PCB revision `-` (revision 0), giving board version `MGS-1.0-`.
Revisions A and B are written `MGS-1.0A` and `MGS-1.0B`. These are hardware
identifiers; `firmware.version` remains a separate software version.

When changing a board revision, keep `device.hardware_revision`,
`device.pcb_revision`, and `device.board_version` consistent with
[`common/board/board_identity.h`](../../common/board/board_identity.h) and the USB
product string in [`magnecko-sense-fw.ioc`](../../magnecko-sense-fw.ioc).
The shared header supplies the firmware's USB, sensor interface, and external
loader identities. After CubeMX regeneration, keep `USBD_PRODUCT_STRING` mapped
to `MGS_USB_PRODUCT_STRING` in `ux_device_descriptors.h`.

Previously programmed identity values are preserved during ordinary firmware
flashes. To apply the new branding to an existing board, merge the new identity
fields into its device-specific settings file and program it with
`--target parameters --parameters /path/to/settings.json`, retaining its serial
number, calibration, and other per-device values.

## Generated metadata

Generated sections (do not maintain these in the editable file):

- `parameter_build`: `built_at_utc` refreshes on **every parameter build**, even
  standalone `pack_parameters.py` calls without `--target`; `source` names the
  editable JSON file. This timestamp appears under **parameter_build** in the
  Device Monitor's parameter tree and in `build/flash_parameters.json`.
- `repository`: Git commit, description, branch, dirty flag and submodule revisions
  at parameter-generation time.
- `build.fsbl`, `build.application` (slot A), `build.application_b` (slot B): Git
  state at packaging, image packaging timestamp (`image_built_at_utc`), Debug/Release configuration, image
  filename/size/SHA-256, compiler name/version/target, CMake version/generator,
  toolchain filename, CMake options (`DEV_MODE`, `VL53L9_SENSOR_ONLY` where
  applicable), actual compile flags/definitions for `Core/Src/main.c`, and
  preprocessed ThreadX settings. Each selected slot's `built_at_utc` and
  `programmed_at_utc` refresh to the current host UTC time for every flash,
  including when programming the same existing image again. Other slots retain
  their records. Flashing also records each selected slot's address.
- `flash`: parameter-generation timestamp, selected target, updated image labels,
  connection mode and parameter-region address/size. This describes the latest
  completed parameter update; timestamps come from the host clock.

## Timestamps and image provenance

A parameter-only rebuild updates `parameter_build.built_at_utc` without changing
`build.fsbl.built_at_utc`, `build.application.built_at_utc` or slot B's date.
When flashing Appli, FSBL or both, each selected firmware record's `built_at_utc`
refreshes too. `image_built_at_utc` preserves the original build/packaging time
from the verified image manifest. Building parameters does not rebuild firmware.
When preparing a flash operation, `parameter_build.built_at_utc`, the selected
slots' `built_at_utc` / `programmed_at_utc`, and `flash.parameters_generated_at_utc`
share the same UTC timestamp. Firmware is verified before the new parameter
block is committed; a failed firmware write leaves the board's old records intact.

Image build details come from each packaged image's adjacent `.manifest.json`;
flashing existing images retains their original configuration and image provenance
while refreshing the selected slots' record/flash timestamps.
The image name, byte count and SHA-256 must match the manifest. Old manifests
without the expanded build settings require a one-time rebuild using
`scripts/build.sh` or `flash.sh --build`. SRAM `DEV_MODE=ON` application images
must be rebuilt without `--dev` before persistent flashing. Unselected local
images are never used to claim what is installed in other slots. Older on-board
records remain as recorded until those slots are flashed again.

## Storage layout

Layout: address `0x90000000`, reserved size `0x40000` (256 KiB). Schema version 2
uses compact UTF-8 JSON, one NUL byte and `0xFF` padding, still within the existing
16 KiB firmware reader limit. Updating parameters replaces this reserved block;
FSBL starts at `0x90040000`, application A at `0x90100000`, B at `0x90900000`.

## Inspecting stored records

Inspect the generated, readable JSON in `build/flash_parameters.json`; the padded
flash image is `build/flash_parameters.bin`. The PC viewer shows the expanded
sections automatically under **Device & firmware parameters**. Compiler paths,
user/host names, credentials and signing keys are not added to these records.


## Troubleshooting parameter programming

Rebuild the custom external loader before retrying the 256 KiB parameter image:

```sh
./scripts/build.sh --target extmem --config Release
```

Use the newly generated `ExtMemLoader/build/magnecko-sense-fw_ExtMemLoader.stldr`, including
when selecting the loader manually in CubeProgrammer. The first 256 KiB consists
of **32 × 4 KiB + 1 × 128 KiB** sectors. The previous uniform-sector wrapper used
256 KiB erase commands for this hybrid area: it could leave the 4 KiB overlay
unerased and extend into the FSBL at `0x90040000`. The board override now walks
the advertised physical geometry and bounds the inclusive erase range. Loader
data cache is disabled because SWD updates programming buffers between calls.

If an update was already attempted with the old loader, rebuild and program the
full firmware to restore any affected FSBL content, rather than updating only
parameters:

```sh
./scripts/flash.sh --target all --config Release --build
```

Native regressions cover parameter-only boundaries and the full device range.
Actual erase/program/verify still requires checking on the board; a Sector[0]
message by itself does not uniquely identify the hardware failure.

---

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)
