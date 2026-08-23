#!/usr/bin/env bash
# 运行 pytest 冒烟测试并生成 JUnit XML 与 HTML 报告。
#
# 用法（先确保 TRACE32 已启动并开启 Remote API 端口）:
#   ./ci/start_sim.sh   # 模拟器场景：拉起模拟器并等待端口
#   ./ci/run.sh         # 跑测试
#
# 环境变量:
#   T32_TARGET  sim | hw（默认 sim）
#   T32_NODE / T32_PORT / T32_CPU / T32_RAM_ADDR
set -euo pipefail

export T32_TARGET="${T32_TARGET:-sim}"
export T32_NODE="${T32_NODE:-localhost}"
export T32_PORT="${T32_PORT:-20000}"

cd "$(dirname "$0")/.."

pytest -q --junitxml=report.xml --html=report.html --self-contained-html --timeout=300 tests/
