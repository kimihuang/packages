"""文件系统和存储测试"""

from labgrid.driver.shelldriver import ShellDriver


class TestFilesystem:
    def test_root_mount(self, qemu_env):
        """确认根文件系统已挂载"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("mount | grep ' / '")
        assert len(output) >= 1
        print(f"Root Mount: {output[0].strip()}")

    def test_root_writable(self, qemu_env):
        """确认根文件系统可写"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("echo test_write > /tmp/test_writable")
        output = shell.run_check("cat /tmp/test_writable")
        assert "test_write" in output[0]
        shell.run_check("rm /tmp/test_writable")

    def test_proc_mounts(self, qemu_env):
        """查看 /proc/mounts"""
        shell = qemu_env.get_driver(ShellDriver)
        output = shell.run_check("cat /proc/mounts")
        assert len(output) > 0
        print(f"Mount Points ({len(output)}):")
        for line in output:
            print(f"  {line}")

    def test_tmpfs_available(self, qemu_env):
        """确认 /tmp 可读写"""
        shell = qemu_env.get_driver(ShellDriver)
        shell.run_check("echo test > /tmp/test_tmpfs_check")
        output = shell.run_check("cat /tmp/test_tmpfs_check")
        assert "test" in output[0]
        shell.run_check("rm /tmp/test_tmpfs_check")
