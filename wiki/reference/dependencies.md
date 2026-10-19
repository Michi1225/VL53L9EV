# Library documentation and source map

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)

## Contents

- [Firmware contexts](#firmware-contexts)
- [Project implementation](#project-implementation)
- [Bundled library documentation](#bundled-library-documentation)
- [Desktop tools and tests](#desktop-tools-and-tests)

## Firmware contexts

| Location | Purpose |
| --- | --- |
| [Appli](../../Appli/) | Main ThreadX application, USB, Ethernet, sensors, and control services |
| [FSBL](../../FSBL/) | Bootloader, image validation, SRAM loading, and A/B selection |
| [ExtMemLoader](../../ExtMemLoader/) | CubeProgrammer external flash loader |
| [Drivers](../../Drivers/) | STM32 HAL/LL and CMSIS drivers |
| [Middlewares](../../Middlewares/) | ThreadX, NetX Duo, USBX, USB-PD, and external memory manager |

## Project implementation

| Location | Purpose |
| --- | --- |
| [Firmware update policy](../../common/firmware/) | Shared image validation, flash metadata, and boot policy |
| [SEMPER driver](../../common/s28hs512t/) | S28HS512T SPI/OPI access |
| [Application services](../../Appli/Libs/control/) | Streaming, control, update server, profiling, and parameter reporting |
| [VL53L9 integration](../../Appli/Libs/53L9/) | Sensor application, interface, and firmware library |
| [IMU integration](../../Appli/Libs/IMU/) | LSM6DSV16X acquisition |
| [Ethernet PHY](../../Appli/Libs/PHY/) | ADIN1300 integration |
| [Build and programming scripts](../../scripts/) | Builds, manifests, parameters, SWD programming, and DFU uploader |

## Bundled library documentation

Vendor documents remain with their libraries; the wiki links to them rather
than maintaining independent copies or changing submodule contents.

- LSM6DSV16X register driver:
  [README](../../Appli/Libs/lsm6dsv16x-pid/README.md),
  [release notes](../../Appli/Libs/lsm6dsv16x-pid/Release_Notes.md),
  [contributing](../../Appli/Libs/lsm6dsv16x-pid/CONTRIBUTING.md),
  [security policy](../../Appli/Libs/lsm6dsv16x-pid/SECURITY.md), and
  [code of conduct](../../Appli/Libs/lsm6dsv16x-pid/CODE_OF_CONDUCT.md).
- VL53L9 sensor library:
  [changelog](../../Appli/Libs/53L9/vl53l9/CHANGELOG.md) and
  [license](../../Appli/Libs/53L9/vl53l9/LICENSE.txt).
- VL53L9 integration licenses:
  [application](../../Appli/Libs/53L9/app/LICENSE.txt) and
  [interface](../../Appli/Libs/53L9/interface/LICENSE.txt).

The LSM6DSV16X driver is a Git submodule. Initialize it after cloning if its
files are missing:

```bash
git submodule update --init --recursive
```

## Desktop tools and tests

- [Device Monitor](../../test_scripts/Board_Viewer.py)
- [Device Monitor firmware update worker](../../test_scripts/Firmware_Update.py)
- [USB stream decoder](../../test_scripts/USB_Debug.py)
- [Ethernet viewer](../../test_scripts/Ethernet_Debug.py)
- [Sensor command client](../../test_scripts/Sensor_Control.py)
- [Firmware uploader](../../scripts/firmware_update.py)
- [Scene reconstruction](../../test_scripts/Reconstruct_Scene.py)
- [Test scripts](../../test_scripts/)

Run host regressions from the repository root:

```bash
python3 -m unittest discover -s test_scripts -p 'test_*.py'
```

GUI integration tests require a working display and the dependencies listed in
[Device Monitor](../tools/device-monitor.md#installation). Hardware acceptance
is recorded separately in the [bring-up plan](../hardware/bring-up-and-testing.md)
and [firmware-update validation guide](../firmware/firmware-updates.md#validation).

---

[Wiki home](../README.md) · [Full contents](../SUMMARY.md)
