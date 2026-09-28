# Build, Flash and Debug Workflow

This directory contains the helper scripts used to build, package, flash, and debug the STM32N6 project.

The project contains three firmware contexts:

- `appli` — main application
- `fsbl` — first-stage bootloader
- `extmem` — STM32CubeProgrammer external memory loader
- `all` — build/package all three

The scripts are intended to be called either directly from a terminal or through the VS Code tasks in `.vscode/tasks.json`.

---

## 1. Initial setup

After cloning the repository, make the shell scripts executable:

```bash
chmod +x scripts/*.sh
```

You normally only need to do this once.

The scripts automatically try to locate the STM32Cube GNU toolchain and STM32CubeProgrammer tools from common installation locations, including the STM32Cube VS Code bundles under:

```text
~/.local/share/stm32cube/bundles/
```

The build scripts prefer the STM32Cube GNU toolchain over Ubuntu's generic `/usr/bin/arm-none-eabi-gcc`.

### Required tools

The following tools are required:

```text
cmake
arm-none-eabi-gcc
arm-none-eabi-g++
arm-none-eabi-objcopy
STM32_SigningTool_CLI
STM32_Programmer_CLI
ST-LINK_gdbserver
```

`STM32_SigningTool_CLI` may require installation of the full STM32CubeProgrammer package. The lightweight programmer bundle installed by the VS Code extension may contain `STM32_Programmer_CLI` without the signing tool.

---

## 2. Building

The main build script is:

```text
scripts/build.sh
```

Syntax:

```bash
./scripts/build.sh --config <Debug|Release> --target <appli|fsbl|extmem|all>
```

Examples:

```bash
./scripts/build.sh --config Debug --target appli
./scripts/build.sh --config Debug --target fsbl
./scripts/build.sh --config Debug --target extmem
./scripts/build.sh --config Debug --target all
./scripts/build.sh --config Release --target all
```

### Build outputs

#### Application

Building `appli` produces:

```text
Appli/build/VL53L9_eval_Appli.elf
Appli/build/VL53L9_eval_Appli.bin
Appli/build/VL53L9_eval_Appli-signed.bin
```

The signed application image is the file used for programming external flash.

#### FSBL

Building `fsbl` produces:

```text
FSBL/build/VL53L9_eval_FSBL.elf
FSBL/build/VL53L9_eval_FSBL.bin
FSBL/build/VL53L9_eval_FSBL-trusted.bin
```

The trusted FSBL image is the file used for programming external flash.

#### External memory loader

Building `extmem` produces:

```text
ExtMemLoader/build/VL53L9_eval_ExtMemLoader.elf
ExtMemLoader/build/VL53L9_eval_ExtMemLoader.stldr
```

The `.stldr` file is still an ELF file internally. It is copied from the generated ELF and must not be converted to a raw binary.

---

## 3. Building from VS Code

Use:

```text
Terminal -> Run Build Task
```

or:

```text
Ctrl+Shift+B
```

Select:

```text
Build configuration:
    Debug
    Release
```

and then:

```text
Build target:
    appli
    fsbl
    extmem
    all
```

For normal development, `Debug` is usually the appropriate configuration.

For a complete firmware package, use:

```text
Release + all
```

or:

```text
Debug + all
```

depending on the desired build configuration.

---

## 4. External flash layout

The current external flash programming addresses are:

```text
FSBL:
    0x90040000

Application Slot A:
    0x90100000

Application Slot B:
    0x90900000
```

The custom ExtMemLoader exposes the SEMPER flash through the STM32N6 XSPI1 memory-mapped region.

The current boot flow uses:

```text
BootROM
  -> FSBL2 at 0x90040000
  -> Application Slot A at 0x90100000
```

---

## 5. Flashing persistent external memory

Persistent programming is handled by:

```text
scripts/flash.sh
```

### Build and flash

To rebuild the required images before programming:

```bash
./scripts/flash.sh --target all --config Debug --build
```

Other examples:

```bash
./scripts/flash.sh --target fsbl --config Debug --build
./scripts/flash.sh --target appli-a --config Debug --build
./scripts/flash.sh --target appli-b --config Debug --build
```

Available flash targets are:

```text
fsbl
appli-a
appli-b
all
```

`all` currently programs:

```text
FSBL
Application Slot A
```

### Flash without rebuilding

If the images have already been built:

```bash
./scripts/flash.sh --target all
```

or:

```bash
./scripts/flash.sh --target fsbl
./scripts/flash.sh --target appli-a
./scripts/flash.sh --target appli-b
```

The script automatically uses:

```text
ExtMemLoader/build/VL53L9_eval_ExtMemLoader.stldr
```

with `STM32_Programmer_CLI`.

---

## 6. Flashing from VS Code

Use:

```text
Terminal -> Run Task
```

and select:

```text
Flash: Build + Program
```

This asks for:

```text
Debug / Release
```

and:

```text
all
fsbl
appli-a
appli-b
```

Alternatively, use:

```text
Flash: Program Existing Images
```

to program already-generated images without rebuilding.

---

## 7. SRAM development mode

The STM32N6 can run firmware directly from internal SRAM without programming the external flash.

For this mode:

```text
BOOT1 = 1
```

The firmware is volatile and is lost after reset or power removal.

The development helper is:

```text
scripts/run_dev.sh
```

Supported targets:

```text
appli
fsbl
```

### Build, load into SRAM, run, then detach

Example:

```bash
./scripts/run_dev.sh     --action run     --target appli     --config Debug     --build
```

For the FSBL:

```bash
./scripts/run_dev.sh     --action run     --target fsbl     --config Debug     --build
```

This performs approximately:

```text
build
-> reset target
-> load ELF into SRAM
-> start execution
-> detach debugger
```

The MCU continues running after the debugger disconnects.

---

## 8. SRAM development from VS Code

Use:

```text
Terminal -> Run Task
-> Dev: Load + Run SRAM (detach)
```

Then select:

```text
Debug / Release
```

and:

```text
appli / fsbl
```

This is useful when the firmware should simply run from SRAM without keeping a debugger attached.

---

## 9. Source-level debugging from SRAM

For interactive source-level debugging, use the VS Code Run and Debug panel.

Available configurations:

```text
Dev: Debug Application from SRAM
Dev: Debug FSBL from SRAM
```

These configurations:

```text
build the selected Debug target
-> load the ELF into SRAM
-> load debug symbols
-> run to main
-> keep the debugger attached
```

`BOOT1` must be set to `1` for this development-mode workflow.

---

## 10. Attach to an already-running target

An already-running application can be debugged without intentionally resetting or reloading it.

Use the VS Code Run and Debug configuration:

```text
Attach: Application (no reset)
```

or:

```text
Attach: FSBL (no reset)
```

This loads only the ELF symbols and attaches to the target.

Typical workflow:

```text
1. Load application into SRAM and detach
2. Let the firmware run
3. Later select "Attach: Application (no reset)"
```

The same attach configuration can also be used after a normal standalone boot from external flash, provided debug access is still available.

---

## 11. Development mode vs normal boot

### Development mode

Use:

```text
BOOT1 = 1
```

Typical workflow:

```text
build
-> load ELF directly into SRAM
-> debug/run
```

No external flash programming is required.

The program is lost after reset or power removal.

### Normal standalone boot

Program the persistent external flash first, then select the normal boot configuration.

Current boot chain:

```text
BootROM
-> external SEMPER flash
-> FSBL at 0x90040000
-> application at 0x90100000
```

After changing boot mode, reset or power-cycle the MCU.

---

## 12. CMake compiler cache

CMake stores the absolute compiler path in its cache.

If the project was previously configured using Ubuntu's generic compiler:

```text
/usr/bin/arm-none-eabi-gcc
```

instead of the STM32Cube GNU toolchain, remove the existing build directories once:

```bash
rm -rf build/Debug
rm -rf build/Release
rm -rf FSBL/build
rm -rf Appli/build
rm -rf ExtMemLoader/build
```

Then rebuild:

```bash
./scripts/build.sh --config Debug --target all
```

During configuration/build, the compiler path should point to the STM32Cube bundle, for example:

```text
~/.local/share/stm32cube/bundles/gnu-tools-for-stm32/.../bin/arm-none-eabi-gcc
```

and not:

```text
/usr/bin/arm-none-eabi-gcc
```

---

## 13. Optional tool path overrides

The scripts normally auto-detect the required STM32 tools.

If necessary, individual paths can be overridden using environment variables.

Example:

```bash
export STM32_GCC_PATH=/path/to/gnu-tools-for-stm32/bin
export STM32_SIGNING_TOOL=/path/to/STM32_SigningTool_CLI
export STM32_PROGRAMMER_CLI=/path/to/STM32_Programmer_CLI
export STLINK_GDBSERVER=/path/to/ST-LINK_gdbserver
```

For CMake or objcopy:

```bash
export CMAKE_BIN=/path/to/cmake
export OBJCOPY_BIN=/path/to/arm-none-eabi-objcopy
```

These overrides are optional and should normally not be committed with machine-specific absolute paths.

---

## 14. Quick reference

### Build application

```bash
./scripts/build.sh --config Debug --target appli
```

### Build everything

```bash
./scripts/build.sh --config Debug --target all
```

### Build release images

```bash
./scripts/build.sh --config Release --target all
```

### Program FSBL + Application A

```bash
./scripts/flash.sh --target all --config Debug --build
```

### Program already-built images

```bash
./scripts/flash.sh --target all
```

### Run application from SRAM and detach

```bash
./scripts/run_dev.sh --action run --target appli --config Debug --build
```

### Run FSBL from SRAM and detach

```bash
./scripts/run_dev.sh --action run --target fsbl --config Debug --build
```

### Make scripts executable after cloning

```bash
chmod +x scripts/*.sh
```

---

## 15. Recommended everyday workflow

For application development:

```text
BOOT1 = 1
-> Run and Debug
-> Dev: Debug Application from SRAM
```

For quick standalone SRAM execution:

```text
BOOT1 = 1
-> Run Task
-> Dev: Load + Run SRAM (detach)
```

For persistent firmware testing:

```text
Build / sign
-> Flash: Build + Program
-> select normal boot mode
-> reset / power-cycle
```

For debugging firmware that is already running:

```text
Run and Debug
-> Attach: Application (no reset)
```