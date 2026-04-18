import pytest
import select
from labgrid.driver.qemudriver import QEMUDriver
from labgrid.driver.shelldriver import ShellDriver


@pytest.fixture(scope="session")
def qemu_env(target):
    """启动 QEMU 并等待 shell 就绪，返回 target 用于后续操作"""
    qemu = target.get_driver(QEMUDriver, activate=False)
    shell = target.get_driver(ShellDriver, activate=False)

    # activate QEMUDriver first, then start QEMU, then activate ShellDriver
    target.activate(qemu)
    qemu.on()

    # drain initial boot output until console is idle
    while True:
        ready, _, _ = select.select([qemu._clientsocket], [], [], 2)
        if not ready:
            break
        qemu._clientsocket.recv(4096)

    target.activate(shell)
    shell._await_login()

    yield target

    qemu.off()


class TestQEMU:
    def test_qemu_monitor_version(self, qemu_env):
        """通过 QEMU QMP monitor 查看 QEMU 版本"""
        qemu = qemu_env.get_driver(QEMUDriver)
        result = qemu.monitor_command("query-version")
        assert "qemu" in result
        major = result["qemu"]["major"]
        minor = result["qemu"]["minor"]
        print(f"QEMU Version: {major}.{minor}")

    def test_cpu_info(self, qemu_env):
        """查看 CPU 版本信息"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cat /proc/cpuinfo")
        assert len(output) > 0
        print("CPU Info:")
        for line in output:
            print(line)

    def test_kernel_version(self, qemu_env):
        """查看内核版本"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("uname -a")
        assert len(output) > 0
        print(f"Kernel: {output[0]}")
