#!/usr/bin/env bash
# 公共 shell 函数库（框架维护者维护）。

# 从环境 env.json 读取字段并导出为 shell 变量（T32_BIN/T32_SYS/T32_PORT/T32_NODE/T32_PBI/T32_RCL/STARTUP_CMM）。
load_env_json() {
    local env="$1"
    eval "$(python3 - "$env" <<'PY'
import json, os, sys
env = sys.argv[1]
d = json.load(open(os.path.join("modules", env, "env.json")))
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

# 解析 --env / --module / --case 通用参数。
# 用法: parse_args "$@"; 之后可用 ENV、MODULE、CASE_FILTER 变量。
#   --env    环境目录（默认 sim_board）
#   --module 功能子目录（可选，缺省跑环境内全部）
#   --case   单条 case 过滤（-k 子串匹配）
parse_args() {
    ENV="sim_board"
    MODULE=""
    CASE_FILTER=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --env) ENV="$2"; shift 2 ;;
            --module) MODULE="$2"; shift 2 ;;
            --case) CASE_FILTER="$2"; shift 2 ;;
            *) echo "未知参数: $1" >&2; exit 2 ;;
        esac
    done
}
