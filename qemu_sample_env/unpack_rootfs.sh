#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CPIO_FILE="${SCRIPT_DIR}/rootfs.cpio"
ROOTFS_DIR="${SCRIPT_DIR}/rootfs"

if [ ! -f "${CPIO_FILE}" ]; then
    echo "Error: ${CPIO_FILE} not found"
    exit 1
fi

echo "Unpacking ${CPIO_FILE} to ${ROOTFS_DIR} ..."
mkdir -p "${ROOTFS_DIR}"
cd "${ROOTFS_DIR}" || exit 1

cpio -idmv < "${CPIO_FILE}"

echo "Done. Rootfs extracted to ${ROOTFS_DIR}"
