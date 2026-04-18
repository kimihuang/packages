"""QEMU QMP Monitor 测试"""

from labgrid.driver.qemudriver import QEMUDriver


class TestQEMUMonitor:
    def test_query_version(self, qemu_env):
        """查询 QEMU 版本"""
        qemu = qemu_env.get_driver(QEMUDriver)
        result = qemu.monitor_command("query-version")
        assert "qemu" in result
        major = result["qemu"]["major"]
        minor = result["qemu"]["minor"]
        print(f"QEMU Version: {major}.{minor}")

    def test_query_status(self, qemu_env):
        """查询 VM 运行状态"""
        qemu = qemu_env.get_driver(QEMUDriver)
        result = qemu.monitor_command("query-status")
        assert result["status"] == "running"
        print(f"VM Status: {result['status']}")

    def test_query_cpus(self, qemu_env):
        """查询 CPU 拓扑"""
        qemu = qemu_env.get_driver(QEMUDriver)
        result = qemu.monitor_command("query-cpus-fast")
        cpu_count = len(result)
        print(f"CPU Count: {cpu_count}")
        assert cpu_count >= 1
