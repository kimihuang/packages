#!/bin/bash
# 测试报告生成脚本
# 使用 pytest-html 生成看板报告

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_PYTEST="/home/lion/workdir/sourcecode/quantum_main/.venv/bin/pytest"
ENV="labgrid-env.yaml"
REPORT_DIR="${SCRIPT_DIR}/report"

rm -rf "${REPORT_DIR}"
mkdir -p "${REPORT_DIR}"

echo "========================================"
echo "  Running labgrid QEMU tests..."
echo "========================================"

cd "${SCRIPT_DIR}"

${VENV_PYTEST} \
    -v \
    --lg-env "${ENV}" \
    --html="${REPORT_DIR}/report.html" \
    --self-contained-html \
    --json-report \
    --json-report-file="${REPORT_DIR}/report.json" \
    --tb=short \
    -s \
    2>&1 | tee "${REPORT_DIR}/console.log"

EXIT_CODE=${PIPESTATUS[0]}

echo ""
echo "========================================"
if [ ${EXIT_CODE} -eq 0 ]; then
    echo "  ALL TESTS PASSED"
else
    echo "  SOME TESTS FAILED (exit code: ${EXIT_CODE})"
fi
echo "========================================"
echo ""

# 打印摘要
if command -v python3 &>/dev/null; then
    python3 -c "
import json
with open('${REPORT_DIR}/report.json') as f:
    data = json.load(f)

summary = data.get('summary', {})
total = summary.get('total', 0)
passed = summary.get('passed', 0)
failed = summary.get('failed', 0)
error = summary.get('error', 0)
duration = data.get('duration', 0)

print('Test Summary')
print(f'  Total:   {total}')
print(f'  Passed:  {passed}')
print(f'  Failed:  {failed}')
print(f'  Error:   {error}')
print(f'  Time:    {duration:.2f}s')

if failed > 0 or error > 0:
    print()
    print('Failures:')
    for t in data.get('tests', []):
        if t.get('outcome') in ('failed', 'error'):
            print(f'  - {t.get(\"nodeid\", \"\")}')
"
fi

echo ""
echo "Report: ${REPORT_DIR}/report.html"

exit ${EXIT_CODE}
