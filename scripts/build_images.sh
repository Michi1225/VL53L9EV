#!/usr/bin/env bash
set -euo pipefail

# Build and package the STM32N6 FSBL + application images.
#
# Usage:
#   ./scripts/build_images.sh [Debug|Release]
#
# Requirements:
#   - cmake
#   - ninja
#   - arm-none-eabi-objcopy
#   - STM32CubeProgrammer with STM32_PRG_PATH pointing to its "bin" directory
#
# Outputs:
#   FSBL/build/VL53L9_eval_FSBL.bin
#   FSBL/build/VL53L9_eval_FSBL-trusted.bin
#   Appli/build/VL53L9_eval_Appli.bin
#   Appli/build/VL53L9_eval_Appli-signed.bin

CONFIG="${1:-Debug}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

FSBL_ELF="${ROOT_DIR}/FSBL/build/VL53L9_eval_FSBL.elf"
FSBL_BIN="${ROOT_DIR}/FSBL/build/VL53L9_eval_FSBL.bin"
FSBL_TRUSTED="${ROOT_DIR}/FSBL/build/VL53L9_eval_FSBL-trusted.bin"

APPLI_ELF="${ROOT_DIR}/Appli/build/VL53L9_eval_Appli.elf"
APPLI_BIN="${ROOT_DIR}/Appli/build/VL53L9_eval_Appli.bin"
APPLI_SIGNED="${ROOT_DIR}/Appli/build/VL53L9_eval_Appli-signed.bin"

if command -v nproc >/dev/null 2>&1; then
    JOBS="${JOBS:-$(nproc)}"
else
    JOBS="${JOBS:-4}"
fi

die()
{
    echo "ERROR: $*" >&2
    exit 1
}

find_signing_tool()
{
    local candidate

    [[ -n "${STM32_PRG_PATH:-}" ]] || \
        die "STM32_PRG_PATH is not set. It should point to the STM32CubeProgrammer bin directory."

    for candidate in \
        "${STM32_PRG_PATH}/STM32_SigningTool_CLI" \
        "${STM32_PRG_PATH}/STM32_SigningTool_CLI.exe"
    do
        if [[ -f "${candidate}" ]]; then
            echo "${candidate}"
            return 0
        fi
    done

    die "STM32_SigningTool_CLI was not found under STM32_PRG_PATH=${STM32_PRG_PATH}"
}

command -v cmake >/dev/null 2>&1 || die "cmake not found in PATH"
command -v arm-none-eabi-objcopy >/dev/null 2>&1 || die "arm-none-eabi-objcopy not found in PATH"

SIGNING_TOOL="$(find_signing_tool)"

case "${CONFIG}" in
    Debug|Release)
        ;;
    *)
        die "Unsupported configuration '${CONFIG}'. Use Debug or Release."
        ;;
esac

echo "============================================================"
echo "Configuring ${CONFIG}"
echo "============================================================"

cd "${ROOT_DIR}"
cmake --preset "${CONFIG}"

echo
echo "============================================================"
echo "Building FSBL + Application"
echo "============================================================"

cmake --build --preset "${CONFIG}" \
    --target VL53L9_eval_FSBL VL53L9_eval_Appli \
    --parallel "${JOBS}"

[[ -f "${FSBL_ELF}" ]] || die "FSBL ELF not found: ${FSBL_ELF}"
[[ -f "${APPLI_ELF}" ]] || die "Application ELF not found: ${APPLI_ELF}"

echo
echo "============================================================"
echo "Converting ELF -> raw BIN"
echo "============================================================"

arm-none-eabi-objcopy -O binary "${FSBL_ELF}" "${FSBL_BIN}"
arm-none-eabi-objcopy -O binary "${APPLI_ELF}" "${APPLI_BIN}"

echo "Created:"
echo "  ${FSBL_BIN}"
echo "  ${APPLI_BIN}"

echo
echo "============================================================"
echo "Adding STM32N6 image headers"
echo "============================================================"

# FSBL image:
# - type fsbl: consumed directly by the STM32N6 BootROM
# - -of 0x80000000: ST's non-secure/unkeyed FSBL option flags for this flow
# - --align: required/recommended with recent STM32CubeProgrammer versions
"${SIGNING_TOOL}" \
    -bin "${FSBL_BIN}" \
    -s \
    -nk \
    -of 0x80000000 \
    --align \
    -t fsbl \
    -hv 2.3 \
    -o "${FSBL_TRUSTED}"

# LRUN application image:
# - type ssbl: image loaded/processed by the FSBL
# - the generated 0x400-byte image header is accounted for by
#   EXTMEM_HEADER_OFFSET = 0x400 in the FSBL configuration
"${SIGNING_TOOL}" \
    -bin "${APPLI_BIN}" \
    -nk \
    --align \
    -t ssbl \
    -hv 2.3 \
    -o "${APPLI_SIGNED}"

echo
echo "============================================================"
echo "Done"
echo "============================================================"
echo
echo "Raw binaries:"
echo "  FSBL : ${FSBL_BIN}"
echo "  App  : ${APPLI_BIN}"
echo
echo "Images to program:"
echo "  FSBL : ${FSBL_TRUSTED}"
echo "         -> flash at 0x90040000"
echo
echo "  App A: ${APPLI_SIGNED}"
echo "         -> flash at 0x90100000"
echo
echo "  App B: ${APPLI_SIGNED}"
echo "         -> flash at 0x90900000"
