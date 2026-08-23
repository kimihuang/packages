"""case 加载与执行：读取 ci_testcase_seq.json 声明的脚本列表，按序执行。

目录约定::

    modules/<env>/env.json                环境启动配置
    modules/<env>/ci_testcase_seq.json    case 顺序声明（框架只读这个文件）
    modules/<env>/<module>/<script>       实际脚本（cmm/py）

ci_testcase_seq.json 每条 case 支持的字段：
    script          脚本路径（必填，相对环境目录）
    name            可读名称（默认 = script 去扩展名）
    timeout_ms      单 case 超时（默认 5000）
    enabled         是否启用（默认 true）
    critical        是否关键（默认 false；关键 case 失败则中止后续 case）
    retry_count     失败重试次数（默认 0，即不重试）
    retry_delay_ms  重试间隔（毫秒，默认 0）

- cmm case：框架 CD 到脚本所在目录后 dbg.cmm() 执行（脚本即 case，不做转换）
- py case：框架加载模块，调用其 run(dbg)（dbg 由框架注入）
"""
import importlib.util
import json
import os
import sys
import time
from typing import Any, Dict, List

from .config import EnvConfig, MODULES_DIR

# session 级状态：critical case 是否已失败（用于中止后续 case）
_critical_failed = False

# 默认单 case 超时（毫秒），case 未指定 timeout_ms 时使用
DEFAULT_TIMEOUT_MS = 5000


class CaseSkippedError(Exception):
    """critical case 已失败，后续 case 被跳过。"""


def reset_seq_state():
    """重置 critical 失败状态（每次运行序列前调用）。"""
    global _critical_failed
    _critical_failed = False


def load_seq(env: str, base_dir: str = MODULES_DIR) -> List[Dict[str, Any]]:
    """读取环境目录的 ci_testcase_seq.json，返回有序 case 列表。"""
    env_dir = os.path.join(base_dir, env)
    seq_path = os.path.join(env_dir, "ci_testcase_seq.json")
    if not os.path.isfile(seq_path):
        raise ValueError("缺少 case 顺序声明文件: {}".format(seq_path))

    with open(seq_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    cases: List[Dict[str, Any]] = []
    for entry in data.get("testcases", []):
        script = entry["script"]
        path = os.path.join(env_dir, script)
        if not os.path.isfile(path):
            raise ValueError("脚本不存在: {}".format(path))

        if script.endswith(".cmm"):
            ctype, cid = "cmm", script[:-4]
        elif script.endswith(".py"):
            ctype, cid = "py", script[:-3]
        else:
            raise ValueError("未知脚本类型: {}".format(script))

        cases.append({
            "type": ctype,
            "id": cid,
            "path": path,
            "module": os.path.dirname(script),
            "name": entry.get("name", cid),
            "timeout_ms": entry.get("timeout_ms"),  # None = 用默认
            "enabled": entry.get("enabled", True),
            "critical": entry.get("critical", False),
            "retry_count": int(entry.get("retry_count", 0)),
            "retry_delay_ms": int(entry.get("retry_delay_ms", 0)),
        })
    return cases


def run_case(dbg, env: EnvConfig, case: Dict[str, Any]):
    """执行一条 case（失败按 retry_count 重试）。"""
    global _critical_failed
    if _critical_failed:
        raise CaseSkippedError("critical case 已失败，跳过后续 case")

    timeout_ms = case.get("timeout_ms") or DEFAULT_TIMEOUT_MS
    timeout = timeout_ms / 1000.0
    retry_count = case.get("retry_count", 0)
    retry_delay_ms = case.get("retry_delay_ms", 0)

    last_exc = None
    for attempt in range(retry_count + 1):
        try:
            _execute_case(dbg, case, timeout)
            return
        except Exception as e:
            last_exc = e
            if attempt < retry_count:
                if retry_delay_ms > 0:
                    time.sleep(retry_delay_ms / 1000.0)
                print("case {!r} 第 {}/{} 次重试（失败: {}）".format(
                    case["id"], attempt + 1, retry_count, e))

    # 重试耗尽仍失败
    if case.get("critical"):
        _critical_failed = True
    raise last_exc


def _execute_case(dbg, case: Dict[str, Any], timeout: float):
    """执行一次 case（不含重试）。"""
    case_type = case["type"]
    if case_type == "cmm":
        _run_cmm_case(dbg, case, timeout)
    elif case_type == "py":
        _run_py_case(dbg, case)
    else:
        raise ValueError("未知 case 类型: {!r}".format(case_type))


def _run_cmm_case(dbg, case: Dict[str, Any], timeout: float):
    """执行 cmm case，并按统一标准判定 pass/fail。

    pass/fail 标准：脚本内用 ``PRINT %ERROR "<原因>"`` 表达失败。
    ``PRINT %ERROR`` 会把消息行 type 设为 16（ERROR_INFO）或 2（ERROR），
    框架在执行后读消息行 type 判定。
    """
    case_dir = os.path.dirname(case["path"])
    dbg.cmd("CD {}".format(case_dir))
    dbg.cmd("PRINT")  # 清空消息行，避免读到陈旧消息
    dbg.cmm(case["path"], timeout=timeout)
    msg = dbg.get_message()
    if msg.type in (2, 16):  # 2=ERROR, 16=ERROR_INFO
        raise AssertionError(
            "cmm case {!r} 失败: {}".format(case["id"], msg.text.replace("\x00", "").strip())
        )


def _run_py_case(dbg, case: Dict[str, Any]):
    """加载 py 模块，调用其 run(dbg)。"""
    path = case["path"]
    mod_name = "t32_module_case_" + case["id"].replace("/", "_")
    spec = importlib.util.spec_from_file_location(mod_name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = mod
    spec.loader.exec_module(mod)
    if not hasattr(mod, "run"):
        raise ValueError("py case {!r} 缺少 run(dbg) 函数".format(path))
    mod.run(dbg)
