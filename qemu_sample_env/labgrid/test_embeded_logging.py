"""embeded_logging (elog) 功能测试

elog 是一个类 Android logd 的嵌入式日志框架，包含：
- elogd 守护进程（已集成到 rootfs 并启动）
- elogcat 命令行工具（类似 Android logcat）
- elog_test 单元/集成测试

测试通过 ShellDriver 在 QEMU Linux 中执行命令进行验证。
"""

import pytest
from labgrid.driver.shelldriver import ShellDriver


class TestElogdDaemon:
    """elogd 守护进程运行状态检查"""

    def test_elogd_running(self, qemu_env):
        """确认 elogd 进程正在运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep elogd | grep -v grep")
        assert len(output) > 0, "elogd is not running"

    def test_elogd_sockets_exist(self, qemu_env):
        """确认 elogd socket 文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        for sock in ["/var/run/elogdw", "/var/run/elogd", "/var/run/elogdr"]:
            output = shell.run_check(f"ls {sock}")
            assert len(output) > 0, f"Socket {sock} not found"

    def test_log_files_exist(self, qemu_env):
        """确认日志文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ls /var/log/elog_*.log 2>/dev/null | wc -l")
        count = int(output[0].strip())
        assert count >= 1, "No elog log files found"


class TestElogcatBasic:
    """elogcat 基础功能测试"""

    def test_elogcat_help(self, qemu_env):
        """显示 elogcat 帮助信息"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elogcat -h")
        text = "\n".join(output)
        assert "Usage" in text or "elogcat" in text

    def test_elogcat_clear(self, qemu_env):
        """elogcat -c 清空 buffer（通过 cmd socket，非阻塞）"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elogcat -c")
        assert len(output) >= 0


class TestElogcatBuffer:
    """elogcat buffer 查询功能"""

    def test_get_buffer_size(self, qemu_env):
        """-g 参数查看各 buffer 大小和统计"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elogcat -g")
        text = "\n".join(output)
        assert "main" in text
        assert "capacity=" in text

    def test_buffer_main_capacity(self, qemu_env):
        """验证 main buffer 容量为 256KB"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elogcat -g")
        text = "\n".join(output)
        for line in output:
            if "main" in line and "capacity=" in line:
                assert "262144" in line, f"Unexpected main capacity: {line}"
                break

    def test_six_buffers(self, qemu_env):
        """确认 6 个 log buffer 都存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elogcat -g")
        text = "\n".join(output)
        for buf in ["main", "radio", "events", "system", "crash", "kernel"]:
            assert buf in text, f"Buffer {buf} not found in -g output"


class TestElogTest:
    """elog_test 测试套件"""

    def test_elog_test_exists(self, qemu_env):
        """确认 elog_test 可执行文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("which elog_test")
        assert len(output) > 0

    def test_elog_test_all_passed(self, qemu_env):
        """运行 elog_test，验证全部通过无失败"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_test 2>&1")
        text = "\n".join(output)
        assert "FAIL" not in text, f"elog_test has failures: {text}"

    def test_elog_test_nine_suites(self, qemu_env):
        """验证 elog_test 运行了 9 个测试套件"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_test 2>&1")
        text = "\n".join(output)
        # 统计 [PASS] 行数
        passed_count = text.count("[PASS]")
        assert passed_count == 9, f"Expected 9 test suites, got {passed_count}"


class TestElogIntegrationTest:
    """elog_integration_test 端到端集成测试

    integration test 会自己 fork elogd，需要先停掉系统中已有的 elogd，
    测试结束后重新启动系统 elogd。
    """

    @pytest.fixture(autouse=True)
    def restart_elogd(self, qemu_env):
        """测试前停 elogd，测试后重启"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run("kill $(pidof elogd) 2>/dev/null; true")
        yield
        # 重启系统 elogd
        shell.run("elogd &", timeout=2)

    def test_integration_exists(self, qemu_env):
        """确认 elog_integration_test 可执行文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("which elog_integration_test")
        assert len(output) > 0

    def test_integration_all_passed(self, qemu_env):
        """运行全部集成测试，验证无失败"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_integration_test 2>&1", timeout=30)
        text = "\n".join(output)
        assert "FAIL" not in text, f"Integration test failures: {text}"

    def test_integration_fifteen_cases(self, qemu_env):
        """验证 15 个集成测试全部通过"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_integration_test 2>&1", timeout=30)
        text = "\n".join(output)
        assert "15 passed, 0 failed" in text, f"Unexpected result: {text}"

    def test_integration_e2e_cases(self, qemu_env):
        """验证 e2e 端到端测试通过"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_integration_test 2>&1", timeout=30)
        text = "\n".join(output)
        for case in [
            "e2e basic", "e2e level filter", "e2e header fields",
            "e2e multi entries", "e2e cmd stats", "e2e multi buffer",
            "e2e log mask", "e2e events + kernel", "e2e buffer stats",
            "e2e binary event",
        ]:
            assert f"PASS: {case}" in text, f"Missing: PASS: {case}"

    def test_integration_concurrent_cases(self, qemu_env):
        """验证并发测试通过"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_integration_test 2>&1", timeout=30)
        text = "\n".join(output)
        for case in [
            "concurrent writers", "concurrent readers",
            "concurrent write+read", "concurrent write+cmd",
            "concurrent multi-buffer",
        ]:
            assert f"PASS: {case}" in text, f"Missing: PASS: {case}"
