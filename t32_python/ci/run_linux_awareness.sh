#!/usr/bin/env bash
# 运行 Linux OS Awareness 测试并生成 JUnit XML 与 HTML 报告。
#
# 用法（先启动模拟器）:
#   ./ci/start_linux_sim.sh
#   ./ci/run_linux_awareness.sh
#
# 环境变量:
#   T32_NODE / T32_PORT / T32_ARMv8_DEMO_DIR / T32_LINUX_BOOT_TIMEOUT
set -euo pipefail

export T32_NODE="${T32_NODE:-localhost}"
export T32_PORT="${T32_PORT:-20000}"

cd "$(dirname "$0")/.."

pytest -q \
  --junitxml=report_linux.xml \
  --html=report_linux.html --self-contained-html \
  --timeout=600 \
  tests/test_linux_awareness.py
