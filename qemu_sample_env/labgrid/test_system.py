"""系统信息测试：CPU、内核、内存"""

from labgrid.driver.shelldriver import ShellDriver


class TestCPU:
    def test_cpu_info(self, qemu_env):
        """查看 /proc/cpuinfo"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cat /proc/cpuinfo")
        assert len(output) > 0
        print("CPU Info:")
        for line in output:
            print(line)

    def test_cpu_architecture(self, qemu_env):
        """确认 CPU 架构为 aarch64"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("uname -m")
        assert "aarch64" in output[0]
        print(f"Architecture: {output[0]}")

    def test_cpu_online_count(self, qemu_env):
        """确认至少有 1 个 CPU 在线"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("nproc")
        count = int(output[0].strip())
        assert count >= 1
        print(f"Online CPUs: {count}")


class TestKernel:
    def test_kernel_version(self, qemu_env):
        """查看内核版本"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("uname -a")
        assert len(output) > 0
        print(f"Kernel: {output[0]}")

    def test_kernel_release(self, qemu_env):
        """确认内核版本号格式"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("uname -r")
        # e.g. 6.1.0
        parts = output[0].strip().split(".")
        assert len(parts) >= 2
        assert parts[0].isdigit()
        print(f"Kernel Release: {output[0].strip()}")


class TestMemory:
    def test_meminfo(self, qemu_env):
        """查看内存信息"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cat /proc/meminfo | head -5")
        assert len(output) > 0
        print("MemInfo:")
        for line in output:
            print(line)

    def test_total_memory(self, qemu_env):
        """确认总内存大于 0"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("awk '/^MemTotal/ {print $2}' /proc/meminfo")
        total_kb = int(output[0].strip())
        assert total_kb > 0
        total_mb = total_kb // 1024
        print(f"Total Memory: {total_mb} MB")
