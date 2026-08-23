"""Linux OS Awareness 冒烟测试：在 TRACE32 模拟器上启动 Linux 内核。

依赖 demo 脚本 armv8_sim_linux.cmm 及其配套文件（kernel/vmlinux、
ramdisk/rootfs.ext4、simul/*.so 等），默认位于
~/t32/demo/arm/kernel/linux/board/simulator/armv8/，可用 T32_ARMv8_DEMO_DIR 覆盖。

注意：该 demo 会自行完成 SYStem.CPU CortexA53、SIM 外设加载、内核加载、
MMU/OS Awareness 配置与 Go，故本测试的 fixture 不预设 CPU（区别于基础冒烟）。
"""
import os
import time

import pytest

import lauterbach.trace32.rcl as t32

ARMv8_DEMO_DIR = os.environ.get(
    "T32_ARMv8_DEMO_DIR",
    os.path.expanduser("~/t32/demo/arm/kernel/linux/board/simulator/armv8"),
)
# 脚本执行 + Linux 启动等待的总超时（秒）
BOOT_TIMEOUT = int(os.environ.get("T32_LINUX_BOOT_TIMEOUT", "300"))
# Linux 完整启动判定的最小任务数（内核线程 + 用户空间进程均就绪）
MIN_TASKS = int(os.environ.get("T32_LINUX_MIN_TASKS", "10"))

# demo 运行所必需的配套文件
REQUIRED_FILES = (
    "kernel/vmlinux",
    "kernel/trace32_simulator.dtb",
    "ramdisk/rootfs.ext4",
)


@pytest.fixture(scope="module")
def linux_t32():
    """独立的 TRACE32 连接（不预设 CPU，由 demo 脚本自行配置）。"""
    node = os.environ.get("T32_NODE", "localhost")
    port = int(os.environ.get("T32_PORT", "20000"))
    dbg = t32.connect(node=node, port=str(port), protocol="TCP")
    try:
        yield dbg
    finally:
        dbg.disconnect()


def _wait_for(predicate, timeout, interval=2.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return False


def test_linux_boot_and_awareness(linux_t32):
    dbg = linux_t32

    # 前置检查：demo 配套文件必须存在，否则脚本会弹出 DIALOG 阻塞
    for f in REQUIRED_FILES:
        assert os.path.isfile(os.path.join(ARMv8_DEMO_DIR, f)), (
            "缺少 demo 依赖文件: {}/{}".format(ARMv8_DEMO_DIR, f)
        )

    # 1) 切到 demo 目录：脚本内 ~~~~ 相对路径依赖于此
    dbg.cmd("CD {}".format(ARMv8_DEMO_DIR))

    # 2) 执行 demo 脚本（阻塞直到脚本结束；脚本末尾 Go 后 ENDDO）
    dbg.cmm("armv8_sim_linux.cmm", timeout=BOOT_TIMEOUT)

    # 3) OS Awareness 就绪（task_count > 0）
    assert _wait_for(lambda: dbg.fnc.task_count() > 0, timeout=BOOT_TIMEOUT), (
        "OS Awareness 未就绪（task_count 仍为 0）"
    )
    initial_tasks = dbg.fnc.task_count()

    # 4) Linux 完整启动：任务数增长到阈值（内核线程 + 用户空间进程均起来）
    assert _wait_for(lambda: dbg.fnc.task_count() >= MIN_TASKS, timeout=BOOT_TIMEOUT), (
        "Linux 未在 {}s 内完整启动（task_count={}，期望 >= {}）".format(
            BOOT_TIMEOUT, dbg.fnc.task_count(), MIN_TASKS
        )
    )

    # 5) 目标仍处于运行状态
    assert dbg.fnc.state_run(), "Linux 未处于运行状态"

    print("Linux boot OK: task_count {} -> {}".format(initial_tasks, dbg.fnc.task_count()))
