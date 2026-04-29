#!/bin/bash
# build.sh – convenience wrapper around make with cross-compilation defaults
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_DIR"

exec make \
    ARCH="${ARCH:-arm64}" \
    CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" \
    CROSS="${CROSS_COMPILE:-aarch64-linux-gnu-}" \
    "$@"
