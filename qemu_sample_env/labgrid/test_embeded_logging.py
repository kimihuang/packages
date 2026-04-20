"""embeded_logging (elog) 功能测试

elog 是一个类 Android logd 的嵌入式日志框架，包含：
- elogd 守护进程（已集成到 rootfs 并启动）
- elogcat 命令行工具（类似 Android logcat）
- elog_test 单元测试
- elog_integration_test 端到端集成测试
- elog_app 示例应用（日志生产者）

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
        """确认 elogd 三个 socket 文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        for sock in ["/var/run/elogdw", "/var/run/elogd", "/var/run/elogdr"]:
            output = shell.run_check(f"ls {sock}")
            assert len(output) > 0, f"Socket {sock} not found"


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
        shell.run_check("elogcat -c")


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


class TestElogFlush:
    """刷盘功能测试"""

    def test_log_files_exist(self, qemu_env):
        """确认刷盘日志文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ls /var/log/elog_*.log 2>/dev/null | wc -l")
        count = int(output[0].strip())
        assert count >= 1, "No elog log files found"

    def test_main_log_not_empty(self, qemu_env):
        """elog_app 写入后 main.log 应有内容"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("elogcat -c")
        shell.run_check("elog_app -n 3")
        # 等 flusher 周期
        shell.run_check("sleep 1")
        output = shell.run_check("wc -c < /var/log/elog_main.log")
        size = int(output[0].strip())
        assert size > 0, "elog_main.log is empty after elog_app wrote logs"


class TestElogWriteRead:
    """写日志+读日志端到端测试（系统 elogd）"""

    def test_elog_app_write_and_grow(self, qemu_env):
        """elog_app 写入日志后 buffer count 增长"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("elogcat -c")
        output_before = shell.run_check("elogcat -g")
        text_before = "\n".join(output_before)
        # 提取 main buffer count
        count_before = 0
        for line in output_before:
            if "main" in line and "count=" in line:
                for part in line.split():
                    if part.startswith("count="):
                        count_before = int(part.split("=")[1])
                        break

        shell.run_check("elog_app -n 10")

        output_after = shell.run_check("elogcat -g")
        text_after = "\n".join(output_after)
        count_after = 0
        for line in output_after:
            if "main" in line and "count=" in line:
                for part in line.split():
                    if part.startswith("count="):
                        count_after = int(part.split("=")[1])
                        break

        assert count_after > count_before, \
            f"Buffer count did not grow: before={count_before} after={count_after}"

    def test_clear_then_count_zero(self, qemu_env):
        """清空后 main buffer count 为 0"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("elogcat -c")
        output = shell.run_check("elogcat -g")
        for line in output:
            if "main" in line and "count=" in line:
                assert "count=0" in line, f"Buffer not cleared: {line}"
                break


class TestElogTest:
    """elog_test 单元测试套件"""

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
        """验证 10 个 e2e 端到端测试通过"""
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
        """验证 5 个并发测试通过"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_integration_test 2>&1", timeout=30)
        text = "\n".join(output)
        for case in [
            "concurrent writers", "concurrent readers",
            "concurrent write+read", "concurrent write+cmd",
            "concurrent multi-buffer",
        ]:
            assert f"PASS: {case}" in text, f"Missing: PASS: {case}"
