#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CPIO_FILE="${SCRIPT_DIR}/rootfs.cpio"
ROOTFS_DIR="${SCRIPT_DIR}/rootfs"

if [ ! -d "${ROOTFS_DIR}" ]; then
    echo "Error: ${ROOTFS_DIR} not found"
    exit 1
fi

echo "Packing ${ROOTFS_DIR} to ${CPIO_FILE} ..."
cd "${ROOTFS_DIR}" || exit 1

fakeroot sh -c 'find . | cpio -o -H newc' > "${CPIO_FILE}"

rm -rf ${ROOTFS_DIR}
echo "Done. Rootfs packed to ${CPIO_FILE}"
