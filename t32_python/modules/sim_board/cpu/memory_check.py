# memory_check.py —— 内存读写校验示例（脚本即 case，定义 run(dbg)）
def run(dbg):
    addr = dbg.address(access="D", value=0x20000000)
    dbg.memory.write_uint32(address=addr, value=0x12345678)
    assert dbg.memory.read_uint32(address=addr) == 0x12345678
    print("memory_check case done")
