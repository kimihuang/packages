"""进程和服务测试"""

import allure
from labgrid.driver.shelldriver import ShellDriver


@allure.feature("Process")
class TestProcess:
    def test_init_process(self, qemu_env):
        """确认 init 进程 (PID 1) 存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | head -2")
        assert len(output) >= 2
        allure.attach(output[1].strip(), name="init", attachment_type=allure.attachment_type.TEXT)

    def test_process_count(self, qemu_env):
        """查看当前进程数量"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | wc -l")
        count = int(output[0].strip())
        assert count >= 1
        allure.attach(f"{count}", name="process_count", attachment_type=allure.attachment_type.TEXT)

    def test_sh_available(self, qemu_env):
        """确认 sh 可用"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("which sh")
        assert len(output) > 0


@allure.feature("Services")
@allure.severity(allure.severity_level.CRITICAL)
class TestService:
    def test_syslogd_running(self, qemu_env):
        """确认 syslogd 服务运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep syslogd | grep -v grep")
        assert len(output) > 0

    def test_cron_running(self, qemu_env):
        """确认 crond 服务运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep crond | grep -v grep")
        assert len(output) > 0

    def test_klogd_running(self, qemu_env):
        """确认 klogd 服务运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep klogd | grep -v grep")
        assert len(output) > 0
