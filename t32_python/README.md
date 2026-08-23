# TRACE32 CI 测试框架 — 使用说明

> 面向**业务用户**：用 cmm/py 脚本 + 两份 JSON 配置，即可把调试好的 T32 脚本接入自动化测试。
> 框架设计细节见 [DESIGN.md](DESIGN.md)，本文档只讲「怎么用」。

---

## 1. 框架是什么

一个「**脚本即 case**」的 TRACE32 自动化测试框架：

- 你调试好的 `.cmm` 脚本（寄存器配置/功能）或 `.py` 脚本（复杂逻辑），**直接就是一条 case**
- 框架按 `ci_testcase_seq.json` 里声明的顺序，串行执行这些脚本
- 框架负责：连接 T32 → 执行脚本 → 判定 pass/fail → 生成报告

---

## 2. 快速开始

```bash
cd t32_python

# 1. 安装依赖（一次性）
python -m pip install ./tools/lauterbach_trace32_rcl-1.0.7-py3-none-any.whl  # 见 requirements.txt 说明
python -m pip install pytest pytest-html

# 2. 启动模拟器（读 modules/sim_board/env.json）
./ci/start.sh --env sim_board

# 3. 跑测试（读 modules/sim_board/ci_testcase_seq.json，按声明顺序执行）
T32_VENV=/path/to/.venv ./ci/run.sh --env sim_board
```

> `T32_VENV` 指向装了 PYRCL + pytest 的 venv；若 `pytest` 已在 PATH 中可省略。

---

## 3. 目录结构（你只需关心 modules/）

```
modules/
├── sim_board/                       # 环境一：模拟器
│   ├── env.json                     # 环境启动配置
│   ├── ci_testcase_seq.json         # case 顺序声明（框架只读这个）
│   └── cpu/                         # 功能子目录
│       ├── 01_init_ddr.cmm          # case（cmm 脚本）
│       ├── 02_config_clock.cmm
│       └── 03_memory_check.py       # case（py 脚本）
└── soc_board/                       # 环境二：真实硬件
    ├── env.json
    ├── ci_testcase_seq.json
    └── soc/
        └── ddr_init.cmm
```

---

## 4. 写一条 case

### 4.1 cmm 脚本（寄存器配置/功能，推荐）

一个 `.cmm` 文件 = 一条 case，框架用 `dbg.cmm()` 直接执行。

```cmm
; 01_init_ddr.cmm —— DDR 初始化
Data.Set A:0xFD000000 %Long 0x00000001
Data.Set A:0xFD000100 %Long 0x00000114
WAIT 1.ms

; 读回校验：失败用 PRINT %ERROR 表达
IF Data.Long(A:0xFD000008)&0x1
(
    PRINT "DDR ready"
)
ELSE
(
    PRINT %ERROR "DDR init failed"
)
```

### 4.2 py 脚本（复杂逻辑）

一个 `.py` 文件 = 一条 case，**约定定义 `run(dbg)` 函数**，框架连接好注入 `dbg`。

```python
# 03_memory_check.py —— 内存读写校验
def run(dbg):
    addr = dbg.address(access="D", value=0x20000000)
    dbg.memory.write_uint32(address=addr, value=0x12345678)
    assert dbg.memory.read_uint32(address=addr) == 0x12345678
```

---

## 5. 配置文件

### 5.1 `env.json`（环境启动配置）

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
| `pbi` | 否 | 接口类型：`SIM`（模拟器）/ `USB`/`NET`（真实硬件） |
| `rcl` | 否 | `NETTCP`（TCP）/ `NETASSIST`（UDP） |
| `startup_cmm` | 否 | 启动脚本内容；**空 = 由 case 脚本自行初始化** |

### 5.2 `ci_testcase_seq.json`（case 顺序声明）

框架**只读这个文件**，按 `testcases` 数组顺序串行执行。

```json
{
  "testcases": [
    {
      "script": "cpu/01_init_ddr.cmm",
      "name": "DDR 初始化",
      "timeout_ms": 30000,
      "enabled": true,
      "critical": true,
      "retry_count": 0,
      "retry_delay_ms": 0
    },
    {
      "script": "cpu/03_memory_check.py",
      "timeout_ms": 5000,
      "enabled": true,
      "critical": false,
      "retry_count": 1,
      "retry_delay_ms": 200
    }
  ]
}
```

| 字段 | 必填 | 默认 | 说明 |
|------|------|------|------|
| `script` | 是 | — | 脚本路径（相对环境目录，如 `cpu/smoke.cmm`） |
| `name` | 否 | 脚本名 | 可读名称（报告/测试 id 用） |
| `timeout_ms` | 否 | `5000` | 单 case 超时（毫秒） |
| `enabled` | 否 | `true` | `false` 则跳过该 case |
| `critical` | 否 | `false` | 关键 case：失败则**中止后续** case |
| `retry_count` | 否 | `0` | 失败重试次数 |
| `retry_delay_ms` | 否 | `0` | 重试间隔（毫秒） |

---

## 6. pass / fail 约定

| case 类型 | 失败表达 | 框架判定 |
|-----------|----------|----------|
| cmm | `PRINT %ERROR "<原因>"` | 消息行 type ∈ {2,16} 判 fail |
| py | `assert` / `raise` | 异常冒泡判 fail |

> ⚠️ **cmm 脚本不要用 `ENDDO FALSE()` 表达失败**（框架检测不到），一律用 `PRINT %ERROR`。

---

## 7. 常见操作

**加一个新环境（如真实硬件）**

```bash
mkdir modules/board_a
# 写 modules/board_a/env.json（pbi=USB，cpu_type=真实芯片）
# 写 modules/board_a/ci_testcase_seq.json（登记脚本）
mkdir modules/board_a/soc
# 放 cmm/py 脚本到 modules/board_a/soc/
```

**加一条 case**：脚本放进功能子目录 + 在 `ci_testcase_seq.json` 登记

**临时禁用某 case**：`enabled: false`

**设长超时**：`timeout_ms: 60000`（长流程如 DDR 初始化/Linux 启动）

**偶发失败重试**：`retry_count: 2, retry_delay_ms: 500`

**声明依赖顺序**：按执行先后排列 `testcases` 数组；关键前置步骤设 `critical: true`（失败则不再跑后续）

**只跑部分 case**

```bash
./ci/run.sh --env sim_board --module cpu      # 只跑 cpu 子目录
./ci/run.sh --env sim_board --case smoke      # 只跑名字含 smoke 的 case
```

---

## 8. 报告

每次运行生成两份报告：

- `report_<env>.html` — 浏览器打开，人工查看
- `report_<env>.xml` — JUnit 格式，CI 机器解析

---

## 9. 字段速查

**env.json**：`name` `t32_bin` `t32_sys` `cpu_type` `node` `port` `pbi` `rcl` `startup_cmm`

**ci_testcase_seq.json（每条 case）**：`script` `name` `timeout_ms` `enabled` `critical` `retry_count` `retry_delay_ms`
