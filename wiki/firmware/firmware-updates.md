# Application firmware updates and A/B recovery

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

## Contents

- [First installation](#first-installation)
- [Uploading and inspecting status](#uploading-and-inspecting-status)
- [Flash allocation and limits](#flash-allocation-and-limits)
- [Persistent state and boot behavior](#persistent-state-and-boot-behavior)
- [Wire format](#wire-format)
- [Validation](#validation)

The application accepts updates over USB CDC or a dedicated Ethernet TCP server
on **port 50003**. This is an application protocol, not the USB DFU device class.
The existing sensor/command TCP service remains on port 50002.

## First installation

Install **both the new FSBL and this DFU-capable application** using the normal
SWD workflow, without `--dev`:

```bash
./scripts/flash.sh --target all --config Release --build
```

Boot normally from flash and verify USB/Ethernet operation. On the first boot,
FSBL validates existing STM32 v2.3 images, adopts valid A/B images, and creates
boot metadata. Slot A is preferred for this initial migration. Installing the
DFU-capable baseline is necessary so that rollback also restores update access.
Thereafter normal updates use the uploader, which never programs FSBL.

Generated source integration is confined to existing `USER CODE` sections.
The `.inc` implementations are included once from each project's `main.c` to
preserve CubeMX source lists and regeneration without editing generated code.

## Uploading and inspecting status

Firmware updates are available directly in **Device Monitor → Firmware update**.
Select a packaged application, enter its release identifier, and press Upload
image. The tab shows acknowledged progress, verification, A/B slot status, and
persistent warnings. It can reset a staged image and monitor confirmation or
fallback. USB streaming is paused and resumed automatically. See the
[viewer instructions](../tools/device-monitor.md#firmware-updates) for all controls.

The command-line client remains available for scripts and terminal use.
Use the packaged application binary from a **non-DEV_MODE** build:

```bash
./scripts/build.sh --target appli --config Release
python3 scripts/firmware_update.py --tcp 192.168.8.20 \
  --image Appli/build/magnecko-sense-fw_Appli-signed.bin --version 2
```

For USB, close Device Monitor/other serial owners first:

```bash
python3 scripts/firmware_update.py --usb /dev/ttyACM0 \
  --image Appli/build/magnecko-sense-fw_Appli-signed.bin --version 2
```

The USB uploader requires `pyserial`. `--version` is a uint32 release identifier,
not an anti-rollback counter. The updater selects the inactive slot itself.
The client validates the wrapper/checksum and sends a SHA-256 digest; the board
verifies each programmed chunk and the entire stored image before committing it.
By default the client requests reset after verification. `--no-reset` leaves the
image staged; a later `--reset` starts its trial boot.

After reset, reconnect after roughly ten seconds and inspect status:

```bash
python3 scripts/firmware_update.py --tcp 192.168.8.20
python3 scripts/firmware_update.py --usb /dev/ttyACM0
```

For a failed startup, allow roughly 32 seconds plus boot time for the watchdog
and fallback. The USB device node may change after reset; a `/dev/serial/by-id/`
path is preferable when available. Slot numbers are 0 = A, 1 = B; `4294967295`
means no slot. Successful activation has `running == confirmed`, no pending
slot, and `boot_confirmed` set. A fallback keeps `fallback_warning` set and
records the cause in `last_error`. Warnings remain until explicitly cleared:

```bash
python3 scripts/firmware_update.py --tcp 192.168.8.20 --clear-warnings
python3 scripts/firmware_update.py --tcp 192.168.8.20 --abort
python3 scripts/firmware_update.py --tcp 192.168.8.20 --reset
```

`--abort` cancels an in-progress transfer, not an already verified pending image.
A lost connection aborts an incomplete upload; inactivity for 60 seconds also
aborts it. A power interruption is recorded as an upload failure on next boot.
Uploads restart from the beginning; chunks are not blindly retried or resumed.
If an acknowledgement is lost, inspect status before choosing abort or reset.
Only one USB/TCP session may own an upload. USB telemetry is suppressed during
an upload, and a packet mutex prevents interleaving DFU replies and telemetry.
Existing USB `CTRL` framing continues to work outside DFU operations.

## Flash allocation and limits

All offsets below are relative to XSPI1's `0x90000000` bank.

| Region | Offset | Reserved size |
| --- | --- | --- |
| Application A | `0x00100000` | 8 MiB |
| Application B | `0x00900000` | 8 MiB |
| Update metadata copy A | `0x01100000` | 256 KiB |
| Update metadata copy B | `0x01140000` | 256 KiB |
| Remaining future/data space | `0x01180000` | Until end of flash |

The two metadata sectors are **new reservations in the previous future/data
area**, rather than the older proposed 4 KiB metadata offsets in the parameter
area. `flash.sh` currently replaces the first 256 KiB parameter region, so using
that area would erase boot state during parameter programming. Factory data,
parameters, FSBL and the confirmed application are never DFU erase targets.
The hardware adapter also restricts writes and erases to these explicit ranges.

Although flash slots are 8 MiB, the current LRUN code allocation is 512 KiB.
Uploaded images must be at most `0x80000` bytes including the STM32 v2.3 header
and alignment padding. Vectors are at offset `0x400`; the signed file is copied
to `0x34000000`, with code starting at `0x34000400`. Raw `.bin`/ELF files and
images for other load layouts are rejected.

**SWD reprogramming after A/B metadata exists:** replacing a slot directly does
not update its stored SHA-256 descriptor. To reprovision a baseline with the
legacy programming script, explicitly erase just the two update metadata sectors
using CubeProgrammer and the external loader, then reprogram/boot the baseline.
This discards update history and causes validated-image adoption again. Parameter
programming alone does not require or erase these sectors. `PARM` JSON build
metadata is not rewritten by DFU; current DFU versions/flags come from `DFUS`.

## Persistent state and boot behavior

Each metadata sector holds a 124-byte record: format/version/length, generation,
confirmed slot, pending slot, trial-attempt flag, status flags, last error, and
both slot descriptors (size, release identifier and SHA-256). Integers are LE32.
A CRC32 covers bytes 0–115; offset 116 holds that CRC and offset 120 the final commit
word. The replacement sector is erased/programmed/read back first, and its commit
word is programmed **last**. The previous valid copy remains intact throughout.
The newest valid generation is selected with rollover-aware comparison.

1. `BEGIN` invalidates the inactive descriptor in durable metadata **before**
   erasing that slot. The confirmed slot remains untouched.
2. `DATA` accepts only sequential, aligned, bounded chunks with a matching CRC32;
   each chunk is programmed and read back.
3. `FINISH` rereads the entire image, checks SHA-256, STM32 header/payload checksum,
   file length, stack bounds/alignment and Thumb entry point. Only then are
   `image_uploaded`, `image_verified` and the pending slot committed.
4. FSBL validates the selected image again on every boot. Before jumping to a
   candidate it commits `trial = 1`, starts the independent watchdog, loads SRAM,
   and verifies the copied image's SHA-256.
5. The application confirms the trial only after its dedicated update server and
   USB receive task have run for five seconds, and its loaded image matches the
   descriptor. Connections/cables are not required. A sensor fault does not prevent
   confirmation when the updater itself is available.
6. A reset before confirmation (including watchdog reset after an initialization
   hang or hard fault) makes FSBL reject the candidate, boot the previous image,
   and retain a fallback warning. A failed confirmation metadata write also keeps
   the old image recoverable. After confirmation, the DFU worker refreshes IWDG.
7. A corrupted confirmed image is rejected using its stored SHA-256; FSBL tries
   the other recorded valid image and records a warning. If neither is valid,
   FSBL records `no_valid_image` when possible and stops rather than jumping to
   corrupt code; SWD recovery is then required.

The trial window is approximately 32 seconds at nominal 32 kHz LSI; oscillator
variation affects the exact timeout. A debugger halt during trial can trigger
rollback. Automatic reset/recovery requires normal flash boot mode; with BOOT1
set for development, reset returns to the ROM development flow instead of
autonomously restarting FSBL. Five-second confirmation tests updater availability, not every sensor
or future application function. Runtime failures after confirmation can reset
through IWDG, but do not automatically revoke an already confirmed image unless
its stored image fails validation on the next boot.

Image hashes and checksums detect corruption; the existing unauthenticated USB/
TCP transport model is retained. Despite the packaging filename `-signed.bin`,
the current build uses SigningTool `-nk`; this updater does not authenticate a
publisher or verify a cryptographic signing key. Deploy on trusted interfaces.

## Wire format

The existing common 16-byte header is used: `<4sIIHH>` = magic, request ID,
payload bytes, version, operation. Requests use `DFUC`, nonzero ID, version 1.
Responses use `DFUS`, the matching ID/operation, version 1 and 48 payload bytes.
One request is outstanding per connection. USB multiplexes `DFUC` and `CTRL`
without scanning for magic inside accepted payloads; TCP 50003 accepts `DFUC`.

| Operation | Number | Payload |
| --- | --- | --- |
| Status | 1 | Empty |
| Begin | 2 | `<II32s>`: image size, release identifier, SHA-256 |
| Data | 3 | `<II>`: byte offset, CRC32 of data; followed by 4–512 data bytes |
| Finish | 4 | Empty |
| Abort in-progress upload | 5 | Empty |
| Reset into verified pending image | 6 | Empty |
| Clear sticky warnings | 7 | Empty |

CRC32 uses the standard reflected polynomial `0xEDB88320`, compatible with
Python `zlib.crc32`. Image/chunk sizes are multiples of four. The board picks the
slot; no command accepts a physical flash address. Reset is scheduled 500 ms
after successfully sending its reply. DEV_MODE builds accept status/legacy
commands but reject update mutations and never confirm a flash trial.

`DFUS` payload is `<12I>`:

| Word | Field |
| --- | --- |
| 0 | Result |
| 1 | Persistent flags |
| 2 | Running slot |
| 3 | Confirmed slot |
| 4 | Pending slot |
| 5 | Received bytes in current upload |
| 6 | Total bytes in current upload |
| 7 | Last persistent error |
| 8 | Metadata generation |
| 9 | Slot A release identifier |
| 10 | Slot B release identifier |
| 11 | Maximum chunk data size (512) |

Results: 0 OK, 1 bad request, 2 busy/other owner, 3 flash I/O failure,
4 invalid image, 5 SHA/CRC mismatch, 6 unexpected offset/length, 7 not ready,
8 previous trial failed. The status result also reports startup validation or
missing A/B metadata, so a legacy FSBL cannot silently accept staged updates.

| Flag bit | Meaning |
| --- | --- |
| 0 | Upload in progress |
| 1 | Complete image uploaded |
| 2 | Stored image verified |
| 3 | Candidate boot confirmed |
| 4 | Sticky fallback warning |
| 5 | Sticky upload failure |
| 6 | Sticky no-valid-image warning |
| 7 | Metadata initialized/recovered by image adoption |
| 8 | Sticky aborted upload |

## Validation

Run host tests against the actual portable C policy/parser plus the Python client:

```bash
python3 -m unittest discover -s test_scripts -p test_firmware_update.py -v
python3 -m unittest discover -s test_scripts -p test_viewer_firmware_update.py -v
```

Tests model NOR one-to-zero programming, sequential updates in both directions,
header/vector/checksum/SHA failures, additive-checksum collisions, wrong offsets,
CRC failures, interrupted transfers, unconfirmed boots, corrupted confirmed/
pending images, generation rollover, and interrupted metadata writes/erases.
ASan/UBSan cover the host C harness. Cross-build both Appli and FSBL.
Viewer tests cover progress, exclusive USB ownership and automatic reconnection,
staging, cancellation, lost acknowledgements, confirmation, and fallback. With a
working display they also exercise the actual tab and window-close behavior;
those GUI tests are skipped when a display cannot be opened.

Hardware acceptance remains necessary:

- Provision and boot the new baseline once; inspect initial status.
- Upload A → B over USB, reset, then verify B confirmation and stored flags.
- Upload B → A over TCP, reset, then verify A confirmation.
- Interrupt transfers and power during slot erase/program and metadata commits;
  the confirmed image must still boot and accept a new update.
- Upload a structurally valid test build that deliberately hangs before updater
  startup; verify watchdog reset, previous-image boot, and fallback warning.
- Corrupt the pending image before reset and a confirmed image with an intact
  backup; verify rejection and fallback without executing the corrupted image.
- Disconnect/reconnect USB and Ethernet mid-upload and check ownership, abort
  flags, telemetry recovery and a successful subsequent upload.

---

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)
