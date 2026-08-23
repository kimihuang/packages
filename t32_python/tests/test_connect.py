"""冒烟测试：连接与基础查询。"""
import os


def test_ping(t32):
    """连接健康检查。"""
    t32.ping()


def test_software_version(t32):
    """软件版本可读取。"""
    assert t32.fnc.software_version()


def test_interface_matches_target(t32):
    """INTERFACE.SIM() 与 T32_TARGET 一致。"""
    is_sim = os.environ.get("T32_TARGET", "sim") == "sim"
    assert t32.fnc.interface_sim() is is_sim
