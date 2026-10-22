# Magnecko Sense firmware

STM32N657 firmware for the VL53L9 depth sensor and LSM6DSV16X IMU, with
USB CDC and Ethernet streaming, device configuration, CPU profiling, and A/B
firmware updates.

This repository contains the application (`Appli/`), first-stage bootloader
(`FSBL/`), external flash loader (`ExtMemLoader/`), drivers, middleware, shared
firmware, device configuration (`config/`), and build/flash/debug helpers
(`scripts/`).

From this directory:

```sh
git submodule update --init --recursive
./scripts/build.sh --config Debug --target all
```

Project documentation, desktop tools, host tests, recordings, the documentation
generator, and the host firmware uploader live in the parent
[`magnecko-sense` repository](../README.md).
In its checkout, see the [project wiki](../wiki/README.md),
[build/flash/debug guide](../wiki/getting-started/build-flash-debug.md), and
[desktop tools and tests](../test_scripts/).

The future ROS 2 control based SDK is a separate top-level submodule,
`magnecko-sense-sdk/`.
