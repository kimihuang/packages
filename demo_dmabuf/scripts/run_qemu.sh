#!/bin/bash
# run_qemu.sh – launch the demo_dmabuf guest kernel under QEMU (aarch64)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ARCH="${ARCH:-arm64}"
CROSS="${CROSS:-aarch64-linux-gnu-}"

# ---- Configurable paths (override via env or edit below) ----
KERNEL_IMAGE="${KERNEL_IMAGE:-$PROJECT_DIR/../guest_kernel/Image}"
ROOTFS_CPIO="${ROOTFS_CPIO:-$PROJECT_DIR/rootfs.cpio}"

QEMU="${QEMU:-qemu-system-aarch64}"

# QEMU machine settings
MACHINE="virt"
CPU="cortex-a57"
SMP="${SMP:-2}"
MEM="${MEM:-512M}"

# Network (user-mode NAT, useful for gdb / ssh if needed)
NET="-netdev user,id=net0 -device virtio-net-pci,netdev=net0"

# Serial console (stdio)
SERIAL="-serial mon:stdio"

# GDB (uncomment to wait for gdb connect on port 1234)
# GDB="-gdb tcp::1234 -S"

# Kernel command line
CMDLINE="console=ttyAMA0 earlycon=pl011,0x09000000 \
    panic=1 \
    rdinit=/init \
    quiet"

# ---- Sanity checks ----
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "ERROR: Kernel image not found: $KERNEL_IMAGE"
    echo "  Set KERNEL_IMAGE env var to override."
    exit 1
fi

if [ ! -f "$ROOTFS_CPIO" ]; then
    echo "ERROR: rootfs not found: $ROOTFS_CPIO"
    echo "  Run 'make rootfs' first, or set ROOTFS_CPIO env var."
    exit 1
fi

# ---- Launch QEMU ----
echo "Launching QEMU ..."
echo "  Machine : $MACHINE  (CPU: $CPU, SMP: $SMP, MEM: $MEM)"
echo "  Kernel  : $KERNEL_IMAGE"
echo "  Rootfs  : $ROOTFS_CPIO"

exec $QEMU \
    -machine "$MACHINE" \
    -cpu "$CPU" \
    -smp "$SMP" \
    -m "$MEM" \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$ROOTFS_CPIO" \
    -append "$CMDLINE" \
    -no-reboot \
    $NET \
    $SERIAL \
    $GDB
