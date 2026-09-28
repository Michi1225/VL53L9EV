#!/usr/bin/env bash
set -euo pipefail

# STM32N6 development-mode helper.
#
# Actions:
#   debug  - reset, load ELF into SRAM, run to main, keep GDB attached
#   run    - reset, load ELF into SRAM, detach; target keeps running
#   attach - hot-attach using ELF symbols only; do not reset or reload
#
# For debug/run loading, BOOT1 must be 1 (Development Mode).
# Attach can also be used later against an already-running target.
#
# Usage:
#   ./scripts/run_dev.sh --action debug  --target appli --build
#   ./scripts/run_dev.sh --action run    --target appli --build
#   ./scripts/run_dev.sh --action attach --target appli
#   ./scripts/run_dev.sh --action debug  --target fsbl  --build

ACTION=""
TARGET=""
CONFIG="Debug"
BUILD_FIRST=0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GDB_PORT="${STM32_GDB_PORT:-61234}"

die()
{
    echo "ERROR: $*" >&2
    exit 1
}

usage()
{
    cat <<EOF
Usage:
  $0 --action debug|run|attach --target appli|fsbl [--config Debug|Release] [--build]

Examples:
  $0 --action debug  --target appli --build
  $0 --action run    --target appli --build
  $0 --action attach --target appli
EOF
}

find_stm32_gcc_bin()
{
    if [[ -n "${STM32_GCC_PATH:-}" ]]; then
        [[ -x "${STM32_GCC_PATH}/arm-none-eabi-gdb" ]] || \
            die "STM32_GCC_PATH does not contain arm-none-eabi-gdb"
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
        local gdb
        gdb="$(
            find "${existing[@]}" -type f -name arm-none-eabi-gdb -perm -u+x \
                2>/dev/null | sort -V | tail -n 1
        )"
        [[ -n "${gdb}" ]] && { dirname "${gdb}"; return 0; }
    fi
    return 1
}

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

find_gdb_server()
{
    if [[ -n "${STLINK_GDBSERVER:-}" ]]; then
        [[ -x "${STLINK_GDBSERVER}" ]] || die "STLINK_GDBSERVER is not executable"
        printf '%s\n' "${STLINK_GDBSERVER}"
        return 0
    fi

    if command -v ST-LINK_gdbserver >/dev/null 2>&1; then
        command -v ST-LINK_gdbserver
        return 0
    fi

    local roots=(
        "${HOME}/.local/share/stm32cube"
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
        local server
        server="$(
            find "${existing[@]}" -type f -name ST-LINK_gdbserver -perm -u+x \
                2>/dev/null | sort -V | tail -n 1
        )"
        [[ -n "${server}" ]] && { printf '%s\n' "${server}"; return 0; }
    fi
    return 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--action)
            [[ $# -ge 2 ]] || die "Missing value after $1"
            ACTION="$2"
            shift 2
            ;;
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

case "${ACTION}" in
    debug|run|attach) ;;
    *) usage; die "Action must be debug, run or attach" ;;
esac

case "${CONFIG}" in
    Debug|Release) ;;
    *) die "Configuration must be Debug or Release" ;;
esac

case "${TARGET}" in
    appli|application)
        TARGET="appli"
        ELF="${ROOT_DIR}/Appli/build/VL53L9_eval_Appli.elf"
        DISPLAY_NAME="Application"
        ;;
    fsbl)
        ELF="${ROOT_DIR}/FSBL/build/VL53L9_eval_FSBL.elf"
        DISPLAY_NAME="FSBL"
        ;;
    *)
        usage
        die "Target must be appli or fsbl"
        ;;
esac

if [[ "${BUILD_FIRST}" -eq 1 ]]; then
    "${SCRIPT_DIR}/build.sh" --target "${TARGET}" --config "${CONFIG}"
fi

[[ -f "${ELF}" ]] || die "ELF not found: ${ELF}"

STM32_GCC_BIN="$(find_stm32_gcc_bin)" || die "arm-none-eabi-gdb not found"
export PATH="${STM32_GCC_BIN}:${PATH}"

GDB="$(command -v arm-none-eabi-gdb)"
PROGRAMMER_CLI="$(find_programmer_cli)" || die "STM32_Programmer_CLI not found"
PROGRAMMER_BIN="$(dirname "${PROGRAMMER_CLI}")"
GDBSERVER="$(find_gdb_server)" || die "ST-LINK_gdbserver not found"

LOG_FILE="$(mktemp -t stm32n6-gdbserver.XXXXXX.log)"
GDBSERVER_PID=""

cleanup()
{
    if [[ -n "${GDBSERVER_PID}" ]] && kill -0 "${GDBSERVER_PID}" 2>/dev/null; then
        kill "${GDBSERVER_PID}" 2>/dev/null || true
        wait "${GDBSERVER_PID}" 2>/dev/null || true
    fi
    rm -f "${LOG_FILE}"
}
trap cleanup EXIT INT TERM

echo "==============================================================="
echo "STM32N6 GDB"
echo "==============================================================="
echo "Action      : ${ACTION}"
echo "Target      : ${DISPLAY_NAME}"
echo "ELF         : ${ELF}"
echo "GDB         : ${GDB}"
echo "GDB server  : ${GDBSERVER}"
echo "Port        : ${GDB_PORT}"
echo

if [[ "${ACTION}" != "attach" ]]; then
    echo "Requirement : BOOT1 = 1 (Development Mode)"
else
    echo "Hot attach  : no reset and no image reload"
fi
echo

"${GDBSERVER}" \
    -p "${GDB_PORT}" \
    -l 1 \
    -d \
    -s \
    -cp "${PROGRAMMER_BIN}" \
    -m 1 \
    -g \
    >"${LOG_FILE}" 2>&1 &

GDBSERVER_PID=$!

READY=0
for _ in $(seq 1 60); do
    if ! kill -0 "${GDBSERVER_PID}" 2>/dev/null; then
        echo "ST-LINK_gdbserver terminated unexpectedly:" >&2
        cat "${LOG_FILE}" >&2
        exit 1
    fi

    if command -v ss >/dev/null 2>&1; then
        if ss -ltn 2>/dev/null | grep -Eq "[:.]${GDB_PORT}[[:space:]]"; then
            READY=1
            break
        fi
    else
        sleep 1
        READY=1
        break
    fi
    sleep 0.1
done

[[ "${READY}" -eq 1 ]] || {
    echo "Timed out waiting for ST-LINK_gdbserver:" >&2
    cat "${LOG_FILE}" >&2
    exit 1
}

case "${ACTION}" in
    debug)
        exec "${GDB}" "${ELF}" \
            -ex "target remote localhost:${GDB_PORT}" \
            -ex "monitor reset" \
            -ex "load" \
            -ex "tbreak main" \
            -ex "continue"
        ;;

    run)
        # GDB detach normally resumes the target. This leaves the SRAM-loaded
        # image running while both GDB and the GDB server exit.
        "${GDB}" "${ELF}" --batch \
            -ex "target remote localhost:${GDB_PORT}" \
            -ex "monitor reset" \
            -ex "load" \
            -ex "set confirm off" \
            -ex "detach" \
            -ex "quit"
        echo
        echo "${DISPLAY_NAME} loaded into SRAM and detached."
        echo "The target should now be running. You can hot-attach later."
        ;;

    attach)
        exec "${GDB}" "${ELF}" \
            -ex "target remote localhost:${GDB_PORT}"
        ;;
esac
