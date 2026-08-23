#!/usr/bin/env bash
# 读 ci_testcase_seq.json 按声明顺序跑测试，生成 JUnit XML + HTML 报告。
#
# 用法（先启动模拟器）:
#   ./ci/start.sh --env sim_board
#   ./ci/run.sh --env sim_board [--module cpu] [--case smoke]
#
# 环境变量:
#   T32_VENV   .venv 路径（默认用 PATH 中的 pytest）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."
source "$SCRIPT_DIR/lib.sh"

parse_args "$@"

export T32_ENV="$ENV"

# 定位 pytest
if [[ -n "${T32_VENV:-}" ]]; then
    PYTEST=( "$T32_VENV/bin/python3" -m pytest )
elif command -v pytest >/dev/null 2>&1; then
    PYTEST=( pytest )
else
    echo "ERROR: 找不到 pytest，请设置 T32_VENV 或安装 pytest" >&2
    exit 1
fi

PYTEST_ARGS=( -q --junitxml="report_${ENV}.xml" --html="report_${ENV}.html" --self-contained-html --timeout=600 )

# --module 过滤功能子目录（case id 形如 "<module>/<name>"，用 "<module>/" 前缀匹配）
# --case 过滤单条 case（-k 子串匹配）
FILTER=""
if [[ -n "$MODULE" ]]; then
    FILTER="$MODULE/"
fi
if [[ -n "$CASE_FILTER" ]]; then
    FILTER="${FILTER}${CASE_FILTER}"
fi
if [[ -n "$FILTER" ]]; then
    PYTEST_ARGS+=( -k "$FILTER" )
fi

# 只收集 case 入口（test_cases.py）
"${PYTEST[@]}" "${PYTEST_ARGS[@]}" test_cases.py
