"""TRACE32 会话连接封装。

基于 lauterbach.trace32.rcl (PYRCL)，提供硬件/模拟器统一的连接入口。

用法示例::

    from t32session import connect_target

    # 连接指令集模拟器
    with connect_target(target="sim", cpu="CortexA15") as dbg:
        dbg.cmd("Data.Set D:0x20000000 %LE 0x12345678")

    # 连接真实硬件
    with connect_target(target="hw", cpu="CortexA55") as dbg:
        dbg.ping()

环境变量（参数未显式传入时生效）:
    T32_TARGET  目标类型: sim | hw（默认 sim）
    T32_NODE    远程节点（默认 localhost）
    T32_PORT    Remote API 端口（默认 20000）
    T32_CPU     目标 CPU 类型（默认 CortexA15）
"""
import os

import lauterbach.trace32.rcl as t32

DEFAULT_CPU = "CortexA15"


def connect_target(target=None, node=None, port=None, cpu=None, timeout=60.0):
    """建立 TRACE32 连接并按目标类型完成初始化，返回 Debugger。

    Args:
        target: 目标类型，"sim" 或 "hw"。
        node:   Remote API 节点。
        port:   Remote API 端口。
        cpu:    目标 CPU 类型。
        timeout: 连接建立超时（秒）。

    Returns:
        lauterbach.trace32.rcl.Debugger

    Raises:
        ValueError: target 取值非法。
    """
    target = target or os.environ.get("T32_TARGET", "sim")
    node = node or os.environ.get("T32_NODE", "localhost")
    port = int(port or os.environ.get("T32_PORT", "20000"))
    cpu = cpu or os.environ.get("T32_CPU", DEFAULT_CPU)

    if target not in ("sim", "hw"):
        raise ValueError("target must be 'sim' or 'hw', got {!r}".format(target))

    dbg = t32.connect(node=node, port=str(port), protocol="TCP", timeout=timeout)

    if target == "sim":
        dbg.cmd("SYStem.CPU {}".format(cpu))
        dbg.cmd("SYStem.Up")
    else:
        dbg.cmd("SYStem.CPU {}".format(cpu))
        dbg.cmd("SYStem.Attach")

    return dbg
