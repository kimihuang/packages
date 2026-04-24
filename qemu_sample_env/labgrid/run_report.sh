#!/bin/bash
# 测试报告生成脚本
# 使用 allure-pytest + allure CLI 生成看板报告

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_PYTEST="/home/lion/workdir/sourcecode/quantum_main/.venv/bin/pytest"
VENV_ALLURE="${SCRIPT_DIR}/tools/allure-2.29.0/bin/allure"
ENV="labgrid-env.yaml"
ALLURE_DIR="${SCRIPT_DIR}/allure-results"
ALLURE_REPORT="${SCRIPT_DIR}/allure-report"

# 安装 allure CLI（如果不存在）
ensure_allure() {
    if [ -x "${VENV_ALLURE}" ]; then
        return 0
    fi
    local tar="/tmp/allure-2.29.0.tgz"
    if [ ! -f "${tar}" ] || [ "$(wc -c < "${tar}")" -lt 10000000 ]; then
        echo "Downloading allure CLI..."
        curl -L --connect-timeout 10 --max-time 600 \
            "https://github.com/allure-framework/allure2/releases/download/2.29.0/allure-2.29.0.tgz" \
            -o "${tar}"
    fi
    mkdir -p "${SCRIPT_DIR}/tools"
    tar -xzf "${tar}" -C "${SCRIPT_DIR}/tools/"
    chmod +x "${VENV_ALLURE}"
    echo "Allure CLI installed to ${VENV_ALLURE}"
}

# 清理旧数据
rm -rf "${ALLURE_DIR}" "${ALLURE_REPORT}"
mkdir -p "${ALLURE_DIR}"

echo "========================================"
echo "  Running labgrid QEMU tests..."
echo "========================================"

cd "${SCRIPT_DIR}"

${VENV_PYTEST} \
    -v \
    --lg-env "${ENV}" \
    --alluredir="${ALLURE_DIR}" \
    --clean-alluredir \
    --tb=short \
    -s \
    2>&1 | tee "${ALLURE_DIR}/console.log"

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

# 生成 allure 报告
echo "Generating allure report..."
ensure_allure
"${VENV_ALLURE}" generate "${ALLURE_DIR}" -o "${ALLURE_REPORT}" --clean

echo ""
echo "========================================"
echo "  Report: file://${ALLURE_REPORT}/index.html"
echo "========================================"

exit ${EXIT_CODE}
