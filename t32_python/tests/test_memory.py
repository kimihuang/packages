"""冒烟测试：内存读写。"""
import os

# 目标 RAM 地址，需根据目标 CPU 的内存映射调整
RAM_ADDR = int(os.environ.get("T32_RAM_ADDR", "0x20000000"), 16)


def test_memory_rw(t32):
    """写入并读回一个 32 位值。"""
    addr = t32.address(access="D", value=RAM_ADDR)
    t32.memory.write_uint32(address=addr, value=0x12345678)
    assert t32.memory.read_uint32(address=addr) == 0x12345678
