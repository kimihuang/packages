"""pytest 集成：fixture 与 case 参数化（脚本即 case，json 声明顺序）。

通过环境变量 T32_ENV 指定要运行的环境（默认 sim_board）：
- env_config：加载 modules/<env>/env.json
- dbg：基于 env_config 建立 session 级连接
- case_item：由 ci_testcase_seq.json 声明的 case 参数化而来，供 test_cases.py::test_case 使用
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from framework.config import load_env  # noqa: E402
from framework.runner import load_seq, reset_seq_state  # noqa: E402
from framework.session import connect_target  # noqa: E402

ENV = os.environ.get("T32_ENV", "sim_board")


@pytest.fixture(scope="session")
def env_config():
    """环境启动配置。"""
    return load_env(ENV)


@pytest.fixture(scope="session")
def dbg(env_config):
    """TRACE32 连接（session 级，测试结束后断开）。"""
    reset_seq_state()  # 重置 critical 失败状态
    dbg = connect_target(env_config)
    try:
        yield dbg
    finally:
        dbg.disconnect()


def pytest_generate_tests(metafunc):
    """把 ci_testcase_seq.json 声明的 case 参数化为 case_item（跳过 disabled）。"""
    if "case_item" in metafunc.fixturenames:
        cases = [c for c in load_seq(ENV) if c.get("enabled", True)]
        metafunc.parametrize("case_item", cases, ids=lambda c: c["name"])
