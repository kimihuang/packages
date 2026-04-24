"""cv_test (CV Test Framework) 功能测试

cv_test 是一个 C 语言单元测试框架，采用 Group -> Module -> Case 三级结构。
"""

import allure
import pytest
from labgrid.driver.shelldriver import ShellDriver


@allure.feature("cv_test")
@allure.story("Binary")
class TestCvTestBinary:
    """cv_test 二进制文件检查"""

    def test_cv_test_exists(self, qemu_env):
        """确认 cv_test 可执行文件存在"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("which cv_test")
        assert len(output) > 0

    def test_cv_test_help(self, qemu_env):
        """cv_test --help 正常输出"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cv_test --help")
        text = "\n".join(output)
        assert "cv_test" in text or "Usage" in text
        assert "--group" in text


@allure.feature("cv_test")
@allure.story("List")
class TestCvTestList:
    """--list / --list-detail 测试"""

    def test_list_all(self, qemu_env):
        """--list 列出所有测试用例"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cv_test --list")
        text = "\n".join(output)
        assert "group_math" in text
        allure.attach(text, name="test_list", attachment_type=allure.attachment_type.TEXT)

    def test_list_detail(self, qemu_env):
        """--list-detail 显示详细状态信息"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cv_test --list-detail")
        text = "\n".join(output)
        assert "ENABLED" in text
        allure.attach(text, name="test_list_detail", attachment_type=allure.attachment_type.TEXT)

    def test_list_has_groups(self, qemu_env):
        """验证至少有一个 group 已注册"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cv_test --list")
        groups = [l.strip() for l in output if l.strip().startswith("group_")]
        assert len(groups) >= 1, "No groups registered"


@allure.feature("cv_test")
@allure.story("Filter")
class TestCvTestFilter:
    """按组/模块/用例筛选执行"""

    def test_run_group_math(self, qemu_env):
        """-g group_math 运行指定组"""
        shell = qemu_env.get_driver(ShellDriver)
        stdout, _, _ = shell._run("cv_test -g group_math -s 2>&1", timeout=10)
        text = "\n".join(stdout)
        assert "SUMMARY" in text

    def test_filter_by_wildcard(self, qemu_env):
        """-k 通配符过滤用例"""
        shell = qemu_env.get_driver(ShellDriver)
        stdout, _, _ = shell._run("cv_test -k 'test_1_plus*' -s 2>&1", timeout=10)
        text = "\n".join(stdout)
        assert "SUMMARY" in text

    def test_filter_nonexistent(self, qemu_env):
        """筛选不存在的用例应标记为 SKIP"""
        shell = qemu_env.get_driver(ShellDriver)
        stdout, _, _ = shell._run("cv_test -c nonexistent_case -s 2>&1", timeout=10)
        text = "\n".join(stdout)
        assert "SKIP" in text


@allure.feature("cv_test")
@allure.story("Execution")
class TestCvTestExecution:
    """执行控制和结果检查"""

    def test_run_all(self, qemu_env):
        """运行全部测试"""
        shell = qemu_env.get_driver(ShellDriver)
        stdout, _, _ = shell._run("cv_test -s 2>&1", timeout=10)
        text = "\n".join(stdout)
        assert "SUMMARY" in text
        allure.attach(text, name="full_run", attachment_type=allure.attachment_type.TEXT)

    def test_run_all_has_pass(self, qemu_env):
        """全部测试中应有 PASS 用例"""
        shell = qemu_env.get_driver(ShellDriver)
        stdout, _, _ = shell._run("cv_test -s 2>&1", timeout=10)
        text = "\n".join(stdout)
        assert "PASS" in text


@allure.feature("cv_test")
@allure.story("Repeat")
class TestCvTestRepeat:
    """重复执行功能"""

    def test_count_2(self, qemu_env):
        """-n 2 重复执行 2 轮"""
        shell = qemu_env.get_driver(ShellDriver)
        stdout, _, _ = shell._run("cv_test -g group_math -n 2 -s 2>&1", timeout=15)
        text = "\n".join(stdout)
        assert "SUMMARY" in text

    def test_fail_fast(self, qemu_env):
        """-f 快速失败模式"""
        shell = qemu_env.get_driver(ShellDriver)
        stdout, _, _ = shell._run("cv_test -g group_math -f -s 2>&1", timeout=10)
        text = "\n".join(stdout)
        assert "SUMMARY" in text
