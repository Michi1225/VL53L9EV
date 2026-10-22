# Magnecko Sense project wiki

Firmware, hardware, protocol, and desktop-tool documentation for the STM32N657
sensor board. This folder is the maintained home of the project guides.

**[Full table of contents](SUMMARY.md)**

For a printable reference, use the [LaTeX User Guide and Reference Manual](../manual/README.md).
The [board datasheet](../datasheet/README.md) gives a compact specification overview.

## Start here

| Task | Guide |
| --- | --- |
| Set up the toolchain, build, flash, or debug | [Build, flash, and debug](getting-started/build-flash-debug.md) |
| Inspect a new board and record hardware tests | [Board bring-up and testing](hardware/bring-up-and-testing.md) |
| Interpret boot, initialization and ranging LED patterns | [Status LED](firmware/status-led.md) |
| Check programming addresses and reserved regions | [Current flash layout](hardware/flash-layout.md) |
| View depth, IMU, parameters, and CPU usage | [Device Monitor](tools/device-monitor.md) |
| Upload a new application and understand rollback | [Firmware updates and A/B recovery](firmware/firmware-updates.md) |
| Change stored identity and inspect build provenance | [Parameters and build metadata](firmware/parameters.md) |
| Implement a client or decode packets | [Wire protocol](protocols/wire-protocol.md) |
| Diagnose throughput, frame rate, or sensor faults | [Streaming and diagnostics](tools/streaming-and-diagnostics.md) |
| Record scans and reconstruct a point cloud | [Scene reconstruction](tools/scene-reconstruction.md) |

## Reading paths

For first use, read **build/flash/debug → current flash layout → Device Monitor**.
For firmware maintenance, read **parameters → firmware updates → wire protocol**.
For board validation, use **bring-up/testing → streaming diagnostics → reconstruction**.

Run shell and Python examples from the **repository root**, even when the guide
is in a nested wiki folder. Source files, scripts, configuration files, and
recordings keep their existing repository locations.

## Documentation structure

```text
wiki/
├── README.md                         Start here
├── SUMMARY.md                        Complete contents
├── getting-started/
│   └── build-flash-debug.md           Tools, builds, SWD, SRAM, VS Code
├── hardware/
│   ├── flash-layout.md               Current programming layout
│   ├── electrical-ratings.md         Shared electrical rating tables
│   └── bring-up-and-testing.md        Board checklist and acceptance plan
├── firmware/
│   ├── parameters.md                 Identity, JSON, image provenance
│   ├── status-led.md                 Boot and sensor lifecycle patterns
│   └── firmware-updates.md            USB/Ethernet DFU and A/B recovery
├── protocols/
│   └── wire-protocol.md               Commands and binary packet formats
├── tools/
│   ├── device-monitor.md             Desktop UI and CPU profiler
│   ├── streaming-and-diagnostics.md   Streaming, timing, faults
│   └── scene-reconstruction.md        Recording, mounting, reconstruction
└── reference/
    ├── flash-layout-proposal.md       Original design proposal
    └── dependencies.md               Library documentation and source map
```

## Current connection endpoints

| Interface | Endpoint | Purpose |
| --- | --- | --- |
| Ethernet | `192.168.8.20:50002` | Sensor streaming and sensor commands |
| Ethernet | `192.168.8.20:50003` | Application firmware updates and update status |
| Ethernet | TCP port `50000` | Legacy echo service |
| USB | CDC serial device, such as `/dev/ttyACM0` | Streaming, commands, and application updates |

The firmware update service requires the updated FSBL and a DFU-capable baseline
application. See its [first-installation instructions](firmware/firmware-updates.md#first-installation).

## Maintaining the wiki

Each guide has its own section contents and links back to this index. Keep the
[full contents](SUMMARY.md) in sync when adding or renaming pages, and use
relative links so the documentation works in a local checkout and on GitHub.
After edits, run `python3 scripts/build_docs.py` from the repository root to
refresh the LaTeX manual and shared datasheet ratings, then run
`python3 scripts/build_docs.py --check`. Add new technical pages to
`manual/sources.json`. See the [manual build instructions](../manual/README.md).
The previous documentation locations contain links to their new home.

The [original flash-layout proposal](reference/flash-layout-proposal.md) is
retained as design history. Use the [current flash layout](hardware/flash-layout.md)
and update guide for the implemented parameter and DFU metadata locations.
Unchecked bring-up items are planned tests, not evidence of completed validation.
Vendor and submodule documentation remains beside its source and is linked from
the [dependency reference](reference/dependencies.md).
