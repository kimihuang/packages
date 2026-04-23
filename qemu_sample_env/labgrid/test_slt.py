"""linux-slt (System Level Test) 功能测试

slt 是一个系统级测试框架，通过串口与上位机通信，执行测试命令并返回结果。
核心组件：slt_daemon（守护进程）、slt_test（单元测试）。

在 QEMU 环境中串口不可用，主要测试进程状态、配置、二进制。
"""

import pytest
from labgrid.driver.shelldriver import ShellDriver


class TestSltDaemon:
    """slt_daemon 守护进程运行状态检查"""

    def test_slt_daemon_running(self, qemu_env):
        """确认 slt_daemon 进程正在运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep slt_daemon | grep -v grep")
        assert len(output) > 0, "slt_daemon is not running"

    def test_slt_daemon_started_with_config(self, qemu_env):
        """确认 slt_daemon 以正确参数启动"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep slt_daemon | grep -v grep")
        text = "\n".join(output)
        assert "/etc/slt/config.yaml" in text, \
            f"slt_daemon not started with config file: {text}"

    def test_slt_daemon_binary_exists(self, qemu_env):
        """确认 /usr/bin/slt_daemon 可执行文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ls -l /usr/bin/slt_daemon")
        assert len(output) > 0

    def test_slt_daemon_help(self, qemu_env):
        """slt_daemon --help 输出包含关键信息"""
        shell = qemu_env.get_driver(ShellDriver)
        # --help 显示帮助后继续运行（打开串口），用 run 不 check exit code
        stdout, _, _ = shell._run("/usr/bin/slt_daemon --help 2>&1", timeout=3)
        text = "\n".join(stdout)
        assert "SLT" in text or "slt" in text
        assert "--config" in text or "--port" in text


class TestSltConfig:
    """slt 配置文件检查"""

    def test_config_file_exists(self, qemu_env):
        """确认 /etc/slt/config.yaml 存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ls /etc/slt/config.yaml")
        assert len(output) > 0

    def test_config_has_serial_section(self, qemu_env):
        """配置文件包含 serial 段"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cat /etc/slt/config.yaml")
        text = "\n".join(output)
        assert "[serial]" in text, f"No [serial] section in config: {text[:300]}"

    def test_config_port_is_ttyAMA0(self, qemu_env):
        """串口配置为 ttyAMA0（QEMU aarch64 默认串口）"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cat /etc/slt/config.yaml")
        text = "\n".join(output)
        assert "ttyAMA0" in text, f"Serial port not set to ttyAMA0: {text[:300]}"


class TestSltDirectories:
    """slt 目录结构检查"""

    def test_log_directory_exists(self, qemu_env):
        """确认 /var/log/slt 目录存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ls -d /var/log/slt")
        assert len(output) > 0

    def test_log_directory_writable(self, qemu_env):
        """日志目录可写"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("touch /var/log/slt/test_write && rm /var/log/slt/test_write && echo OK")
        assert "OK" in output[0]
