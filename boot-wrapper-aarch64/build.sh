#!/bin/bash
#
# build.sh - 构建 boot-wrapper-aarch64
#
# 用法:
#   方式一: 从项目根目录 source envsetup.sh 后运行
#     source build/envsetup.sh
#     lunch quantum_qemu_debug
#     bash src/packages/boot-wrapper-aarch64/build.sh
#
#   方式二: 直接运行，脚本会自动加载构建环境
#     bash src/packages/boot-wrapper-aarch64/build.sh
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOOTWRAPPER_DIR="$SCRIPT_DIR"

# 尝试从项目根目录加载构建环境
if [ -z "$PROJECT_ROOT" ]; then
    # 向上查找项目根目录（包含 build/envsetup.sh 的目录）
    _dir="$BOOTWRAPPER_DIR"
    while [ "$_dir" != "/" ]; do
        if [ -f "$_dir/build/envsetup.sh" ]; then
            PROJECT_ROOT="$_dir"
            break
        fi
        _dir="$(dirname "$_dir")"
    done

    if [ -n "$PROJECT_ROOT" ]; then
        echo "检测到项目根目录: $PROJECT_ROOT"
        _old_pwd="$(pwd)"
        cd "$PROJECT_ROOT"
        source "$PROJECT_ROOT/build/envsetup.sh"
        cd "$_old_pwd"
    else
        echo "错误: 未找到项目根目录，无法加载构建环境"
        echo "请先执行: source <project_root>/build/envsetup.sh"
        exit 1
    fi
fi

# 加载板卡配置（如果未加载）
if [ -z "$BOARD_NAME" ]; then
    # 默认使用 quantum_qemu_debug 板卡配置
    BOARD_CONF_PATH="$PROJECT_ROOT/board/quantum/board_conf/quantum_qemu_debug.conf"
    if [ -f "$BOARD_CONF_PATH" ]; then
        echo "使用默认板卡配置: $BOARD_CONF_PATH"
        source "$BOARD_CONF_PATH"
    else
        echo "错误: 板卡配置文件不存在: $BOARD_CONF_PATH"
        echo "请先执行: lunch <board_name>"
        exit 1
    fi
fi

# 内核构建输出目录（包含 Image 和 DTB 的构建产物）
KERNEL_BUILD_DIR="$BOARD_OUT_DIR/build/linux-6.1"

echo "========================================"
echo "  boot-wrapper-aarch64 构建配置"
echo "========================================"
echo "  BOARD_NAME:        $BOARD_NAME"
echo "  CROSS_COMPILE:     $CROSS_COMPILE"
echo "  KERNEL_BUILD_DIR:  $KERNEL_BUILD_DIR"
echo "  KERNEL_DTB_NAME:   $KERNEL_DTB_NAME"
echo "  KERNEL_CMDLINE:    $KERNEL_CMDLINE"
echo "  BOARD_OUT_DIR:     $BOARD_OUT_DIR"
echo "  ROOTFS_FILE:       $ROOTFS_FILE"
echo "========================================"
echo ""

# ---------- 参数检查 ----------

# 检查交叉编译工具链
TOOLCHAIN_PREFIX="${CROSS_COMPILE%-}"  # 去掉末尾的 '-'
if ! command -v "${CROSS_COMPILE}gcc" &>/dev/null; then
    echo "错误: 找不到交叉编译器 ${CROSS_COMPILE}gcc"
    echo "请确保工具链已安装并在 PATH 中"
    exit 1
fi

# 检查内核构建目录
if [ ! -d "$KERNEL_BUILD_DIR" ]; then
    echo "错误: 内核构建目录不存在: $KERNEL_BUILD_DIR"
    exit 1
fi

# 检查内核镜像
KERNEL_IMAGE="$KERNEL_BUILD_DIR/arch/arm64/boot/Image"
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "错误: 内核镜像不存在: $KERNEL_IMAGE"
    echo "请先构建 Linux 内核"
    exit 1
fi

# 检查 DTB 文件
DTB_FILE="$KERNEL_BUILD_DIR/arch/arm64/boot/dts/${KERNEL_DTB_NAME}.dtb"
if [ ! -f "$DTB_FILE" ]; then
    echo "错误: DTB 文件不存在: $DTB_FILE"
    echo "请先构建 Linux 内核以生成 DTB"
    exit 1
fi

# 检查 rootfs
ROOTFS_FILE="$BOARD_OUT_DIR/images/rootfs.cpio"
if [ ! -f "$ROOTFS_FILE" ]; then
    echo "错误: rootfs 文件不存在: $ROOTFS_FILE"
    echo "请先构建 rootfs"
    exit 1
fi

# 检查 autotools 依赖
for cmd in autoreconf aclocal automake autoconf; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "错误: 找不到 $cmd，请安装 autotools"
        exit 1
    fi
done

# ---------- 构建 ----------

BUILD_DIR="$BOOTWRAPPER_DIR/build"

# 进入 boot-wrapper-aarch64 源码目录
cd "$BOOTWRAPPER_DIR"

# 步骤 1: 运行 autoreconf 生成 configure 脚本
echo "[1/3] 运行 autoreconf -i ..."
autoreconf -i

# 步骤 2: 运行 configure
echo "[2/3] 运行 configure ..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CONFIGURE_ARGS=(
    --host="$TOOLCHAIN_PREFIX"
    --with-kernel-dir="$KERNEL_BUILD_DIR"
    --with-dtb="$DTB_FILE"
    --with-cmdline="$KERNEL_CMDLINE"
    --with-initrd="$ROOTFS_FILE"
    --enable-psci
)

echo "  configure 参数:"
for arg in "${CONFIGURE_ARGS[@]}"; do
    echo "    $arg"
done

"$BOOTWRAPPER_DIR/configure" "${CONFIGURE_ARGS[@]}"

# 步骤 3: 编译
echo "[3/3] 编译 ..."
make -j"$(nproc)"

# ---------- 输出 ----------

OUTPUT_FILE="$BUILD_DIR/linux-system.axf"
if [ ! -f "$OUTPUT_FILE" ]; then
    echo "错误: 构建输出文件不存在: $OUTPUT_FILE"
    exit 1
fi

echo ""
echo "========================================"
echo "  构建成功!"
echo "========================================"
echo "  输出文件: $OUTPUT_FILE"
echo "  文件大小: $(du -h "$OUTPUT_FILE" | cut -f1)"
echo "========================================"

# 如果定义了 BOARD_OUT_DIR，则复制输出
if [ -n "$BOARD_OUT_DIR" ]; then
    mkdir -p "$BOARD_OUT_DIR"
    cp -v "$OUTPUT_FILE" "$BOARD_OUT_DIR/"
    echo "  已复制到:   $BOARD_OUT_DIR/$(basename "$OUTPUT_FILE")"
fi
