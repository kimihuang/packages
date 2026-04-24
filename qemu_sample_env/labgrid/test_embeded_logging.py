"""embeded_logging (elog) 功能测试

elog 是一个类 Android logd 的嵌入式日志框架，包含：
- elogd 守护进程（已集成到 rootfs 并启动）
- elogcat 命令行工具（类似 Android logcat）
- elog_test 单元测试
- elog_integration_test 端到端集成测试
"""

import allure
import pytest
from labgrid.driver.shelldriver import ShellDriver


@allure.feature("elog Daemon")
@allure.severity(allure.severity_level.CRITICAL)
class TestElogdDaemon:
    """elogd 守护进程运行状态检查"""

    def test_elogd_running(self, qemu_env):
        """确认 elogd 进程正在运行"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ps | grep elogd | grep -v grep")
        assert len(output) > 0, "elogd is not running"
        allure.attach(output[0], name="process", attachment_type=allure.attachment_type.TEXT)

    def test_elogd_sockets_exist(self, qemu_env):
        """确认 elogd 三个 socket 文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        for sock in ["/var/run/elogdw", "/var/run/elogd", "/var/run/elogdr"]:
            output = shell.run_check(f"ls {sock}")
            assert len(output) > 0, f"Socket {sock} not found"


@allure.feature("elog elogcat")
class TestElogcatBasic:
    """elogcat 基础功能测试"""

    def test_elogcat_help(self, qemu_env):
        """显示 elogcat 帮助信息"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elogcat -h")
        text = "\n".join(output)
        assert "Usage" in text or "elogcat" in text

    def test_elogcat_clear(self, qemu_env):
        """elogcat -c 清空 buffer"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("elogcat -c")


@allure.feature("elog Buffer")
class TestElogcatBuffer:
    """elogcat buffer 查询功能"""

    def test_get_buffer_size(self, qemu_env):
        """-g 参数查看各 buffer 大小和统计"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elogcat -g")
        text = "\n".join(output)
        assert "main" in text
        allure.attach(text, name="buffer_stats", attachment_type=allure.attachment_type.TEXT)

    def test_six_buffers(self, qemu_env):
        """确认 6 个 log buffer 都存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elogcat -g")
        text = "\n".join(output)
        for buf in ["main", "radio", "events", "system", "crash", "kernel"]:
            assert buf in text


@allure.feature("elog Flush")
class TestElogFlush:
    """刷盘功能测试"""

    def test_log_files_exist(self, qemu_env):
        """确认刷盘日志文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("ls /var/log/elog_*.log 2>/dev/null | wc -l")
        count = int(output[0].strip())
        assert count >= 1

    def test_main_log_not_empty(self, qemu_env):
        """elog_app 写入后 main.log 应有内容"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("elogcat -c")
        shell.run_check("elog_app -n 3")
        shell.run_check("sleep 1")
        output = shell.run_check("wc -c < /var/log/elog_main.log")
        size = int(output[0].strip())
        assert size > 0


@allure.feature("elog Write/Read")
class TestElogWriteRead:
    """写日志+读日志端到端测试"""

    def test_elog_app_write_and_grow(self, qemu_env):
        """elog_app 写入日志后 buffer count 增长"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("elogcat -c")
        output_before = shell.run_check("elogcat -g")
        count_before = 0
        for line in output_before:
            if "main" in line and "count=" in line:
                for part in line.split():
                    if part.startswith("count="):
                        count_before = int(part.split("=")[1])
                        break
        shell.run_check("elog_app -n 10")
        output_after = shell.run_check("elogcat -g")
        count_after = 0
        for line in output_after:
            if "main" in line and "count=" in line:
                for part in line.split():
                    if part.startswith("count="):
                        count_after = int(part.split("=")[1])
                        break
        assert count_after > count_before

    def test_clear_then_count_zero(self, qemu_env):
        """清空后 main buffer count 为 0"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("elogcat -c")
        output = shell.run_check("elogcat -g")
        for line in output:
            if "main" in line and "count=" in line:
                assert "count=0" in line
                break


@allure.feature("elog Unit Test")
class TestElogTest:
    """elog_test 单元测试套件"""

    def test_elog_test_all_passed(self, qemu_env):
        """运行 elog_test，验证全部通过"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_test 2>&1")
        text = "\n".join(output)
        assert "FAIL" not in text, f"elog_test has failures: {text}"
        allure.attach(text, name="elog_test_output", attachment_type=allure.attachment_type.TEXT)

    def test_elog_test_nine_suites(self, qemu_env):
        """验证 elog_test 运行了 9 个测试套件"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_test 2>&1")
        text = "\n".join(output)
        passed_count = text.count("[PASS]")
        assert passed_count == 9


@allure.feature("elog Integration Test")
@allure.severity(allure.severity_level.NORMAL)
class TestElogIntegrationTest:
    """elog_integration_test 端到端集成测试"""

    @pytest.fixture(autouse=True)
    def restart_elogd(self, qemu_env):
        shell = qemu_env.get_driver(ShellDriver)
        shell.run("kill $(pidof elogd) 2>/dev/null; true")
        yield
        shell.run("elogd &", timeout=2)

    def test_integration_all_passed(self, qemu_env):
        """运行全部集成测试，验证无失败"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_integration_test 2>&1", timeout=30)
        text = "\n".join(output)
        assert "FAIL" not in text
        allure.attach(text, name="integration_test_output", attachment_type=allure.attachment_type.TEXT)

    def test_integration_fifteen_cases(self, qemu_env):
        """验证 15 个集成测试全部通过"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("elog_integration_test 2>&1", timeout=30)
        text = "\n".join(output)
        assert "15 passed, 0 failed" in text
