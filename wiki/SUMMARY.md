# Wiki contents

[Wiki home](README.md)

## Getting started

- [Build, flash, and debug](getting-started/build-flash-debug.md)
  - [Initial setup and required tools](getting-started/build-flash-debug.md#1-initial-setup)
  - [Build outputs](getting-started/build-flash-debug.md#build-outputs)
  - [Persistent flashing](getting-started/build-flash-debug.md#5-flashing-persistent-external-memory)
  - [SRAM development](getting-started/build-flash-debug.md#7-sram-development-mode)
  - [Source-level debugging](getting-started/build-flash-debug.md#9-source-level-debugging-from-sram)
  - [Attach without resetting](getting-started/build-flash-debug.md#10-attach-to-an-already-running-target)
  - [Quick command reference](getting-started/build-flash-debug.md#14-quick-reference)

## Hardware and validation

- [Board datasheet (LaTeX)](../datasheet/README.md)
- [Current flash layout](hardware/flash-layout.md)
- [Board bring-up and system testing](hardware/bring-up-and-testing.md)
  - [Power rails](hardware/bring-up-and-testing.md#3-power-rails)
  - [Clocks, reset, and boot pins](hardware/bring-up-and-testing.md#4-clocks-reset-boot-pins)
  - [External flash](hardware/bring-up-and-testing.md#7-semper-flash)
  - [Boot chain](hardware/bring-up-and-testing.md#9-boot-chain)
  - [Final acceptance](hardware/bring-up-and-testing.md#24-final-acceptance)

## Firmware and persistent state

- [Boot and application status LED](firmware/status-led.md)
- [Device parameters and build metadata](firmware/parameters.md)
  - [Programming](firmware/parameters.md#programming-parameters)
  - [Editable fields](firmware/parameters.md#editable-fields)
  - [Generated metadata](firmware/parameters.md#generated-metadata)
  - [Timestamps and provenance](firmware/parameters.md#timestamps-and-image-provenance)
  - [Troubleshooting](firmware/parameters.md#troubleshooting-parameter-programming)
- [Firmware updates and A/B recovery](firmware/firmware-updates.md)
  - [First installation](firmware/firmware-updates.md#first-installation)
  - [Uploading and checking status](firmware/firmware-updates.md#uploading-and-inspecting-status)
  - [Flash allocation](firmware/firmware-updates.md#flash-allocation-and-limits)
  - [Persistent flags, trial boot, and fallback](firmware/firmware-updates.md#persistent-state-and-boot-behavior)
  - [DFU wire format](firmware/firmware-updates.md#wire-format)
  - [Validation](firmware/firmware-updates.md#validation)

## Protocol reference

- [Magnecko Sense wire protocol](protocols/wire-protocol.md)
  - [Connections and lifecycle](protocols/wire-protocol.md#connections-and-lifecycle)
  - [Common header](protocols/wire-protocol.md#common-16-byte-header)
  - [Sensor commands: CTRL](protocols/wire-protocol.md#commands-ctrl)
  - [Sensor status: TOFS](protocols/wire-protocol.md#sensor-status-and-acknowledgements-tofs)
  - [Depth frames: VL9F](protocols/wire-protocol.md#depth-frames-vl9f)
  - [IMU: IMUF](protocols/wire-protocol.md#imu-imuf)
  - [Parameters: PARM](protocols/wire-protocol.md#devicebuild-parameters-parm)
  - [CPU profiling: PROF](protocols/wire-protocol.md#cpu-profiling-prof)
  - [Backpressure and recovery](protocols/wire-protocol.md#backpressure-and-recovery)

## Desktop tools and data

- [Device Monitor](tools/device-monitor.md)
  - [Installation](tools/device-monitor.md#installation)
  - [Connecting](tools/device-monitor.md#connecting)
  - [Sensor controls](tools/device-monitor.md#sensor-controls)
  - [Firmware updates](tools/device-monitor.md#firmware-updates)
  - [Parameters](tools/device-monitor.md#device-and-firmware-parameters)
  - [CPU profiler](tools/device-monitor.md#cpu-profiler)
- [Streaming and sensor diagnostics](tools/streaming-and-diagnostics.md)
  - [Ethernet streaming](tools/streaming-and-diagnostics.md#ethernet-streaming)
  - [USB streaming](tools/streaming-and-diagnostics.md#usb-streaming)
  - [Backpressure and link recovery](tools/streaming-and-diagnostics.md#backpressure-and-link-recovery)
  - [Sensor fault isolation](tools/streaming-and-diagnostics.md#isolating-sensor-faults)
  - [Frame rate and temperature](tools/streaming-and-diagnostics.md#gaming-mode-frame-rate-and-temperature)
- [Scene reconstruction](tools/scene-reconstruction.md)
  - [Recording](tools/scene-reconstruction.md#recording-a-scan)
  - [Output artifacts](tools/scene-reconstruction.md#output-artifacts)
  - [Replay](tools/scene-reconstruction.md#replaying-a-capture)
  - [Calibration](tools/scene-reconstruction.md#calibration-files)
  - [Coordinate conventions](tools/scene-reconstruction.md#coordinate-conventions)
  - [Board mounting](tools/scene-reconstruction.md#board-mounting-calibration)
  - [Accuracy limits](tools/scene-reconstruction.md#accuracy-limits)

## Design history and dependencies

- [Original flash-layout proposal](reference/flash-layout-proposal.md)
- [Library documentation and source map](reference/dependencies.md)
