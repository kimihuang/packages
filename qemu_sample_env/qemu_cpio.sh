#!/bin/bash

KERNEL_IMAGE="/home/lion/workdir/sourcecode/quantum_main/out/quantum_qemu_debug/build/linux-6.1/arch/arm64/boot/Image"
DTB="/home/lion/workdir/sourcecode/quantum_main/src/packages/qemu_sample_env/quantum_qemu.dtb"
ROOTFS="/home/lion/workdir/sourcecode/quantum_main/src/packages/qemu_sample_env/rootfs.cpio"
MONITOR="${1:-unix:qemu-monitor.sock,server,nowait}"

qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -nographic \
    -serial mon:stdio \
    -m 512 \
    -kernel "${KERNEL_IMAGE}" \
    -dtb "${DTB}" \
    -initrd "${ROOTFS}" \
    -append "console=ttyAMA0 init=/init nokaslr earlycon=pl011,0x9000000 debug loglevel=8" \
    -monitor "${MONITOR}"
