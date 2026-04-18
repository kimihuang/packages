"""进程和服务测试"""

from labgrid.driver.shelldriver import ShellDriver


class TestProcess:
    def test_init_process(self, qemu_env):
        """确认 init 进程 (PID 1) 存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | head -2")
        assert len(output) >= 2
        print(f"Init: {output[1].strip()}")

    def test_process_count(self, qemu_env):
        """查看当前进程数量"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | wc -l")
        count = int(output[0].strip())
        assert count >= 1
        print(f"Process Count: {count}")

    def test_sh_available(self, qemu_env):
        """确认 sh 可用"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("which sh")
        assert len(output) > 0
        print(f"sh: {output[0].strip()}")


class TestService:
    def test_syslogd_running(self, qemu_env):
        """确认 syslogd 服务运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep syslogd | grep -v grep")
        assert len(output) > 0
        print("syslogd: running")

    def test_cron_running(self, qemu_env):
        """确认 crond 服务运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep crond | grep -v grep")
        assert len(output) > 0
        print("crond: running")

    def test_klogd_running(self, qemu_env):
        """确认 klogd 服务运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep klogd | grep -v grep")
        assert len(output) > 0
        print("klogd: running")
