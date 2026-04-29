#!/bin/bash
# create_rootfs.sh – assemble a minimal cpio initramfs from built artefacts
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
STAGING="$PROJECT_DIR/staging"

ARCH="${ARCH:-arm64}"
CROSS="${CROSS:-aarch64-linux-gnu-}"

# Cross-compiled shared libraries that user-space binaries may need
# (adjust to match your sysroot / toolchain)
SYSROOT="/usr/${CROSS}"

# ---- Clean previous staging area ----
rm -rf "$STAGING"

# ---- Directory skeleton ----
mkdir -p "$STAGING"/{bin,sbin,lib,lib64,proc,sys,dev,etc,tmp,modules,usr/bin,usr/lib,usr/sbin}

# ---- Copy kernel modules ----
if ls "$PROJECT_DIR"/*.ko 1>/dev/null 2>&1; then
    cp "$PROJECT_DIR"/*.ko "$STAGING/modules/"
    # Create depmod-friendly module layout
    KERNEL_VER="${KERNEL_VER:-6.6.0-dmabuf-demo}"
    MODULE_DIR="$STAGING/lib/modules/$KERNEL_VER"
    mkdir -p "$MODULE_DIR"
    cp "$PROJECT_DIR"/*.ko "$MODULE_DIR/"
    if command -v ${CROSS}depmod &>/dev/null; then
        ${CROSS}depmod -b "$STAGING" -a "$KERNEL_VER" 2>/dev/null || true
    fi
fi

# ---- Copy user-space binaries ----
for bin in demo_app/demo_app demo_test/test_dmabuf demo_app/sync_file_info; do
    if [ -f "$PROJECT_DIR/$bin" ]; then
        cp "$PROJECT_DIR/$bin" "$STAGING/usr/bin/"
        chmod 755 "$STAGING/usr/bin/$(basename "$bin")"
    fi
done

# ---- Copy required shared libraries ----
if [ -d "$SYSROOT/lib" ]; then
    # Use the cross-toolchain's ldd equivalent to discover dependencies
    for bin in "$STAGING"/usr/bin/*; do
        [ -f "$bin" ] || continue
        # Try to resolve with objdump (works for cross builds)
        deps=$(${CROSS}objdump -p "$bin" 2>/dev/null \
                | awk '/NEEDED/ {print $2}' || true)
        for lib in $deps; do
            # Search in sysroot
            found=$(find "$SYSROOT/lib" "$SYSROOT/usr/lib" -name "$lib" -type f 2>/dev/null | head -1)
            if [ -n "$found" ]; then
                dest_dir="$STAGING/lib/$(dirname "${found#$SYSROOT/lib/}")"
                mkdir -p "$dest_dir"
                cp "$found" "$dest_dir/"
            fi
        done
    done
fi

# ---- Copy dynamic linker ----
LINKER=$(${CROSS}gcc -print-file-name=ld-linux-aarch64.so.1 2>/dev/null || true)
if [ -n "$LINKER" ] && [ -f "$LINKER" ]; then
    cp "$LINKER" "$STAGING/lib/"
fi

# ---- Device nodes ----
mknod -m 622 "$STAGING/dev/console" c 5 1 2>/dev/null || true
mknod -m 666 "$STAGING/dev/null"    c 1 3 2>/dev/null || true
mknod -m 666 "$STAGING/dev/zero"    c 1 5 2>/dev/null || true

# ---- Minimal init script ----
cat > "$STAGING/init" << 'INITEOF'
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

mount -t proc     proc     /proc
mount -t sysfs    sysfs    /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true

# Load kernel modules
for mod in /lib/modules/*/*.ko; do
    [ -f "$mod" ] && insmod "$mod" && echo "Loaded $(basename $mod)"
done

echo "========================================"
echo "  DMA-BUF demo environment ready"
echo "========================================"
echo

# Drop to interactive shell
exec /bin/sh
INITEOF
chmod 755 "$STAGING/init"

# ---- Build cpio ----
(cd "$STAGING" && find . | cpio -o -H newc 2>/dev/null | gzip > "$PROJECT_DIR/rootfs.cpio")

echo "rootfs.cpio created  ($(du -h "$PROJECT_DIR/rootfs.cpio" | cut -f1))"
