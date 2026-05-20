#!/bin/bash

KERNEL_IMAGE="/home/lion/workdir/sourcecode/quantum_main/out/quantum_qemu_debug/build/linux-6.1/arch/arm64/boot/Image"
DTB="/home/lion/workdir/sourcecode/quantum_main/out/quantum_qemu_debug/images/quantum_qemu.dtb"
ROOTFS="/home/lion/workdir/sourcecode/quantum_main/out/quantum_qemu_debug/images/rootfs.cpio"
SHARED_DIR="/home/lion/workdir/sourcecode/quantum_main/out/quantum_qemu_debug/qemu_host_shared"

mkdir -p "${SHARED_DIR}"

echo "Starting QEMU in screen session 'qemu_debug'..."
echo "  - gdbserver port forwarding: host:1234 -> QEMU:1234"
echo "  - Attach to QEMU console: screen -r qemu_debug"
echo "  - Detach from QEMU: Ctrl+A, D"

screen -dmS qemu_debug qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -nographic \
    -serial mon:stdio \
    -m 512 \
    -kernel "${KERNEL_IMAGE}" \
    -dtb "${DTB}" \
    -initrd "${ROOTFS}" \
    -append "console=ttyAMA0 init=/init nokaslr earlycon=pl011,0x9000000 debug loglevel=8" \
    -monitor none \
    -netdev user,id=net0,hostfwd=tcp::1234-:1234 \
    -device virtio-net-device,netdev=net0 \
    -fsdev local,id=shared_dev,path="${SHARED_DIR}",security_model=mapped-xattr \
    -device virtio-9p-device,fsdev=shared_dev,mount_tag=host_shared

sleep 1
if screen -list | grep -q qemu_debug; then
    echo "QEMU screen session started successfully."
else
    echo "ERROR: Failed to start QEMU screen session."
fi
