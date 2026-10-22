#!/usr/bin/env bash
set -euo pipefail

# Program the persistent external SEMPER flash through the custom .stldr.
#
# Usage:
#   ./scripts/flash.sh --target fsbl
#   ./scripts/flash.sh --target appli-a
#   ./scripts/flash.sh --target appli-b
#   ./scripts/flash.sh --target all
#
# Every target refreshes parameters, preserving identity and other slots.
# Add --build to rebuild/sign images first:
#   ./scripts/flash.sh --target all --build --config Debug

TARGET=""
CONFIG="Debug"
BUILD_FIRST=0
PARAMETERS=""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

LOADER="${ROOT_DIR}/ExtMemLoader/build/magnecko-sense-fw_ExtMemLoader.stldr"
FSBL_IMAGE="${ROOT_DIR}/FSBL/build/magnecko-sense-fw_FSBL-trusted.bin"
APPLI_IMAGE="${ROOT_DIR}/Appli/build/magnecko-sense-fw_Appli-signed.bin"

FSBL_ADDR="0x90040000"
APPLI_A_ADDR="0x90100000"
APPLI_B_ADDR="0x90900000"
PARAMETERS_ADDR="0x90000000"
PARAMETERS_IMAGE="${ROOT_DIR}/build/flash_parameters.bin"

CONNECT_MODE="${STM32_CONNECT_MODE:-HOTPLUG}"

# @brief Print an error message and exit with failure.
die()
{
    echo "ERROR: $*" >&2
    exit 1
}

# @brief Print the supported command-line options.
usage()
{
    cat <<EOF
Usage:
  $0 --target fsbl|appli-a|appli-b|parameters|all [--build] [--config Debug|Release]
     [--parameters /path/to/parameters.json]
EOF
}

# @brief Locate the STM32CubeProgrammer command-line executable.
find_programmer_cli()
{
    if [[ -n "${STM32_PROGRAMMER_CLI:-}" ]]; then
        [[ -x "${STM32_PROGRAMMER_CLI}" ]] || \
            die "STM32_PROGRAMMER_CLI is not executable"
        printf '%s\n' "${STM32_PROGRAMMER_CLI}"
        return 0
    fi

    if command -v STM32_Programmer_CLI >/dev/null 2>&1; then
        command -v STM32_Programmer_CLI
        return 0
    fi

    local roots=(
        "${HOME}/.local/share/stm32cube/bundles/programmer"
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
        local cli
        cli="$(
            find "${existing[@]}" -type f -name STM32_Programmer_CLI -perm -u+x \
                2>/dev/null | sort -V | tail -n 1
        )"
        [[ -n "${cli}" ]] && { printf '%s\n' "${cli}"; return 0; }
    fi
    return 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--target)
            [[ $# -ge 2 ]] || die "Missing value after $1"
            TARGET="$2"
            shift 2
            ;;
        -c|--config)
            [[ $# -ge 2 ]] || die "Missing value after $1"
            CONFIG="$2"
            shift 2
            ;;
        --parameters)
            [[ $# -ge 2 ]] || die "Missing value after $1"
            PARAMETERS="$2"
            shift 2
            ;;
        --build)
            BUILD_FIRST=1
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

case "${TARGET}" in
    fsbl|appli-a|appli-b|parameters|all) ;;
    *) usage; die "Target must be fsbl, appli-a, appli-b, parameters or all" ;;
esac

case "${CONFIG}" in
    Debug|Release) ;;
    *) die "Configuration must be Debug or Release" ;;
esac

if [[ "${BUILD_FIRST}" -eq 1 ]]; then
    "${SCRIPT_DIR}/build.sh" --target all --config "${CONFIG}"
fi

[[ -f "${LOADER}" ]] || die "External loader not found: ${LOADER}"

PROGRAMMER="$(find_programmer_cli)" || die "STM32_Programmer_CLI not found"

# Validate all selected image paths before touching the device. Unselected
# images/manifests are not needed, even for a parameters-only update.
PACK_ARGS=(--target "${TARGET}" --connect-mode "${CONNECT_MODE}")
case "${TARGET}" in
    fsbl|all)
        [[ -f "${FSBL_IMAGE}" ]] || die "FSBL image not found: ${FSBL_IMAGE}"
        PACK_ARGS+=(--fsbl "${FSBL_IMAGE}")
        ;;
esac
case "${TARGET}" in
    appli-a|all)
        [[ -f "${APPLI_IMAGE}" ]] || die "Application image not found: ${APPLI_IMAGE}"
        PACK_ARGS+=(--application "${APPLI_IMAGE}")
        ;;
    appli-b)
        [[ -f "${APPLI_IMAGE}" ]] || die "Application image not found: ${APPLI_IMAGE}"
        PACK_ARGS+=(--application-b "${APPLI_IMAGE}")
        ;;
esac
if [[ -n "${PARAMETERS}" || "${TARGET}" == "parameters" ]]; then
    PACK_ARGS+=(--replace)
fi
PARAMETERS="${PARAMETERS:-${ROOT_DIR}/config/flash_parameters.json}"
[[ -f "${PARAMETERS}" ]] || die "Parameter file not found: ${PARAMETERS}"

# Read this board's existing settings rather than borrowing provenance from
# a previous board or from unrelated binaries in the local build directory.
PARAMETER_READBACK="$(mktemp --suffix=.bin -t vl53l9-parameters.XXXXXX)"
trap 'rm -f "${PARAMETER_READBACK}"' EXIT
"${PROGRAMMER}" \
    -c "port=SWD" "mode=${CONNECT_MODE}" \
    -el "${LOADER}" -hardRst \
    -u "${PARAMETERS_ADDR}" 16384 "${PARAMETER_READBACK}"
python3 "${SCRIPT_DIR}/pack_parameters.py" "${PARAMETERS}" "${PARAMETERS_IMAGE}" \
    --existing "${PARAMETER_READBACK}" "${PACK_ARGS[@]}"

# @brief Program and verify the selected image at its configured flash address.
program()
{
    local image="$1"
    local address="$2"
    local name="$3"

    [[ -f "${image}" ]] || die "${name} image not found: ${image}"

    echo
    echo "==============================================================="
    echo "Programming ${name}"
    echo "==============================================================="
    echo "Image   : ${image}"
    echo "Address : ${address}"
    echo "Loader  : ${LOADER}"
    echo

    "${PROGRAMMER}" \
        -c "port=SWD" "mode=${CONNECT_MODE}" \
        -el "${LOADER}" \
        -hardRst \
        -w "${image}" "${address}" \
        -v
}

case "${TARGET}" in
    parameters) ;;
    fsbl)
        program "${FSBL_IMAGE}" "${FSBL_ADDR}" "FSBL"
        ;;
    appli-a)
        program "${APPLI_IMAGE}" "${APPLI_A_ADDR}" "Application A"
        ;;
    appli-b)
        program "${APPLI_IMAGE}" "${APPLI_B_ADDR}" "Application B"
        ;;
    all)
        program "${FSBL_IMAGE}" "${FSBL_ADDR}" "FSBL"
        program "${APPLI_IMAGE}" "${APPLI_A_ADDR}" "Application A"
        ;;
esac

# Commit metadata only after every selected firmware image has been verified.
program "${PARAMETERS_IMAGE}" "${PARAMETERS_ADDR}" "Parameters (reserved first 256 KiB)"

echo
echo "Programming complete."
echo "For normal standalone boot, select the flash boot mode and reset/power-cycle."
