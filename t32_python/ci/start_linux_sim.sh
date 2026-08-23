#!/usr/bin/env bash
# 启动 TRACE32 模拟器（供 Linux OS Awareness demo 使用），并等待 Remote API 端口就绪。
#
# 与 start_sim.sh 的区别：startup.cmm 留空，不预设 CPU，
# 由 armv8_sim_linux.cmm 自行完成 SYStem.CPU CortexA53 等初始化。
#
# 用法:
#   ./ci/start_linux_sim.sh
#
# 环境变量:
#   T32_BIN     TRACE32 可执行文件路径（默认 ~/t32/bin/pc_linux64/t32marm）
#   T32_SYS     TRACE32 系统目录（默认 ~/t32）
#   T32_PORT    Remote API 端口（默认 20000）
#   T32_NODE    监听节点（默认 localhost）
set -euo pipefail

T32_BIN="${T32_BIN:-$HOME/t32/bin/pc_linux64/t32marm}"
T32_SYS="${T32_SYS:-$HOME/t32}"
T32_PORT="${T32_PORT:-20000}"
T32_NODE="${T32_NODE:-localhost}"

WORK_DIR="$(mktemp -d)"
export T32_WORK_DIR="$WORK_DIR"
echo "work dir: $WORK_DIR"

# 生成 config.t32：模拟器模式 + Remote API TCP 端口
# 注意：config.t32 各配置块之间必须用空行分隔（TRACE32 的 mandatory blank line）
cat > "$WORK_DIR/config.t32" <<EOF
OS=
SYS=$T32_SYS
TMP=$T32_SYS/tmp

PBI=SIM

SCREEN=

RCL=NETTCP
PORT=$T32_PORT
EOF

# startup.cmm 留空：demo 脚本自行初始化 CPU
: > "$WORK_DIR/startup.cmm"

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
