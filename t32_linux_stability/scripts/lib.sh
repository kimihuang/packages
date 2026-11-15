#!/usr/bin/env bash
# 公共 shell 函数库（参考 t32_python/ci/lib.sh，适配单环境 + --case 选择）。

# 项目根目录（scripts/ 的上一级）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 读项目根 env.json，导出 shell 变量（T32_BIN/T32_SYS/T32_PORT/T32_NODE/T32_PBI/T32_RCL/STARTUP_CMM）。
load_env_json() {
    eval "$(python3 - "$PROJECT_ROOT/env.json" <<'PY'
import json, os, sys
d = json.load(open(sys.argv[1]))
print("T32_BIN=%r" % os.path.expanduser(d["t32_bin"]))
print("T32_SYS=%r" % os.path.expanduser(d["t32_sys"]))
print("T32_PORT=%r" % str(d.get("port", 20000)))
print("T32_NODE=%r" % d.get("node", "localhost"))
print("T32_PBI=%r" % d.get("pbi", "SIM"))
print("T32_RCL=%r" % d.get("rcl", "NETTCP"))
print("STARTUP_CMM=%r" % d.get("startup_cmm", ""))
PY
)"
}

# 生成 config.t32（TRACE32 配置块之间必须用空行分隔）。
gen_config_t32() {
    local config_path="$1" sys_path="$2" port="$3" pbi="${4:-SIM}" rcl="${5:-NETTCP}"
    cat > "$config_path" <<EOF
OS=
SYS=$sys_path
TMP=$sys_path/tmp

PBI=$pbi

SCREEN=

RCL=$rcl
PORT=$port
EOF
}

# 等待端口就绪；返回 0 就绪，1 超时。
wait_port() {
    local node="$1" port="$2" timeout="${3:-60}"
    local i
    for ((i = 0; i < timeout; i++)); do
        if (exec 3<>"/dev/tcp/$node/$port") 2>/dev/null; then
            exec 3>&- 3<&-
            return 0
        fi
        sleep 1
    done
    return 1
}

# 解析参数。用法: parse_args "$@"; 之后可用 CASE_FILTER、TIMEOUT 变量。
#   --case    要跑的 case 名（对应 cases/<name>.cmm，可省略 .cmm 后缀）
#   --timeout 单 case 超时秒数（默认 120）
parse_args() {
    CASE_FILTER=""
    TIMEOUT=120
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --case) CASE_FILTER="$2"; shift 2 ;;
            --timeout) TIMEOUT="$2"; shift 2 ;;
            *) echo "未知参数: $1" >&2; exit 2 ;;
        esac
    done
}
