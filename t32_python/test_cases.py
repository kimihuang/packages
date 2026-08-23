"""case 分发入口（框架维护者维护，用户无需修改）。

由 conftest.py 的 pytest_generate_tests 将 ci_testcase_seq.json 声明的 case
参数化为 case_item，此处统一分发到 framework.runner.run_case。
"""
import pytest

from framework.runner import CaseSkippedError, run_case


def test_case(dbg, env_config, case_item):
    try:
        run_case(dbg, env_config, case_item)
    except CaseSkippedError as e:
        pytest.skip(str(e))
