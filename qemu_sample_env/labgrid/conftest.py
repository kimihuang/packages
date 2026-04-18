import pytest
import select
import logging
from labgrid.driver.qemudriver import QEMUDriver
from labgrid.driver.shelldriver import ShellDriver

logger = logging.getLogger("labgrid.console")


@pytest.fixture(scope="session")
def qemu_env(target):
    """启动 QEMU 并等待 shell 就绪，返回 target 用于后续操作"""
    qemu = target.get_driver(QEMUDriver, activate=False)
    shell = target.get_driver(ShellDriver, activate=False)

    target.activate(qemu)
    qemu.on()

    # drain initial boot output, 同时记录到 logger
    while True:
        ready, _, _ = select.select([qemu._clientsocket], [], [], 2)
        if not ready:
            break
        data = qemu._clientsocket.recv(4096)
        text = data.decode("utf-8", errors="replace")
        for line in text.splitlines():
            logger.info(line)

    target.activate(shell)
    shell._await_login()

    yield target

    qemu.off()
