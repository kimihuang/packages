#!/bin/bash
#
# qemu_boot_wrapper.sh - 使用 QEMU 运行 boot-wrapper 生成的 linux-system.axf
#
# 用法:
#   source build/envsetup.sh
#   lunch quantum_qemu_debug
#   qemu_boot_wrapper
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 尝试从项目根目录加载构建环境
if [ -z "$PROJECT_ROOT" ]; then
    _dir="$SCRIPT_DIR"
    while [ "$_dir" != "/" ]; do
        if [ -f "$_dir/build/envsetup.sh" ]; then
            PROJECT_ROOT="$_dir"
            break
        fi
        _dir="$(dirname "$_dir")"
    done

    if [ -n "$PROJECT_ROOT" ]; then
        _old_pwd="$(pwd)"
        cd "$PROJECT_ROOT"
        source "$PROJECT_ROOT/build/envsetup.sh"
        cd "$_old_pwd"
    else
        echo "错误: 未找到项目根目录，无法加载构建环境"
        exit 1
    fi
fi

# 加载板卡配置
if [ -z "$BOARD_NAME" ]; then
    BOARD_CONF_PATH="$PROJECT_ROOT/board/quantum/board_conf/quantum_qemu_debug.conf"
    if [ -f "$BOARD_CONF_PATH" ]; then
        source "$BOARD_CONF_PATH"
    else
        echo "错误: 板卡配置文件不存在: $BOARD_CONF_PATH"
        exit 1
    fi
fi

# 加载 qemu 模块（获取 BOARD_OUT_DIR 等变量的补充）
if [ -f "$PROJECT_ROOT/scripts/qemu/qemu.sh" ]; then
    source "$PROJECT_ROOT/scripts/qemu/qemu.sh"
fi

# 设置路径
BOOT_WRAPPER_IMAGE="$BOARD_OUT_DIR/linux-system.axf"

# 参数检查
if [ ! -f "$BOOT_WRAPPER_IMAGE" ]; then
    echo "错误: boot-wrapper 镜像不存在: $BOOT_WRAPPER_IMAGE"
    echo "请先运行: bash src/packages/boot-wrapper-aarch64/build.sh"
    exit 1
fi

echo "========================================"
echo "  QEMU 启动 boot-wrapper 镜像"
echo "========================================"
echo "  镜像:   $BOOT_WRAPPER_IMAGE"
echo "  大小:   $(du -h "$BOOT_WRAPPER_IMAGE" | cut -f1)"
echo "  机器:   $QEMU_MACHINE (secure=on, virtualization=on)"
echo "  CPU:    $QEMU_CPU"
echo "  SMP:    $QEMU_SMP"
echo "  内存:   ${QEMU_MEM}M"
echo "========================================"
echo ""

qemu-system-aarch64 \
    -M "${QEMU_MACHINE},secure=on,virtualization=on" \
    -cpu "$QEMU_CPU" \
    -smp "$QEMU_SMP" \
    -m "$QEMU_MEM" \
    -nographic \
    -kernel "$BOOT_WRAPPER_IMAGE"
