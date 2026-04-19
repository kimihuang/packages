"""sort_app 排序应用功能测试

sort_app 是一个包含 7 种排序算法的命令行工具，已集成到 rootfs 中。
测试通过 ShellDriver 在 QEMU Linux 中执行 sort_app 命令进行验证。
"""

import pytest
from labgrid.driver.shelldriver import ShellDriver
from labgrid.driver.shelldriver import ShellDriver


class TestSortAppBasic:
    """基础功能测试"""

    def test_help(self, qemu_env):
        """显示帮助信息"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -h")
        assert len(output) > 0
        assert any("Usage" in line for line in output)

    def test_list_algorithms(self, qemu_env):
        """列出所有排序算法"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -l")
        text = "\n".join(output)
        for algo in ["bubble", "selection", "insertion", "shell", "merge", "quick", "heap"]:
            assert algo in text, f"Missing algorithm: {algo}"

    def test_single_sort_quick(self, qemu_env):
        """quick sort 排序 10 个元素"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -a quick -n 10 -s 42")
        text = "\n".join(output)
        assert "PASS" in text

    def test_single_sort_bubble(self, qemu_env):
        """bubble sort 排序 10 个元素"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -a bubble -n 10 -s 42")
        text = "\n".join(output)
        assert "PASS" in text

    def test_single_sort_merge(self, qemu_env):
        """merge sort 排序 10 个元素"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -a merge -n 10 -s 42")
        text = "\n".join(output)
        assert "PASS" in text


class TestSortAppAlgorithms:
    """所有 7 种算法正确性验证"""

    @pytest.mark.parametrize("algo", [
        "bubble", "selection", "insertion", "shell", "merge", "quick", "heap",
    ])
    def test_algorithm_sorted(self, qemu_env, algo):
        """每种算法排序后验证结果有序"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check(f"sort_app -a {algo} -n 100 -s 42 -p")
        text = "\n".join(output)

        # 提取 After 行的数字
        after_line = None
        for line in output:
            if line.strip().startswith("After:"):
                after_line = line.strip()
                break
        assert after_line is not None, "No 'After:' line in output"

        numbers_str = after_line.split(":", 1)[1].strip()
        numbers = list(map(int, numbers_str.split()))
        assert len(numbers) == 100

        # 验证升序
        for i in range(len(numbers) - 1):
            assert numbers[i] <= numbers[i + 1], \
                f"{algo}: not sorted at index {i}: {numbers[i]} > {numbers[i+1]}"


class TestSortAppEdgeCases:
    """边界场景测试"""

    def test_single_element(self, qemu_env):
        """排序 1 个元素"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -a quick -n 1 -s 42 -p")
        text = "\n".join(output)
        assert "PASS" in text

    def test_already_sorted(self, qemu_env):
        """已排序数组（seed 固定生成）"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -a quick -n 50 -s 1")
        text = "\n".join(output)
        assert "PASS" in text

    def test_large_array(self, qemu_env):
        """大数组排序 50000 元素"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -a quick -n 50000 -s 42")
        text = "\n".join(output)
        assert "PASS" in text

    def test_benchmark_all(self, qemu_env):
        """benchmark 模式运行全部算法"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("sort_app -b -n 1000 -s 42")
        text = "\n".join(output)
        # 每个算法都应显示 PASS
        assert "PASS" in text
        # 验证 7 种算法都运行了
        for algo in ["bubble", "selection", "insertion", "shell", "merge", "quick", "heap"]:
            assert algo in text, f"Missing algorithm in benchmark: {algo}"

