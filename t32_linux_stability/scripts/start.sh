#!/usr/bin/env bash
# 读项目根 env.json 启动 TRACE32 模拟器，并等待 Remote API 端口就绪。
#
# 用法:
#   ./scripts/start.sh
#
# 参考 t32_python/ci/start.sh，但单环境（env.json 在项目根）。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

load_env_json

WORK_DIR="$(mktemp -d)"
export T32_WORK_DIR="$WORK_DIR"
echo "work dir: $WORK_DIR"

gen_config_t32 "$WORK_DIR/config.t32" "$T32_SYS" "$T32_PORT" "$T32_PBI" "$T32_RCL"

# startup.cmm：空则由 case 脚本自行初始化
if [[ -n "$STARTUP_CMM" ]]; then
    printf '%s\n' "$STARTUP_CMM" > "$WORK_DIR/startup.cmm"
else
    : > "$WORK_DIR/startup.cmm"
fi

"$T32_BIN" -c "$WORK_DIR/config.t32" -s "$WORK_DIR/startup.cmm" &
T32_PID=$!
echo "TRACE32 started (pid=$T32_PID), waiting for API port $T32_PORT ..."

if wait_port "$T32_NODE" "$T32_PORT"; then
    echo "API port $T32_PORT is ready"
else
    echo "ERROR: API port $T32_PORT not ready within 60s" >&2
    exit 1
fi
