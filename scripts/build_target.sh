#!/usr/bin/env bash
set -euo pipefail

# Backwards-compatible wrapper for older task definitions.
TARGET=""
CONFIG="Debug"
DEV_MODE=0

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--target)
            TARGET="$2"
            shift 2
            ;;
        -c|--config)
            CONFIG="$2"
            shift 2
            ;;
        --dev)
            DEV_MODE=1
            shift
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

[[ -n "${TARGET}" ]] || {
    echo "ERROR: --target is required" >&2
    exit 1
}

if [[ "${DEV_MODE}" -eq 1 ]]; then
    exec "${SCRIPT_DIR}/build.sh" \
        --config "${CONFIG}" \
        --target "${TARGET}" \
        --dev
else
    exec "${SCRIPT_DIR}/build.sh" \
        --config "${CONFIG}" \
        --target "${TARGET}"
fi
