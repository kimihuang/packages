"""冒烟测试：寄存器访问。"""


def test_read_pc(t32):
    """读取 PC 寄存器。"""
    reg = t32.register.read(name="PC")
    assert reg.value is not None
