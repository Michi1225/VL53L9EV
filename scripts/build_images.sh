#!/usr/bin/env bash
set -euo pipefail

# Backwards-compatible wrapper.
CONFIG="${1:-Debug}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "${SCRIPT_DIR}/build.sh" --config "${CONFIG}" --target all
