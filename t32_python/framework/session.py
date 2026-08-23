"""连接工厂：基于环境配置建立 Remote API 连接。"""
import lauterbach.trace32.rcl as t32

from .config import EnvConfig

# config.t32 的 RCL 取值 → PYRCL connect() 的 protocol 取值
_RCL_TO_PROTOCOL = {
    "NETTCP": "TCP",
    "NETASSIST": "UDP",
}


def connect(env: EnvConfig):
    """建立 Remote API 连接（不初始化目标）。"""
    return t32.connect(
        node=env.node,
        port=str(env.port),
        protocol=_RCL_TO_PROTOCOL.get(env.rcl, "TCP"),
        timeout=60.0,
    )


def connect_target(env: EnvConfig):
    """连接并按目标类型初始化（sim: SYStem.Up / hw: SYStem.Attach）。"""
    dbg = connect(env)
    dbg.cmd("SYStem.CPU {}".format(env.cpu_type))
    dbg.cmd("SYStem.Up" if env.is_sim else "SYStem.Attach")
    return dbg
