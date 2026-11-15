#!/usr/bin/env bash
# 连接 TRACE32 执行 cases/<name>.cmm；若 API 端口未就绪，自动先启动 TRACE32。
#
# 用法:
#   ./scripts/run.sh --case boot_to_shell
#   ./scripts/run.sh --case inject_panic --timeout 180
#
# 环境变量:
#   T32_VENV  .venv 路径（装了 PYRCL，默认用 PATH 中的 python3）
#
# 参考 t32_python/ci/run.sh，但这里不跑 pytest，直接由 run_case.py 执行单条 case。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

parse_args "$@"

if [[ -z "$CASE_FILTER" ]]; then
    echo "ERROR: 请用 --case <name> 指定要跑的 case（cases/<name>.cmm）" >&2
    echo "可选 case:" >&2
    ls -1 "$SCRIPT_DIR/../cases"/*.cmm 2>/dev/null | sed 's#.*/##; s#\.cmm$##' | sed 's/^/  /' >&2
    exit 2
fi

load_env_json

# 端口未就绪则先启动 TRACE32
if ! wait_port "$T32_NODE" "$T32_PORT" 1; then
    echo "API port $T32_PORT 未就绪，先启动 TRACE32 ..."
    "$SCRIPT_DIR/start.sh"
fi

# 定位带 PYRCL 的 python3：1) $T32_VENV  2) 自动向上探测 .venv  3) PATH 里的 python3
PY=""
if [[ -n "${T32_VENV:-}" && -x "$T32_VENV/bin/python3" ]]; then
    PY="$T32_VENV/bin/python3"
else
    _d="$PROJECT_ROOT"
    for _i in 1 2 3 4 5; do
        _d="$_d/.."
        if [[ -x "$_d/.venv/bin/python3" ]]; then
            PY="$_d/.venv/bin/python3"
            break
        fi
    done
fi
if [[ -z "$PY" ]] && command -v python3 >/dev/null 2>&1; then
    PY="$(command -v python3)"
fi
if [[ -z "$PY" ]]; then
    echo "ERROR: 找不到 python3，请设置 T32_VENV" >&2
    exit 1
fi
if ! "$PY" -c "import lauterbach.trace32.rcl" 2>/dev/null; then
    echo "ERROR: $PY 未安装 PYRCL (lauterbach.trace32.rcl)" >&2
    echo "  安装: $PY -m pip install ../t32_python/tools/lauterbach_trace32_rcl-1.0.7-py3-none-any.whl" >&2
    echo "  或设置 T32_VENV 指向已装 PYRCL 的 venv" >&2
    exit 1
fi
echo "using python: $PY"

"$PY" "$SCRIPT_DIR/run_case.py" "$CASE_FILTER" \
    --node "$T32_NODE" --port "$T32_PORT" --rcl "$T32_RCL" --timeout "$TIMEOUT"
