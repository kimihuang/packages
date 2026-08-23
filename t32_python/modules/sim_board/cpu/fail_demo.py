# fail_demo.py —— py 失败示例（故意断言失败，用于验证 py case 的 fail 判定）
#
# py case 的 pass/fail 由 assert/raise 自然表达：断言失败抛异常，pytest 判 fail。
# 本 case 故意断言错误期望值，触发失败；日常 CI 应删除或移出。
def run(dbg):
    addr = dbg.address(access="D", value=0x20000000)
    dbg.memory.write_uint32(address=addr, value=0x12345678)
    actual = dbg.memory.read_uint32(address=addr)
    # 故意断言错误期望值，触发失败
    assert actual == 0xDEADBEEF, "intentional failure: expected 0xDEADBEEF, got {:#x}".format(actual)
