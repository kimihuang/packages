# demo_dmabuf

DMA-BUF kernel module demo project with cross-compilation support and QEMU
targeting aarch64.

## Project Structure

```
demo_dmabuf/
├── Makefile                  # Top-level build (kernel modules + user-space)
├── configs/
│   └── guest_kernel.config   # Kernel config fragment for DMA-BUF support
├── scripts/
│   ├── build.sh              # Convenience build wrapper
│   ├── create_rootfs.sh      # Assemble minimal cpio initramfs
│   └── run_qemu.sh           # QEMU launch script
├── demo_heap/                # Custom DMA-BUF heap kernel module
├── demo_exporter/            # DMA-BUF exporter kernel module
├── demo_importer/            # DMA-BUF importer kernel module
├── demo_app/                 # User-space test application
├── demo_test/                # Additional user-space tests
└── dma-buf/                  # Shared headers / utilities
```

## Prerequisites

- Linux kernel headers for the target architecture
- Cross-compiler toolchain (default: `aarch64-linux-gnu-gcc`)
- QEMU: `qemu-system-aarch64`

## Build

```bash
# Build everything (kernel modules + user-space) with default cross settings
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/kernel/build

# Or use the convenience script
./scripts/build.sh
```

## Create Rootfs & Run

```bash
# Build modules and user-space, then package into a cpio archive
make rootfs

# Launch under QEMU (requires a built guest kernel Image)
KERNEL_IMAGE=/path/to/Image ./scripts/run_qemu.sh
```

## Configuration

| Variable            | Default                    | Description                          |
|---------------------|----------------------------|--------------------------------------|
| `ARCH`              | `arm64`                    | Target architecture                  |
| `CROSS_COMPILE`     | `aarch64-linux-gnu-`       | Cross-compiler prefix                |
| `KDIR`              | `/lib/modules/$(uname -r)/build` | Kernel build directory          |
| `KERNEL_IMAGE`      | `../guest_kernel/Image`    | Path to guest kernel Image           |
| `KERNEL_VER`        | `6.6.0-dmabuf-demo`        | Kernel version for module layout     |

## License

SPDX-License-Identifier: GPL-2.0
