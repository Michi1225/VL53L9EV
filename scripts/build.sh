#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# build.sh
#
# Unified STM32N6 build script.
#
# Usage:
#   ./scripts/build.sh --config Debug   --target appli
#   ./scripts/build.sh --config Debug   --target fsbl
#   ./scripts/build.sh --config Debug   --target extmem
#   ./scripts/build.sh --config Release --target all
#
# Targets:
#   appli   Build application ELF + BIN + signed SSBL image
#   fsbl    Build FSBL ELF + BIN + trusted FSBL image
#   extmem  Build ExtMemLoader ELF + .stldr
#   all     Build/package all three
#
# Optional environment overrides:
#   STM32_GCC_PATH=/path/to/gnu-tools-for-stm32/.../bin
#   CMAKE_BIN=/path/to/cmake
#   OBJCOPY_BIN=/path/to/arm-none-eabi-objcopy
#   STM32_SIGNING_TOOL=/path/to/STM32_SigningTool_CLI
# ============================================================================

CONFIG="Debug"
TARGET="all"
DEV_MODE=0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

FSBL_TARGET="VL53L9_eval_FSBL"
APPLI_TARGET="VL53L9_eval_Appli"
EXTMEM_TARGET="VL53L9_eval_ExtMemLoader"

FSBL_ELF="${ROOT_DIR}/FSBL/build/${FSBL_TARGET}.elf"
FSBL_BIN="${ROOT_DIR}/FSBL/build/${FSBL_TARGET}.bin"
FSBL_SIGNED="${ROOT_DIR}/FSBL/build/${FSBL_TARGET}-trusted.bin"

APPLI_ELF="${ROOT_DIR}/Appli/build/${APPLI_TARGET}.elf"
APPLI_BIN="${ROOT_DIR}/Appli/build/${APPLI_TARGET}.bin"
APPLI_SIGNED="${ROOT_DIR}/Appli/build/${APPLI_TARGET}-signed.bin"

EXTMEM_ELF="${ROOT_DIR}/ExtMemLoader/build/${EXTMEM_TARGET}.elf"
EXTMEM_STLDR="${ROOT_DIR}/ExtMemLoader/build/${EXTMEM_TARGET}.stldr"

die()
{
    echo "ERROR: $*" >&2
    exit 1
}

info()
{
    echo
    echo "================================================================"
    echo "$*"
    echo "================================================================"
}

usage()
{
    cat <<EOF
Usage:
  $0 --config Debug|Release --target appli|fsbl|extmem|all [--dev]
EOF
}

find_executable()
{
    local override="$1"
    local command_name="$2"
    shift 2

    if [[ -n "${override}" ]]; then
        [[ -x "${override}" ]] || die "'${override}' is not executable"
        printf '%s\n' "${override}"
        return 0
    fi

    if command -v "${command_name}" >/dev/null 2>&1; then
        command -v "${command_name}"
        return 0
    fi

    local candidate
    for candidate in "$@"; do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}

find_stm32_gcc_bin()
{
    if [[ -n "${STM32_GCC_PATH:-}" ]]; then
        [[ -x "${STM32_GCC_PATH}/arm-none-eabi-gcc" ]] || \
            die "STM32_GCC_PATH does not contain arm-none-eabi-gcc: ${STM32_GCC_PATH}"
        printf '%s\n' "${STM32_GCC_PATH}"
        return 0
    fi

    local roots=(
        "${HOME}/.local/share/stm32cube/bundles/gnu-tools-for-stm32"
        "${HOME}/STMicroelectronics"
        "/opt/st"
        "/opt/STMicroelectronics"
        "/usr/local/STMicroelectronics"
    )

    local existing=()
    local root
    for root in "${roots[@]}"; do
        [[ -d "${root}" ]] && existing+=("${root}")
    done

    if (( ${#existing[@]} > 0 )); then
        local gcc
        gcc="$(
            find "${existing[@]}" \
                -type f \
                -name arm-none-eabi-gcc \
                -perm -u+x \
                2>/dev/null \
            | sort -V \
            | tail -n 1
        )"

        if [[ -n "${gcc}" ]]; then
            dirname "${gcc}"
            return 0
        fi
    fi

    return 1
}

find_signing_tool()
{
    if [[ -n "${STM32_SIGNING_TOOL:-}" ]]; then
        [[ -x "${STM32_SIGNING_TOOL}" ]] || \
            die "STM32_SIGNING_TOOL is not executable: ${STM32_SIGNING_TOOL}"
        printf '%s\n' "${STM32_SIGNING_TOOL}"
        return 0
    fi

    if command -v STM32_SigningTool_CLI >/dev/null 2>&1; then
        command -v STM32_SigningTool_CLI
        return 0
    fi

    local roots=(
        "${HOME}/STMicroelectronics"
        "${HOME}/.local/share/stm32cube"
        "/opt/st"
        "/opt/STMicroelectronics"
        "/usr/local/STMicroelectronics"
    )

    local existing=()
    local root
    for root in "${roots[@]}"; do
        [[ -d "${root}" ]] && existing+=("${root}")
    done

    if (( ${#existing[@]} > 0 )); then
        local found
        found="$(
            find "${existing[@]}" \
                -type f \
                -name STM32_SigningTool_CLI \
                -perm -u+x \
                2>/dev/null \
            | sort -V \
            | tail -n 1
        )"

        if [[ -n "${found}" ]]; then
            printf '%s\n' "${found}"
            return 0
        fi
    fi

    return 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--config)
            [[ $# -ge 2 ]] || die "Missing value after $1"
            CONFIG="$2"
            shift 2
            ;;
        -t|--target)
            [[ $# -ge 2 ]] || die "Missing value after $1"
            TARGET="$2"
            shift 2
            ;;
        --dev)
            DEV_MODE=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown argument: $1"
            ;;
    esac
done

case "${CONFIG}" in
    Debug|Release) ;;
    *) die "Configuration must be Debug or Release" ;;
esac

case "${TARGET}" in
    appli|application) TARGET="appli" ;;
    fsbl) ;;
    extmem|extmemloader) TARGET="extmem" ;;
    all) ;;
    *) die "Target must be appli, fsbl, extmem or all" ;;
esac

STM32_GCC_BIN="$(find_stm32_gcc_bin)" || \
    die "STM32Cube GNU ARM toolchain not found. Set STM32_GCC_PATH=/path/to/bin"

export PATH="${STM32_GCC_BIN}:${PATH}"

ARM_GCC="$(command -v arm-none-eabi-gcc || true)"
[[ -n "${ARM_GCC}" ]] || die "arm-none-eabi-gcc not found"

CMAKE="$(
    find_executable \
        "${CMAKE_BIN:-}" \
        cmake \
        "/usr/bin/cmake" \
        "/usr/local/bin/cmake"
)" || die "cmake not found"

OBJCOPY="$(
    find_executable \
        "${OBJCOPY_BIN:-}" \
        arm-none-eabi-objcopy
)" || die "arm-none-eabi-objcopy not found"

# Only the application/FSBL packaging steps need SigningTool.
SIGNING_TOOL=""
if [[ "${TARGET}" == "appli" || "${TARGET}" == "fsbl" || "${TARGET}" == "all" ]]; then
    SIGNING_TOOL="$(find_signing_tool)" || {
        cat >&2 <<'EOF'
ERROR: STM32_SigningTool_CLI was not found.

Install the full STM32CubeProgrammer package, or set:
  STM32_SIGNING_TOOL=/path/to/STM32_SigningTool_CLI
EOF
        exit 1
    }
fi

info "Build configuration"
echo "Project root : ${ROOT_DIR}"
echo "Configuration: ${CONFIG}"
echo "Target       : ${TARGET}"
echo "DEV mode     : ${DEV_MODE}"
echo "ARM GCC      : ${ARM_GCC}"
echo "CMake        : ${CMAKE}"
echo "Objcopy      : ${OBJCOPY}"
[[ -n "${SIGNING_TOOL}" ]] && echo "Signing tool : ${SIGNING_TOOL}"

cd "${ROOT_DIR}"

info "Configuring CMake"
if [[ "${DEV_MODE}" -eq 1 ]]; then
    "${CMAKE}" --preset "${CONFIG}" -DDEV_MODE=ON
else
    "${CMAKE}" --preset "${CONFIG}" -DDEV_MODE=OFF
fi

build_target()
{
    local cmake_target="$1"
    "${CMAKE}" --build --preset "${CONFIG}" --target "${cmake_target}"
}

package_fsbl()
{
    [[ -f "${FSBL_ELF}" ]] || die "FSBL ELF not found: ${FSBL_ELF}"

    info "Packaging FSBL"
    "${OBJCOPY}" -O binary "${FSBL_ELF}" "${FSBL_BIN}"

    rm -f "${FSBL_SIGNED}"
    "${SIGNING_TOOL}" \
        -bin "${FSBL_BIN}" \
        -s \
        -nk \
        -of 0x80000000 \
        -t fsbl \
        -hv 2.3 \
        --align \
        -o "${FSBL_SIGNED}"

    [[ -f "${FSBL_SIGNED}" ]] || die "Failed to create ${FSBL_SIGNED}"
}

package_appli()
{
    [[ -f "${APPLI_ELF}" ]] || die "Application ELF not found: ${APPLI_ELF}"

    info "Packaging application"
    "${OBJCOPY}" -O binary "${APPLI_ELF}" "${APPLI_BIN}"

    rm -f "${APPLI_SIGNED}"
    "${SIGNING_TOOL}" \
        -bin "${APPLI_BIN}" \
        -nk \
        -t ssbl \
        -hv 2.3 \
        --align \
        -o "${APPLI_SIGNED}"

    [[ -f "${APPLI_SIGNED}" ]] || die "Failed to create ${APPLI_SIGNED}"
}

package_extmem()
{
    [[ -f "${EXTMEM_ELF}" ]] || die "ExtMemLoader ELF not found: ${EXTMEM_ELF}"

    info "Creating ExtMemLoader .stldr"
    rm -f "${EXTMEM_STLDR}"
    cp "${EXTMEM_ELF}" "${EXTMEM_STLDR}"

    [[ -f "${EXTMEM_STLDR}" ]] || die "Failed to create ${EXTMEM_STLDR}"

    if command -v file >/dev/null 2>&1; then
        file "${EXTMEM_STLDR}" | grep -q "ELF" || \
            die "${EXTMEM_STLDR} does not appear to be an ELF file"
    fi
}

case "${TARGET}" in
    appli)
        info "Building application"
        build_target "${APPLI_TARGET}"
        package_appli
        ;;
    fsbl)
        info "Building FSBL"
        build_target "${FSBL_TARGET}"
        package_fsbl
        ;;
    extmem)
        info "Building ExtMemLoader"
        build_target "${EXTMEM_TARGET}"
        package_extmem
        ;;
    all)
        info "Building FSBL, application and ExtMemLoader"
        "${CMAKE}" \
            --build \
            --preset "${CONFIG}" \
            --target \
                "${FSBL_TARGET}" \
                "${APPLI_TARGET}" \
                "${EXTMEM_TARGET}"

        package_fsbl
        package_appli
        package_extmem
        ;;
esac

info "Build complete"

case "${TARGET}" in
    appli)
        echo "ELF    : ${APPLI_ELF}"
        echo "Signed : ${APPLI_SIGNED}"
        ;;
    fsbl)
        echo "ELF    : ${FSBL_ELF}"
        echo "Signed : ${FSBL_SIGNED}"
        ;;
    extmem)
        echo "ELF    : ${EXTMEM_ELF}"
        echo "Loader : ${EXTMEM_STLDR}"
        ;;
    all)
        echo "FSBL   : ${FSBL_SIGNED}"
        echo "Appli  : ${APPLI_SIGNED}"
        echo "Loader : ${EXTMEM_STLDR}"
        ;;
esac
