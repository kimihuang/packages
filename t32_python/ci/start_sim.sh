#!/usr/bin/env bash
# 启动 TRACE32 指令集模拟器，并等待 Remote API 端口就绪。
#
# 用法:
#   T32_BIN=/path/to/t32marm [T32_CPU=CortexA15] [T32_PORT=20000] ./ci/start_sim.sh
#
# 环境变量:
#   T32_BIN     TRACE32 可执行文件路径（必填，如 <T32>/bin/pc_linux64/t32marm）
#   T32_CPU     模拟 CPU 类型（默认 CortexA15）
#   T32_PORT    Remote API 端口（默认 20000）
#   T32_NODE    监听节点（默认 localhost）
set -euo pipefail

T32_BIN="${T32_BIN:-/home/lion/t32/bin/pc_linux64/t32marm}"
T32_CPU="${T32_CPU:-CortexA53}"
T32_PORT="${T32_PORT:-20000}"
T32_NODE="${T32_NODE:-localhost}"

WORK_DIR="$(mktemp -d)"
export T32_WORK_DIR="$WORK_DIR"
echo "work dir: $WORK_DIR"

# 生成 config.t32：开启 Remote API TCP 端口
cat > "$WORK_DIR/config.t32" <<EOF
RCL=NETTCP
PORT=$T32_PORT
EOF

# 生成 startup.cmm：选择模拟 CPU 并上电
cat > "$WORK_DIR/startup.cmm" <<EOF
SYStem.CPU $T32_CPU
SYStem.Up
EOF

# 启动模拟器
"$T32_BIN" -c "$WORK_DIR/config.t32" -s "$WORK_DIR/startup.cmm" &
T32_PID=$!
echo "TRACE32 started (pid=$T32_PID), waiting for API port $T32_PORT ..."

# 轮询端口就绪（最多 60s）
for _ in $(seq 1 60); do
    if (exec 3<>"/dev/tcp/$T32_NODE/$T32_PORT") 2>/dev/null; then
        exec 3>&- 3<&-
        echo "API port $T32_PORT is ready"
        exit 0
    fi
    sleep 1
done

echo "ERROR: API port $T32_PORT not ready within 60s" >&2
exit 1
