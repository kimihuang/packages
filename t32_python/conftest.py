"""pytest 配置：提供 session 级 ``t32`` fixture。

连接参数通过环境变量配置（见 ``t32session.connect_target``）：
    T32_TARGET / T32_NODE / T32_PORT / T32_CPU
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from t32session import connect_target  # noqa: E402


@pytest.fixture(scope="session")
def t32():
    """建立 TRACE32 连接，测试会话结束后断开。"""
    dbg = connect_target()
    try:
        yield dbg
    finally:
        dbg.disconnect()
