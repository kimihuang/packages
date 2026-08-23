# TRACE32 Python 自动化测试框架设计文档

> 基于 PYRCL（`lauterbach.trace32.rcl`）的**脚本即 case**测试框架。
> 核心思想：**case 即文件（cmm 或 py）**、**json 声明执行顺序**、**框架只做加载+连接+执行+报告**、**两角色分离**。

---

## 1. 设计原则

1. **case 即文件** —— 一条 case 就是一个 `.cmm` 脚本或一个 `.py` 脚本。
2. **json 声明顺序** —— `ci_testcase_seq.json` 声明本环境要执行哪些 case 及先后顺序，框架只读这个 json 执行（不扫描目录），满足 case 依赖顺序需求。
3. **cmm 即 testcase** —— 调试好的 cmm 脚本本身就是 case，框架用 `dbg.cmm()` 直接执行，**不做 cmm→json 转换**（转换会丢失变量/控制流/读改写）。
4. **两角色分离** —— 框架维护者维护加载/连接/执行/报告；业务用户只需写 cmm 或 py，并在 json 中登记顺序。
5. **env.json 保留** —— 环境启动配置（怎么连、什么 CPU、什么端口）与 case 解耦，由框架统一处理。
6. **DRY** —— 连接、加载、执行、CI 启动/报告等公共逻辑各只有一份实现。

---

## 2. 角色分离（两角色）

| 角色 | 维护内容 | 改动频率 | 技能要求 |
|------|----------|----------|----------|
| **核心框架维护者** | `framework/`、`conftest.py`、`pytest.ini`、`ci/` | 低 | 熟悉 PYRCL / pytest |
| **业务用户** | `modules/<env>/<module>/*.cmm`、`*.py`、`env.json`、`ci_testcase_seq.json` | 高 | 会写 cmm 或 Python |

---

## 3. 目录结构

```
t32_python/
├── framework/                        # 核心框架（维护者）
│   ├── __init__.py
│   ├── config.py                     # env.json 加载（EnvConfig）
│   ├── session.py                    # 连接工厂 connect / connect_target
│   └── runner.py                     # 读 ci_testcase_seq.json（load_seq）+ 执行 cmm/py
├── conftest.py                       # pytest 集成（load_seq 生成测试项 + dbg fixture）
├── test_cases.py                     # case 分发入口（维护者）
├── pytest.ini
├── modules/                          # 业务目录（用户）
│   ├── sim_board/                    # 环境一：模拟器（pbi=SIM）
│   │   ├── env.json                  # 环境启动配置
│   │   ├── ci_testcase_seq.json      # ← case 顺序声明（框架只读这个）
│   │   ├── cpu/
│   │   │   ├── smoke.cmm             # case（cmm 脚本）
│   │   │   ├── memory_check.py       # case（py 脚本）
│   │   │   ├── fail_demo.cmm         # 失败示例
│   │   │   └── fail_demo.py
│   └── soc_board/                    # 环境二：真实硬件（pbi=USB）
│       ├── env.json
│       ├── ci_testcase_seq.json
│       └── soc/
│           └── ddr_init.cmm          # 真实寄存器配置 case
├── ci/                               # 维护者
│   ├── lib.sh                        # 公共 shell 函数
│   ├── start.sh                      # 读环境 env.json 启动模拟器
│   └── run.sh                        # 读 json 顺序跑测试
└── requirements.txt
```

> **两级结构 + json 声明顺序**：一级「环境」（`sim_board`/`soc_board`，含 `env.json` + `ci_testcase_seq.json`），
> 二级「功能子目录」（`cpu`/`isp`/`soc`，放 cmm/py 脚本）。
> 框架**只读 `ci_testcase_seq.json` 声明的脚本**（不扫描目录），按数组顺序串行执行，满足 case 依赖顺序。

---

## 4. 两种 case 实现方式

### 4.1 方式一：cmm 脚本（推荐，最简单）

一个 `.cmm` 文件 = 一条 case。框架负责：连接（基于 `env.json`）→ `CD` 到模块目录 → `dbg.cmm(脚本)` 执行。

```cmm
; ddr_init.cmm —— 一条 case
Data.Set 0xFC000000 %Long 0x00000001
Data.Set 0xFC000004 %Long 0x12345678
WAIT 10.ms
```

- 脚本内可用 `~~~~` 引用模块目录下的相对文件（框架已 `CD` 到模块目录）
- 超时抛 `TimeoutError`（`dbg.cmm` 内部轮询 `PRACTICE.SD` 检测完成）

#### cmm pass/fail 标准（统一约定）

`dbg.cmm` 只能检测「跑完 vs 超时」，检测不到脚本内部逻辑失败，故框架制定统一标准：

| 表达 | 判定 |
|------|------|
| 脚本正常跑完 | **pass** |
| `PRINT %ERROR "<原因>"` | **fail**（消息行 type 变 16/2，框架读 `get_message()` 判定） |
| `ENDDO FALSE()` | ⚠️ 不判定（PYRCL 无读退出值 API，**不要用**） |

框架执行流程：清空消息行 → `dbg.cmm` 执行 → `get_message().type ∈ {2, 16}` 判 fail。
故 cmm 脚本的**校验失败路径必须用 `PRINT %ERROR` 表达**。

### 4.2 方式二：py 脚本（复杂逻辑用）

一个 `.py` 文件 = 一条 case，**约定定义 `run(dbg)` 函数**，框架负责连接并注入 `dbg`：

```python
# memory_test.py —— 一条 case
def run(dbg):
    addr = dbg.address(access="D", value=0x20000000)
    dbg.memory.write_uint32(address=addr, value=0x12345678)
    assert dbg.memory.read_uint32(address=addr) == 0x12345678
```

- `dbg` 由框架基于 `env.json` 连接好注入，py 脚本**无需自己连接**
- 复杂断言、循环、数据驱动逻辑写在这里（cmm 表达不了的部分）
- pass/fail 由 `assert`/`raise` 自然表达，无需额外约定

---

## 5. env.json（保留，环境启动配置）

```json
{
  "name": "sim-board",
  "t32_bin": "~/t32/bin/pc_linux64/t32marm",
  "t32_sys": "~/t32",
  "cpu_type": "CortexA53",
  "node": "localhost",
  "port": 20000,
  "pbi": "SIM",
  "rcl": "NETTCP",
  "startup_cmm": ""
}
```

| 字段 | 必填 | 说明 |
|------|------|------|
| `name` | 是 | 环境名 |
| `t32_bin` | 是 | TRACE32 可执行文件路径（支持 `~`） |
| `t32_sys` | 是 | TRACE32 系统目录 SYS（支持 `~`） |
| `cpu_type` | 否 | 目标 CPU 类型，默认 `CortexA53` |
| `node` / `port` | 否 | Remote API 节点/端口，默认 `localhost`/`20000` |
| `pbi` | 否 | 接口类型，默认 `SIM` |
| `rcl` | 否 | Remote API 协议（`NETTCP`/`NETASSIST`），默认 `NETTCP` |
| `startup_cmm` | 否 | 启动脚本内容；空 = 由 case 脚本自行初始化 |

---

## 5.1 ci_testcase_seq.json（case 顺序声明）

框架**只读这个文件**，按 `testcases` 数组顺序串行执行。

```json
{
  "testcases": [
    {
      "script": "cpu/01_init_ddr.cmm",
      "name": "DDR 初始化",
      "timeout_ms": 30000,
      "enabled": true,
      "critical": true
    },
    {
      "script": "cpu/03_memory_check.py",
      "timeout_ms": 5000,
      "enabled": true,
      "critical": false
    }
  ]
}
```

| 字段 | 必填 | 类型 | 默认 | 说明 |
|------|------|------|------|------|
| `script` | 是 | str | — | 脚本路径（相对环境目录，如 `cpu/smoke.cmm`） |
| `name` | 否 | str | `script` 去扩展名 | 可读名称（报告/测试 id 用） |
| `timeout_ms` | 否 | int | 框架默认 `5000` | 单 case 超时（毫秒） |
| `enabled` | 否 | bool | `true` | 是否启用（`false` 则跳过，不生成测试项） |
| `critical` | 否 | bool | `false` | 关键 case：失败则**中止后续** case（后续判 skip） |
| `retry_count` | 否 | int | `0` | 失败重试次数（0 = 不重试） |
| `retry_delay_ms` | 否 | int | `0` | 重试间隔（毫秒） |

超时优先级：`case 级 timeout_ms` > `框架默认 5000`。

---

## 6. 框架组件职责

### `framework/config.py`
- `load_env(env)` → `EnvConfig`（读 `modules/<env>/env.json`）

### `framework/session.py`
- `connect(env)` / `connect_target(env)`（连接 + `SYStem.CPU` + `SYStem.Up`/`Attach`）

### `framework/runner.py`（核心：读 json + 执行）
- `load_seq(env)` → 读 `modules/<env>/ci_testcase_seq.json`，按声明顺序返回 case 列表：
  - `{"type": "cmm", "id": "cpu/smoke", "path": ".../cpu/smoke.cmm", "module": "cpu"}`
  - `{"type": "py", "id": "cpu/memory_check", "path": ".../cpu/memory_check.py", "module": "cpu"}`
- `run_case(dbg, env, case)`：
  - cmm → `CD` 到 case 所在目录 + 清空消息 + `dbg.cmm(path, timeout)` + `get_message()` 判 fail
  - py → `importlib` 加载模块 + 调 `run(dbg)`

### `conftest.py` + `test_cases.py`
- fixture：`env_config` / `dbg`（session 级连接）
- `pytest_generate_tests`：`load_seq` 参数化为 `case_item`（保持 json 声明顺序）
- `test_cases.py::test_case(dbg, env_config, case_item)` 调 `runner.run_case()`

---

## 7. 执行入口（ci/）

```bash
./ci/start.sh --env sim_board              # 读 modules/sim_board/env.json 启动模拟器
./ci/run.sh --env sim_board                # 读 ci_testcase_seq.json 按序跑测试
./ci/run.sh --env sim_board --module cpu   # 只跑 cpu 功能子目录（-k "cpu/"）
./ci/run.sh --env sim_board --case smoke   # 只跑单条 case（-k 子串匹配）
```

---

## 8. 用户使用流程（业务用户）

1. 建环境：`mkdir modules/sim_board`，写 `modules/sim_board/env.json`
2. 建功能子目录：`mkdir modules/sim_board/cpu`，放 `.cmm`（寄存器配置/功能）或 `.py`（复杂逻辑）
3. 登记顺序：在 `modules/sim_board/ci_testcase_seq.json` 的 `testcases` 数组按依赖顺序登记脚本路径
4. 换环境/CPU/端口：改该环境 `env.json`
5. 跑测试：`./ci/start.sh --env sim_board && ./ci/run.sh --env sim_board`

> 框架只执行 `ci_testcase_seq.json` 里登记的脚本，按数组顺序串行执行，满足 case 依赖顺序。

---

## 9. 与上一版（目录扫描）的差异

| 维度 | 上一版 | 本版 |
|------|--------|------|
| case 发现 | 扫描目录下所有 cmm/py | **读 ci_testcase_seq.json**（按声明顺序） |
| 执行顺序 | 文件名字典序 | **json 数组顺序**（可表达依赖） |
| cmm 脚本 | 直接执行 | 直接执行（不变） |
| 复杂逻辑 | `.py` 写 `run(dbg)` | 不变 |
| 框架职责 | 扫描 + 连接 + 执行 | 加载 json + 连接 + 执行 + 报告 |
