#!/usr/bin/env python3
"""连接 TRACE32 并执行 cases/<name>.cmm，按 PRINT %ERROR 判定 pass/fail。

执行方式：DO case（非阻塞）→ 轮询 PRACTICE.SD() 直到栈深度回到基线。
注意：不用 PYRCL 的 dbg.cmm()，因为它在脚本内 Go 让目标 running 后，
轮询 PRACTICE.SD() 会因 "target running" 报 InternalError；这里自行实现轮询并容错。

用法：
  python3 run_case.py boot_to_shell --node localhost --port 20000 --timeout 120
"""
import argparse
import os
import sys
import time

import lauterbach.trace32.rcl as t32

# 项目根目录（scripts/ 的上一级）
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES_DIR = os.path.join(PROJECT_ROOT, "cases")

# env.json 的 RCL 取值 → PYRCL connect() 的 protocol 取值（同 t32_python）
_RCL_TO_PROTOCOL = {"NETTCP": "TCP", "NETASSIST": "UDP"}


def _run_cmm(dbg, case_path, case_name, timeout):
    """执行 cmm case：DO（非阻塞）+ 轮询 PRACTICE.SD() 检测完成。

    PRACTICE.SD() 返回当前 PRACTICE 脚本栈深度；脚本跑完会回到 DO 前的基线值。
    脚本内 Go 让目标 running 期间，RAPI 的 fnc 调用会因 "target running" 失败，
    此时捕获异常继续等待（脚本仍在执行）。
    """
    dbg.cmd("CD {}".format(PROJECT_ROOT))
    dbg.cmd("PRINT")  # 清空消息行，避免读到陈旧消息

    baseline = dbg.fnc("PRACTICE.SD()")
    dbg.cmd("DO {}".format(case_path))  # 非阻塞，脚本后台执行

    start = time.perf_counter()
    while True:
        try:
            sd = dbg.fnc("PRACTICE.SD()")
        except Exception:
            sd = None  # 目标 running 时 fnc 不可用，脚本仍在执行
        if sd is not None:
            if sd < baseline:
                raise RuntimeError(
                    "PRACTICE 栈深度异常: {} < {}".format(sd, baseline)
                )
            if sd == baseline:
                break  # 脚本执行完成
        if time.perf_counter() - start > timeout:
            print("TIMEOUT: {}".format(case_name))
            sys.exit(3)
        # 轮询间隔不能太短：高频 fnc 调用会让 T32 RCL 状态恶化（PRACTICE.SD()
        # 持续返回 InternalError），导致永远检测不到脚本完成。2s 已验证安全。
        time.sleep(2.0)


def main():
    parser = argparse.ArgumentParser(description="run a case under cases/")
    parser.add_argument("case", help="case 名（对应 cases/<name>.cmm，可省略 .cmm 后缀）")
    parser.add_argument("--node", default="localhost")
    parser.add_argument("--port", default="20000")
    parser.add_argument("--rcl", default="NETTCP", choices=["NETTCP", "NETASSIST"])
    parser.add_argument("--timeout", type=float, default=120.0, help="单 case 超时（秒）")
    args = parser.parse_args()

    case_name = args.case
    if not case_name.endswith(".cmm"):
        case_name += ".cmm"
    case_path = os.path.join(CASES_DIR, case_name)
    if not os.path.isfile(case_path):
        print("ERROR: case 不存在: {}".format(case_path))
        sys.exit(2)

    protocol = _RCL_TO_PROTOCOL.get(args.rcl, "TCP")
    dbg = t32.connect(node=args.node, port=str(args.port), protocol=protocol, timeout=60.0)
    try:
        _run_cmm(dbg, case_path, case_name, args.timeout)
        msg = dbg.get_message()
        if msg.type in (2, 16):  # 2=ERROR, 16=ERROR_INFO
            print("FAIL: {}: {}".format(case_name, msg.text.replace("\x00", "").strip()))
            sys.exit(1)
        print("PASS: {}".format(case_name))
    finally:
        dbg.disconnect()


if __name__ == "__main__":
    main()
