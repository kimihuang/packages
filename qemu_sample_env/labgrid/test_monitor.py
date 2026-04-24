"""QEMU QMP Monitor 测试"""

import allure
from labgrid.driver.qemudriver import QEMUDriver


@allure.feature("QEMU Monitor")
@allure.severity(allure.severity_level.CRITICAL)
class TestQEMUMonitor:
    def test_query_version(self, qemu_env):
        """查询 QEMU 版本"""
        qemu = qemu_env.get_driver(QEMUDriver)
        result = qemu.monitor_command("query-version")
        assert "qemu" in result
        major = result["qemu"]["major"]
        minor = result["qemu"]["minor"]
        allure.attach(f"QEMU Version: {major}.{minor}", name="version", attachment_type=allure.attachment_type.TEXT)

    def test_query_status(self, qemu_env):
        """查询 VM 运行状态"""
        qemu = qemu_env.get_driver(QEMUDriver)
        result = qemu.monitor_command("query-status")
        assert result["status"] == "running"
        allure.attach(f"VM Status: {result['status']}", name="status", attachment_type=allure.attachment_type.TEXT)

    def test_query_cpus(self, qemu_env):
        """查询 CPU 拓扑"""
        qemu = qemu_env.get_driver(QEMUDriver)
        result = qemu.monitor_command("query-cpus-fast")
        cpu_count = len(result)
        assert cpu_count >= 1
        allure.attach(f"CPU Count: {cpu_count}", name="cpus", attachment_type=allure.attachment_type.TEXT)
