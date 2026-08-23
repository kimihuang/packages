"""配置加载器：从 JSON 加载环境启动配置。

路径约定（相对项目根）::

    modules/<env>/env.json   环境启动配置
"""
import json
import os
from dataclasses import dataclass
from typing import Any, Dict

# 项目根目录（framework/ 的上一级）
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULES_DIR = os.path.join(PROJECT_ROOT, "modules")


def _expand_path(value):
    """展开 ~ 为用户主目录。"""
    return os.path.expanduser(value) if isinstance(value, str) else value


@dataclass
class EnvConfig:
    """环境启动配置（该环境在哪跑、怎么启动目标）。"""
    name: str
    t32_bin: str
    t32_sys: str
    cpu_type: str = "CortexA53"
    node: str = "localhost"
    port: int = 20000
    pbi: str = "SIM"
    rcl: str = "NETTCP"
    startup_cmm: str = ""  # 空 = 由 case 脚本自行初始化

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "EnvConfig":
        return cls(
            name=d["name"],
            t32_bin=_expand_path(d["t32_bin"]),
            t32_sys=_expand_path(d["t32_sys"]),
            cpu_type=d.get("cpu_type", "CortexA53"),
            node=d.get("node", "localhost"),
            port=int(d.get("port", 20000)),
            pbi=d.get("pbi", "SIM"),
            rcl=d.get("rcl", "NETTCP"),
            startup_cmm=d.get("startup_cmm", ""),
        )

    @property
    def is_sim(self) -> bool:
        return self.pbi.upper() == "SIM"


def _load_json(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_env(env: str, base_dir: str = MODULES_DIR) -> EnvConfig:
    """加载环境启动配置（modules/<env>/env.json）。"""
    path = os.path.join(base_dir, env, "env.json")
    return EnvConfig.from_dict(_load_json(path))
