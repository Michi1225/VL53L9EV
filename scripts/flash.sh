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
RESET_BOOT_METADATA=0

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
BOOT_METADATA_ADDR="0x91100000"
BOOT_METADATA_SIZE=524288 # Exactly the two 256 KiB A/B metadata sectors.

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
     [--reset-boot-metadata]

--reset-boot-metadata: Reprovision application A after direct SWD replacement.
  Only valid with --target all or appli-a. Back up the two boot-metadata sectors,
  then reset them after all selected images and parameters have been verified.
  Discards A/B update/trial history; the next FSBL boot adopts validated images,
  preferring A. The reset itself does not erase parameter or application regions.
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
        --reset-boot-metadata)
            RESET_BOOT_METADATA=1
            shift
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

if [[ "${RESET_BOOT_METADATA}" -eq 1 ]]; then
    case "${TARGET}" in
        all|appli-a) ;;
        *) die "--reset-boot-metadata requires --target all or appli-a (the new baseline is slot A)" ;;
    esac
fi

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
TEMP_FILES=("${PARAMETER_READBACK}")
trap 'rm -f "${TEMP_FILES[@]}"' EXIT
"${PROGRAMMER}" \
    -c "port=SWD" "mode=${CONNECT_MODE}" \
    -el "${LOADER}" -hardRst \
    -u "${PARAMETERS_ADDR}" 16384 "${PARAMETER_READBACK}"
python3 "${SCRIPT_DIR}/pack_parameters.py" "${PARAMETERS}" "${PARAMETERS_IMAGE}" \
    --existing "${PARAMETER_READBACK}" "${PACK_ARGS[@]}"

if [[ "${RESET_BOOT_METADATA}" -eq 1 ]]; then
    python3 - "${SCRIPT_DIR}" "${APPLI_IMAGE}" <<'PY'
from pathlib import Path
import sys
sys.path.insert(0, sys.argv[1])
from firmware_image import validate_image
try:
    validate_image(Path(sys.argv[2]).read_bytes())
except ValueError as error:
    raise SystemExit(f'Reprovisioning requires a valid persistent application image: {error}')
PY
    # Preserve the actual records before any writes. A short/failed backup aborts
    # reprovisioning, rather than silently destroying the only diagnostic record.
    BOOT_METADATA_BACKUP="$(mktemp "${ROOT_DIR}/build/boot-metadata-backup.XXXXXX.bin")"
    "${PROGRAMMER}" \
        -c "port=SWD" "mode=${CONNECT_MODE}" \
        -el "${LOADER}" -hardRst \
        -u "${BOOT_METADATA_ADDR}" "${BOOT_METADATA_SIZE}" "${BOOT_METADATA_BACKUP}"
    python3 - "${BOOT_METADATA_BACKUP}" "${BOOT_METADATA_SIZE}" <<'PY'
from pathlib import Path
import sys
if Path(sys.argv[1]).stat().st_size != int(sys.argv[2]):
    raise SystemExit('Boot metadata backup must contain exactly 512 KiB; no images were programmed')
PY
    BOOT_METADATA_BLANK="$(mktemp --suffix=.bin -t mgs-boot-metadata.XXXXXX)"
    BOOT_METADATA_VERIFY="$(mktemp --suffix=.bin -t mgs-boot-metadata-verify.XXXXXX)"
    TEMP_FILES+=("${BOOT_METADATA_BLANK}" "${BOOT_METADATA_VERIFY}")
    python3 - "${BOOT_METADATA_BLANK}" "${BOOT_METADATA_SIZE}" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_bytes(b'\xff' * int(sys.argv[2]))
PY
    echo "Boot metadata backup: ${BOOT_METADATA_BACKUP}"
fi

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

# Commit parameter JSON only after every selected firmware image has been verified.
program "${PARAMETERS_IMAGE}" "${PARAMETERS_ADDR}" "Parameters (reserved first 256 KiB)"

if [[ "${RESET_BOOT_METADATA}" -eq 1 ]]; then
    # Address-based programming avoids sector-number differences between loader
    # versions. CubeProgrammer's normal auto-erase replaces only these two full
    # sectors. Verify both via -v and an independent readback of all 512 KiB.
    program "${BOOT_METADATA_BLANK}" "${BOOT_METADATA_ADDR}" "A/B boot metadata reset"
    "${PROGRAMMER}" \
        -c "port=SWD" "mode=${CONNECT_MODE}" \
        -el "${LOADER}" -hardRst \
        -u "${BOOT_METADATA_ADDR}" "${BOOT_METADATA_SIZE}" "${BOOT_METADATA_VERIFY}"
    python3 - "${BOOT_METADATA_VERIFY}" "${BOOT_METADATA_SIZE}" <<'PY'
from pathlib import Path
import sys
if Path(sys.argv[1]).read_bytes() != b'\xff' * int(sys.argv[2]):
    raise SystemExit('Boot metadata reset did not read back as erased; baseline adoption is not ready')
PY
    echo "Boot metadata reset verified; next boot will adopt the validated slot A baseline."
elif [[ "${TARGET}" == "appli-a" || "${TARGET}" == "appli-b" || "${TARGET}" == "all" ]]; then
    echo "Existing A/B boot records were retained. Changed SWD images may no longer match them."
    echo "For deliberate slot A reprovisioning, use --target all or appli-a with --reset-boot-metadata."
fi

echo
echo "Programming complete."
echo "For normal standalone boot, select the flash boot mode and reset/power-cycle."
